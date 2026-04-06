// ai_bridge.c — AI IPC Bridge (Phase 8)
// Kernel ↔ Python AI Engine via COM2 serial port (0x2F8)

#include "ai_bridge.h"
#include <stddef.h>

extern void serial_print(const char *s);

// =============================================================================
// COM2 Port I/O (isolated from COM1 debug output)
// =============================================================================

static inline void ai_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t ai_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// =============================================================================
// Bridge State
// =============================================================================

static AiBridgeStatus g_status      = AI_STATUS_OFFLINE;
static int            g_com2_hw     = 0;   // 1 = COM2 UART physically present
static uint32_t       g_next_seq    = 1;   // rolling sequence number for pairing REQ/RESP
static AiTuneHandler  g_tune_handler = (void*)0;
static AiRemediateHandler g_remediate_handler = (void*)0;

// =============================================================================
// COM2 Init
// =============================================================================

void ai_bridge_init(void) {
    ai_outb(AI_COM_PORT + 1, 0x00);  // disable interrupts
    ai_outb(AI_COM_PORT + 3, 0x80);  // DLAB on
    ai_outb(AI_COM_PORT + 0, 0x01);  // divisor lo → 115200 baud
    ai_outb(AI_COM_PORT + 1, 0x00);  // divisor hi
    ai_outb(AI_COM_PORT + 3, 0x03);  // 8N1
    ai_outb(AI_COM_PORT + 2, 0xC7);  // FIFO, clear, 14-byte threshold
    ai_outb(AI_COM_PORT + 4, 0x0B);  // RTS/DSR on

    // Detect whether COM2 is actually present.
    // When no UART exists the I/O port bus floats to 0xFF.
    // Write a scratch value to the scratch register (offset 7) and read back;
    // a real 16550 returns the value, a missing port still returns 0xFF.
    ai_outb(AI_COM_PORT + 7, 0xA5);
    uint8_t scratch = ai_inb(AI_COM_PORT + 7);
    if (scratch != 0xA5) {
        g_status  = AI_STATUS_OFFLINE;
        g_com2_hw = 0;
        serial_print("[ai] COM2 not present — AI bridge offline\n");
        return;
    }

    g_com2_hw  = 1;
    g_status   = AI_STATUS_READY;
    g_next_seq = 1;
    serial_print("[ai] bridge ready on COM2 (0x2F8, 115200 baud)\n");
}

AiBridgeStatus ai_bridge_status(void) {
    return g_status;
}

void ai_bridge_set_status(AiBridgeStatus status) {
    g_status = status;
}

// =============================================================================
// Low-level COM2 I/O
// =============================================================================

static void ai_putc(char c) {
    while (!(ai_inb(AI_COM_PORT + 5) & 0x20));  // wait TX buffer empty
    ai_outb(AI_COM_PORT, (uint8_t)c);
}

static void ai_puts(const char *s) {
    for (; *s; s++) ai_putc(*s);
}

// Write a 32-bit decimal number to COM2
static void ai_put_dec(uint32_t v) {
    if (v == 0) { ai_putc('0'); return; }
    char buf[12]; int i = 0;
    while (v) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) ai_putc(buf[j]);
}

static int ai_getc(char *out) {
    if (!(ai_inb(AI_COM_PORT + 5) & 0x01)) return 0;  // no data ready
    *out = (char)ai_inb(AI_COM_PORT);
    return 1;
}

// =============================================================================
// Protocol: "REQ|<seq>|<payload>\n"  /  "RESP|<seq>|<payload>\n"
// =============================================================================

static void ai_send_frame(const char *type, uint32_t seq, const char *payload) {
    ai_puts(type);
    ai_putc('|');
    ai_put_dec(seq);
    ai_putc('|');
    ai_puts(payload);
    ai_putc('\n');
}

// Read one line from COM2 into buf (strips '\n').
// Returns number of bytes read, 0 if no complete line yet (non-blocking), -1 on overflow.
static int ai_readline(char *buf, size_t max_len) {
    static char  rx_line[AI_MSG_MAX];
    static size_t rx_pos = 0;

    char c;
    while (ai_getc(&c)) {
        if (c == '\r') continue;
        if (c == '\n') {
            rx_line[rx_pos] = '\0';
            size_t n = rx_pos;
            rx_pos = 0;
            if (n >= max_len) return -1;
            for (size_t i = 0; i <= n; i++) buf[i] = rx_line[i];
            return (int)n;
        }
        if (rx_pos < AI_MSG_MAX - 1) rx_line[rx_pos++] = c;
    }
    return 0;  // no complete line yet
}

