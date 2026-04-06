// VernisOS Phase 14: Deny Audit Log
// Ring buffer recording permission denials for admin review.
#include "auditlog.h"
#include "cli.h"

extern void cli_printf(const char *fmt, ...);
extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);

static AuditEntry g_log[AUDIT_LOG_SIZE];
static uint32_t g_head = 0;       // Next write position
static uint32_t g_count = 0;      // Current entries in buffer
static uint32_t g_total = 0;      // Lifetime total denials

static void str_copy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void auditlog_init(void) {
    g_head = 0;
    g_count = 0;
    g_total = 0;
    for (int i = 0; i < AUDIT_LOG_SIZE; i++) {
        g_log[i].timestamp = 0;
        g_log[i].username[0] = '\0';
        g_log[i].command[0] = '\0';
        g_log[i].privilege = 0;
        g_log[i].required = 0;
    }
    serial_print("[audit] Audit log initialized\n");
}

void auditlog_record(uint64_t timestamp, const char *username,
                     const char *command, uint8_t privilege, uint8_t required) {
    AuditEntry *e = &g_log[g_head];
    e->timestamp = timestamp;
    str_copy_n(e->username, username, AUDIT_USER_MAX);
    str_copy_n(e->command, command, AUDIT_CMD_MAX);
    e->privilege = privilege;
    e->required = required;

    g_head = (g_head + 1) % AUDIT_LOG_SIZE;
    if (g_count < AUDIT_LOG_SIZE) g_count++;
    g_total++;

    serial_print("[audit] DENY: user=");
    serial_print(username);
    serial_print(" cmd=");
    serial_print(command);
    serial_print(" priv=");
    serial_print_dec(privilege);
    serial_print(" req=");
    serial_print_dec(required);
    serial_print("\n");
}

uint32_t auditlog_total_count(void) { return g_total; }
uint32_t auditlog_current_count(void) { return g_count; }

static const char *priv_name(uint8_t p) {
    if (p == 0)   return "root";
    if (p <= 50)  return "admin";
    if (p <= 100) return "user";
    return "?";
}

void auditlog_print_all(void) {
    if (g_count == 0) {
        cli_printf("No denial events recorded.\n");
        return;
    }

    cli_printf("Deny Audit Log (%d entries, %d total lifetime)\n", (int)g_count, (int)g_total);
    cli_printf("%-10s  %-10s  %-20s  %-6s  %-8s\n",
               "Ticks", "User", "Command", "Priv", "Required");
    cli_printf("---------- ---------- -------------------- ------ --------\n");

    // Print newest first
    for (uint32_t i = 0; i < g_count; i++) {
        uint32_t idx;
        if (g_head >= i + 1)
            idx = g_head - i - 1;
        else
            idx = AUDIT_LOG_SIZE - (i + 1 - g_head);

        AuditEntry *e = &g_log[idx];

        // Print tick count as decimal
        char tbuf[20];
        {
            uint64_t v = e->timestamp;
            if (v == 0) { tbuf[0] = '0'; tbuf[1] = '\0'; }
            else {
                char tmp[20]; int n = 0;
                while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
                for (int j = 0; j < n; j++) tbuf[j] = tmp[n - 1 - j];
                tbuf[n] = '\0';
            }
        }

        cli_printf("%-10s  %-10s  %-20s  %-6s  %-8s\n",
                   tbuf, e->username, e->command,
                   priv_name(e->privilege), priv_name(e->required));
    }
}
