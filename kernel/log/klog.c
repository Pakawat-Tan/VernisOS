// klog.c — Kernel Structured Logging System (Phase 16)
//
// Ring buffer backed, serial-output structured logging.
// Format: [tick][LEVEL][tag] message

#include "klog.h"

// External kernel functions
extern void serial_print(const char *s);
extern uint32_t kernel_get_ticks(void);

// Ring buffer
static KlogEntry g_ring[KLOG_RING_SIZE];
static uint32_t  g_head  = 0;
static uint32_t  g_count = 0;
static uint64_t  g_total = 0;
static KlogLevel g_min_level = KLOG_INFO;

// Minimal string helpers (no libc)
static void klog_strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void klog_u64_to_dec(uint64_t val, char *buf, size_t bufsz) {
    if (bufsz == 0) return;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[20];
    int i = 0;
    while (val && i < 20) { tmp[i++] = '0' + (char)(val % 10); val /= 10; }
    size_t j = 0;
    while (i > 0 && j < bufsz - 1) { buf[j++] = tmp[--i]; }
    buf[j] = '\0';
}

const char *klog_level_name(KlogLevel level) {
    switch (level) {
        case KLOG_FATAL: return "FATAL";
        case KLOG_ERROR: return "ERROR";
        case KLOG_WARN:  return "WARN ";
        case KLOG_INFO:  return "INFO ";
        case KLOG_DEBUG: return "DEBUG";
        case KLOG_TRACE: return "TRACE";
        default:         return "?????";
    }
}

// Short 3-letter severity for serial output
static const char *klog_level_short(KlogLevel level) {
    switch (level) {
        case KLOG_FATAL: return "FTL";
        case KLOG_ERROR: return "ERR";
        case KLOG_WARN:  return "WRN";
        case KLOG_INFO:  return "INF";
        case KLOG_DEBUG: return "DBG";
        case KLOG_TRACE: return "TRC";
        default:         return "???";
    }
}

void klog_init(void) {
    for (uint32_t i = 0; i < KLOG_RING_SIZE; i++) {
        g_ring[i].timestamp = 0;
        g_ring[i].level = KLOG_TRACE;
        g_ring[i].tag[0] = '\0';
        g_ring[i].message[0] = '\0';
    }
    g_head = 0;
    g_count = 0;
    g_total = 0;
    g_min_level = KLOG_INFO;
    serial_print("[klog] Logging system initialized\n");
}

void klog(KlogLevel level, const char *tag, const char *msg) {
    uint64_t now = kernel_get_ticks();

    // Always store in ring buffer regardless of filter
    KlogEntry *entry = &g_ring[g_head];
    entry->timestamp = now;
    entry->level = level;
    klog_strncpy(entry->tag, tag ? tag : "?", KLOG_TAG_MAX);
    klog_strncpy(entry->message, msg ? msg : "", KLOG_MSG_MAX);

    g_head = (g_head + 1) % KLOG_RING_SIZE;
    if (g_count < KLOG_RING_SIZE) g_count++;
    g_total++;

    // Serial output if above minimum severity
    if (level <= g_min_level) {
        char tickbuf[20];
        klog_u64_to_dec(now, tickbuf, sizeof(tickbuf));
        serial_print("[");
        serial_print(tickbuf);
        serial_print("][");
        serial_print(klog_level_short(level));
        serial_print("][");
        serial_print(entry->tag);
        serial_print("] ");
        serial_print(entry->message);
        serial_print("\n");
    }
}

void klog_set_level(KlogLevel min_level) {
    g_min_level = min_level;
}

KlogLevel klog_get_level(void) {
    return g_min_level;
}

uint32_t klog_count(void) {
    return g_count;
}

uint64_t klog_total(void) {
    return g_total;
}

void klog_clear(void) {
    g_head = 0;
    g_count = 0;
    serial_print("[klog] Buffer cleared\n");
}

// Called from CLI — prints to both VGA and serial
extern void cli_printf(const char *fmt, ...);

void klog_print_recent(uint32_t count) {
    if (count == 0 || count > g_count) count = g_count;
    if (count == 0) {
        cli_printf("  (no log entries)\n");
        return;
    }

    cli_printf("  %-8s %-5s %-10s %s\n", "Ticks", "Level", "Tag", "Message");
    cli_printf("  %-8s %-5s %-10s %s\n", "--------", "-----", "----------", "--------");

    for (uint32_t i = 0; i < count; i++) {
        // Walk backwards from head (newest first)
        uint32_t idx = (g_head + KLOG_RING_SIZE - 1 - i) % KLOG_RING_SIZE;
        KlogEntry *e = &g_ring[idx];

        // Format tick as decimal
        char tickbuf[20];
        klog_u64_to_dec(e->timestamp, tickbuf, sizeof(tickbuf));

        cli_printf("  %-8s %-5s %-10s %s\n",
            tickbuf, klog_level_name(e->level), e->tag, e->message);
    }

    char totbuf[20];
    klog_u64_to_dec(g_total, totbuf, sizeof(totbuf));
    cli_printf("  --- %u of %s total entries ---\n", count, totbuf);
}