// =============================================================================
// Public API
// =============================================================================

int ai_query_sync(const char *query, char *resp_buf, size_t max_len) {
    if (!g_com2_hw || !query || !resp_buf || max_len < 2)
        return -1;

    uint32_t seq = g_next_seq++;
    g_status = AI_STATUS_BUSY;

    // Send: "REQ|<seq>|<query>\n"
    ai_send_frame("REQ", seq, query);

    // Spin-poll for matching RESP
    char line[AI_MSG_MAX];
    for (uint32_t spin = AI_RESP_TIMEOUT; spin; spin--) {
        int n = ai_readline(line, AI_MSG_MAX);
        if (n <= 0) continue;

        // Parse "RESP|<seq>|<payload>"
        if (line[0] != 'R' || line[1] != 'E' || line[2] != 'S' ||
            line[3] != 'P' || line[4] != '|') continue;

        // Find second '|'
        char *p = line + 5;
        while (*p && *p != '|') p++;
        if (*p != '|') continue;
        *p = '\0';

        // Check sequence matches
        uint32_t recv_seq = 0;
        for (char *d = line + 5; *d; d++) recv_seq = recv_seq * 10 + (uint32_t)(*d - '0');
        if (recv_seq != seq) continue;

        // Copy payload
        char *payload = p + 1;
        size_t i = 0;
        while (payload[i] && i < max_len - 1) { resp_buf[i] = payload[i]; i++; }
        resp_buf[i] = '\0';
        g_status = AI_STATUS_READY;
        return (int)i;
    }

    g_status = AI_STATUS_READY;
    return -1;  // timeout
}

void ai_send_event(const char *event_type, const char *data) {
    if (!g_com2_hw) return;   // no COM2 hardware — skip serial write
    ai_puts("EVT|");
    ai_puts(event_type);
    ai_putc('|');
    ai_puts(data ? data : "");
    ai_putc('\n');
}

// =============================================================================
// CMD frame polling (Phase 11: Auto-Tuner → Kernel)
// =============================================================================

void ai_set_tune_handler(AiTuneHandler handler) {
    g_tune_handler = handler;
}

void ai_set_remediate_handler(AiRemediateHandler handler) {
    g_remediate_handler = handler;
}

// =============================================================================
// Helper: build "p1|p2|p3" event data string
// =============================================================================

char *ai_build_event(char *buf, size_t max,
                     const char *p1, const char *p2, const char *p3) {
    size_t pos = 0;
    if (p1) { for (const char *s = p1; *s && pos < max - 1; s++) buf[pos++] = *s; }
    if (p2) { if (pos < max - 1) buf[pos++] = '|';
              for (const char *s = p2; *s && pos < max - 1; s++) buf[pos++] = *s; }
    if (p3) { if (pos < max - 1) buf[pos++] = '|';
              for (const char *s = p3; *s && pos < max - 1; s++) buf[pos++] = *s; }
    buf[pos] = '\0';
    return buf;
}

// Parse a decimal string into uint32_t (no libc available)
static uint32_t ai_parse_dec(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); s++; }
    return v;
}

// Split src at first '|', write NUL at delimiter, return pointer past it.
// Returns NULL if '|' not found.
static char *ai_split(char *src, char **rest) {
    char *p = src;
    while (*p && *p != '|') p++;
    if (!*p) { *rest = p; return (void*)0; }
    *p = '\0';
    *rest = p + 1;
    return p + 1;  // non-null = success
}

void ai_poll_cmd(void) {
    if (!g_com2_hw) return;   // no COM2 hardware — nothing to poll

    char line[AI_MSG_MAX];
    int n = ai_readline(line, AI_MSG_MAX);
    if (n <= 0) return;  // no complete line yet

    // Expect: CMD|<seq>|TUNE|<action>|<target>|<value>|<reason>
    // Or the AutoTunerModule sends: CMD|0|TUNE|action|target|value|reason
    // Wire format from to_cmd_payload(): "TUNE|action|target|value|reason"
    // Full frame: CMD|0|TUNE|action|target|value|reason

    if (line[0] != 'C' || line[1] != 'M' || line[2] != 'D' || line[3] != '|')
        return;

    char *p = line + 4;  // after "CMD|"
    char *rest;

    // Skip seq field
    if (!ai_split(p, &rest)) return;
    p = rest;

    // Next token should be "TUNE" or "REMEDIATE"
    char *subcmd_start = p;
    if (!ai_split(p, &rest)) return;

    // Check for TUNE command
    if (subcmd_start[0] == 'T' && subcmd_start[1] == 'U' &&
        subcmd_start[2] == 'N' && subcmd_start[3] == 'E') {
        p = rest;

        // action
        char *action = p;
        if (!ai_split(p, &rest)) return;
        p = rest;

        // target
        char *target = p;
        if (!ai_split(p, &rest)) return;
        p = rest;

        // value (floating-point in wire format like "15.00" — take integer part)
        char *value_str = p;
        if (!ai_split(p, &rest)) {
            rest = p + 0;
        }
        uint32_t value = ai_parse_dec(value_str);

        // reason (remainder of the line)
        char *reason = rest;

        if (g_tune_handler) {
            g_tune_handler(action, target, value, reason);
        }
        return;
    }

    // Check for REMEDIATE command: CMD|0|REMEDIATE|action|target|param
    if (subcmd_start[0] == 'R' && subcmd_start[1] == 'E' &&
        subcmd_start[2] == 'M' && subcmd_start[3] == 'E') {
        p = rest;

        // action (log, throttle, kill, revoke, suspend)
        char *action = p;
        if (!ai_split(p, &rest)) return;
        p = rest;

        // target (PID string or "all")
        char *target = p;
        if (!ai_split(p, &rest)) {
            rest = p;
        }
        p = rest;

        // param (optional: quantum ms, capability name, etc.)
        char *param = p;

        if (g_remediate_handler) {
            g_remediate_handler(action, target, param);
        }
        return;
    }

    // Check for POLICY command: CMD|0|POLICY|<hex-encoded blob>
    // Hex decoding: each pair of hex chars = 1 byte
    if (subcmd_start[0] == 'P' && subcmd_start[1] == 'O' &&
        subcmd_start[2] == 'L' && subcmd_start[3] == 'I') {
        p = rest;

        // Hex decode in-place
        static uint8_t policy_buf[AI_MSG_MAX / 2];
        size_t blob_len = 0;
        while (*p && *(p+1) && blob_len < sizeof(policy_buf)) {
            uint8_t hi = 0, lo = 0;
            if (p[0] >= '0' && p[0] <= '9') hi = p[0] - '0';
            else if (p[0] >= 'a' && p[0] <= 'f') hi = p[0] - 'a' + 10;
            else if (p[0] >= 'A' && p[0] <= 'F') hi = p[0] - 'A' + 10;
            else break;
            if (p[1] >= '0' && p[1] <= '9') lo = p[1] - '0';
            else if (p[1] >= 'a' && p[1] <= 'f') lo = p[1] - 'a' + 10;
            else if (p[1] >= 'A' && p[1] <= 'F') lo = p[1] - 'A' + 10;
            else break;
            policy_buf[blob_len++] = (hi << 4) | lo;
            p += 2;
        }

        if (blob_len >= 8) {
            ai_kernel_engine_load_policy(policy_buf, blob_len);
        }
        return;
    }
}

// =============================================================================
// Syscall dispatcher
// =============================================================================

uint32_t ai_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    (void)a3;
    switch (num) {
        case SYS_AI_QUERY: {
            const char *query    = (const char *)(uintptr_t)a1;
            char       *resp_buf = (char *)(uintptr_t)a2;
            return (uint32_t)ai_query_sync(query, resp_buf, AI_MSG_MAX);
        }
        case SYS_AI_STATUS:
            return (uint32_t)g_status;
        case SYS_AI_EVENT: {
            const char *etype = (const char *)(uintptr_t)a1;
            const char *data  = (const char *)(uintptr_t)a2;
            ai_send_event(etype, data);
            return 0;
        }
        default:
            return (uint32_t)-1;
    }
}
