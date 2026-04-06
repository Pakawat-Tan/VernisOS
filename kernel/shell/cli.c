

// --- TCP CLI command implementations (inlined, single-file) ---
#include "tcp.h"

// --- Utility: String Functions (must be before TCP CLI commands) ---
static int cli_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int cli_streq(const char *a, const char *b) {
    return cli_strcmp(a, b) == 0;
}

#include "cli.h"


// Minimal atoi for kernel CLI
static int simple_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

// Forward declaration for cli_parse_ip to fix implicit declaration error
int cli_parse_ip(const char *s, unsigned *a, unsigned *b, unsigned *c, unsigned *d);

// TCP CLI command handlers (must match typedefs from cli.h)
static int cli_cmd_tcphandshake(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 4) {
        cli_printf("Usage: tcphandshake <connect|listen> <ip> <port>\n");
        return 1;
    }
    uint32_t ip = 0;
    uint16_t port = (uint16_t)simple_atoi(cmd->argv[3]);
    unsigned a, b, c, d;
    if (cli_parse_ip(cmd->argv[2], &a, &b, &c, &d) != 4) {
        cli_printf("Invalid IP format\n");
        return 1;
    }
    ip = (a << 24) | (b << 16) | (c << 8) | d;
    if (cli_streq(cmd->argv[1], "connect")) {
        int sock = tcp_connect(ip, port);
        cli_printf("tcp_connect returned %d\n", sock);
    } else if (cli_streq(cmd->argv[1], "listen")) {
        int sock = tcp_listen(port);
        cli_printf("tcp_listen returned %d\n", sock);
    } else {
        cli_printf("Unknown subcommand\n");
        return 1;
    }
    return 0;
}

// Non-static definition for cli_parse_ip (must be visible to linker)
int cli_parse_ip(const char *s, unsigned *a, unsigned *b, unsigned *c, unsigned *d) {
    // Parse IPv4 dotted decimal: a.b.c.d
    unsigned vals[4] = {0, 0, 0, 0};
    int seg = 0;
    const char *p = s;
    while (*p && seg < 4) {
        if (*p >= '0' && *p <= '9') {
            vals[seg] = vals[seg] * 10 + (*p - '0');
        } else if (*p == '.') {
            seg++;
            if (seg > 3) break;
        } else {
            return 0;
        }
        p++;
    }
    if (seg != 3) return 0;
    if (a) *a = vals[0];
    if (b) *b = vals[1];
    if (c) *c = vals[2];
    if (d) *d = vals[3];
    return 4;
}

static int cli_cmd_tcpstat(CliSession *session, const ParsedCommand *cmd) {
    cli_printf("\nTCP Sockets:\n");
    cli_printf("ID  Local IP      LPort  Remote IP     RPort  State\n");
    extern TcpControlBlock g_tcbs[TCP_MAX_SOCKETS];
    for (int i = 0; i < TCP_MAX_SOCKETS; ++i) {
        TcpControlBlock *tcb = &g_tcbs[i];
        if (tcb->state == TCP_CLOSED) continue;
        cli_printf("%2d  %3u.%3u.%3u.%3u  %5u  %3u.%3u.%3u.%3u  %5u  ",
            i,
            (tcb->local_ip >> 24) & 0xFF, (tcb->local_ip >> 16) & 0xFF,
            (tcb->local_ip >> 8) & 0xFF, (tcb->local_ip) & 0xFF,
            tcb->local_port,
            (tcb->remote_ip >> 24) & 0xFF, (tcb->remote_ip >> 16) & 0xFF,
            (tcb->remote_ip >> 8) & 0xFF, (tcb->remote_ip) & 0xFF,
            tcb->remote_port);
        const char *state_str = "?";
        switch (tcb->state) {
            case TCP_CLOSED: state_str = "CLOSED"; break;
            case TCP_LISTEN: state_str = "LISTEN"; break;
            case TCP_SYN_SENT: state_str = "SYN_SENT"; break;
            case TCP_SYN_RECEIVED: state_str = "SYN_RECV"; break;
            case TCP_ESTABLISHED: state_str = "ESTABLISHED"; break;
            case TCP_FIN_WAIT_1: state_str = "FIN_WAIT_1"; break;
            case TCP_FIN_WAIT_2: state_str = "FIN_WAIT_2"; break;
            case TCP_CLOSE_WAIT: state_str = "CLOSE_WAIT"; break;
            case TCP_CLOSING: state_str = "CLOSING"; break;
            case TCP_LAST_ACK: state_str = "LAST_ACK"; break;
            case TCP_TIME_WAIT: state_str = "TIME_WAIT"; break;
        }
        cli_printf("%s\n", state_str);
    }
    return 0;
}

#include "cli.h"
#include "ai_bridge.h"
#include "scheduler_base.h"
#include "policy_enforce.h"
#include "userdb.h"
#include "selftest.h"
#include "auditlog.h"
#include "klog.h"
#include "ipc.h"
#include "dylib.h"
#include "vfs.h"
#include "bcache.h"
#include <stddef.h>
#include <stdarg.h>

// Forward declarations — VGA output + cursor (provided by kernel_x86.c / kernel_x64.c)
extern void vga_print(const char *s);
extern void vga_print_hex(uint32_t val);
extern void vga_print_dec(uint32_t val);
extern void vga_set_pos(size_t row, size_t col);
extern void vga_get_pos(size_t *row, size_t *col);
extern void vga_clear_to_eol(size_t row, size_t col);
extern void vga_set_cursor(size_t row, size_t col);
extern void vga_enable_cursor(void);
extern void vga_clear_screen(void);
extern int  keyboard_read_char(char *out);
extern uint32_t kernel_get_ticks(void);
extern uint32_t kernel_get_timer_hz(void);
extern uint32_t kernel_is_gui_mode(void);
// Deferred kernel work (AI engine tick/feed) — safe in main thread context
extern void kernel_idle_work(void);
// System power control
extern void system_shutdown(void);
extern void system_restart(void);
// Phase 19: ELF exec function pointer (set by kernel)
extern int (*g_elf_exec_fn)(const char *path);
// Phase 22: PCI + Network exports
extern int  kernel_pci_count(void);
extern void kernel_pci_get(int idx, uint16_t *vendor, uint16_t *device,
                           uint8_t *cls, uint8_t *sub, uint8_t *bus, uint8_t *slot);
extern int  kernel_ahci_available(void);
extern int  kernel_ahci_ports(void);
extern uint32_t kernel_ahci_pi(void);
extern uint32_t kernel_ahci_version(void);
extern int  kernel_ahci_port_info(int port, uint32_t *ssts, uint32_t *sig,
                                  uint32_t *cmd, uint32_t *tfd, uint32_t *isr);
extern int  kernel_ahci_identify(int port);
extern int  kernel_ahci_identified(int port);
extern const char *kernel_ahci_model(int port);
extern int  kernel_ahci_read(int port, uint64_t lba, uint32_t sectors, uint8_t *out, uint32_t out_max);
extern int  kernel_ahci_write(int port, uint64_t lba, uint32_t sectors, const uint8_t *data, uint32_t data_len);
extern int  kernel_nvme_available(void);
extern uint32_t kernel_nvme_version(void);
extern int  kernel_nvme_identified(void);
extern const char *kernel_nvme_model(void);
extern const char *kernel_nvme_serial(void);
extern int  kernel_nvme_read(uint64_t lba, uint32_t sectors, uint8_t *out, uint32_t out_max);
extern int  kernel_nvme_write(uint64_t lba, uint32_t sectors, const uint8_t *data, uint32_t data_len);
extern int  kernel_net_available(void);
extern void kernel_net_get_mac(uint8_t *mac);
extern void kernel_net_get_ip(uint8_t *ip);
extern int  kernel_net_ping(uint8_t a, uint8_t b, uint8_t c, uint8_t d, int count);
// Serial input support (try to read from UART COM1)
static int serial_read_char(char *out) {
    // Port 0x3F8 = COM1 data register
    // Port 0x3FD = Line Status Register (LSR) bit 0 = Data Ready
    // Use register-based inb (%dx) so port > 0xFF works in both 32-bit and 64-bit mode.
    // Immediate-form inb only supports 8-bit port addresses (0x00–0xFF); using an
    // immediate > 0xFF causes the assembler to silently truncate it to the low byte,
    // resulting in reads from the wrong port.
    unsigned char lsr;
    unsigned short port_lsr  = 0x3FD;
    unsigned short port_data = 0x3F8;
    __asm__ volatile("inb %w1, %0" : "=a"(lsr)  : "Nd"(port_lsr));

    if (!(lsr & 0x01)) return 0;  // No data available

    unsigned char data;
    __asm__ volatile("inb %w1, %0" : "=a"(data) : "Nd"(port_data));
    *out = (char)data;
    return 1;
}

// Global shell instance
static CliShell *g_shell = NULL;
static uint8_t g_ps_gui_live = 0;
static uint32_t g_ps_gui_interval_ticks = 0;
static uint32_t g_ps_gui_next_tick = 0;

#define CLI_PIPE_BUF_SIZE 8192
static char g_cli_capture_buf[CLI_PIPE_BUF_SIZE];
static int g_cli_capture_len = 0;
static uint8_t g_cli_capture_active = 0;
static uint8_t g_cli_capture_echo = 1;
static char g_cli_pipe_input[CLI_PIPE_BUF_SIZE];
static int g_cli_pipe_input_len = 0;

// Called from GUI terminal key handler. Return 1 if key was consumed.
uint8_t cli_gui_ps_handle_key(uint8_t ch) {
    if (!g_ps_gui_live) return 0;

    if (ch == 'q' || ch == 'Q' || ch == 27) {
        g_ps_gui_live = 0;
        vga_clear_screen();
        cli_printf("Exited ps realtime mode.\n");
    }
    // Consume all key input while monitor is active to avoid screen artifacts.
    return 1;
}

// =============================================================================
// Built-in Commands Table
// =============================================================================

static int cli_cmd_help(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_clear(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_info(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_exit(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_whoami(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_ps(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_echo(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_shutdown(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_restart(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_ai(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_policy(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_login(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_su(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_logout(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_users(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_test(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_auditlog(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_log(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_ls(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_cat(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_write(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_append(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_rm(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_mkdir(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_exec(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_date(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_uptime(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_lspci(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_ahci(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_nvme(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_ping(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_kill(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_grep(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_wc(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_usockbind(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_usocksend(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_usockrecv(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_usockclose(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_dlopen(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_dlsym(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_dlcall(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_dlclose(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_dllist(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_chmod(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_chown(CliSession *session, const ParsedCommand *cmd);
static int cli_cmd_sync(CliSession *session, const ParsedCommand *cmd);
// static int simple_atoi(const char *s); // removed duplicate declaration
static uint64_t simple_atou64(const char *s);

// --- TCP CLI command implementations now inlined above ---
// cli.c — CLI / Terminal System Implementation
// Phase 7: Shell, Command Parsing, User Sessions
// Phase 13: Policy enforcement, login/su/logout commands
// Phase 14: Self-test, audit log commands
// Phase 16: Kernel log viewer command

// Only keep the wrapper at the top of the file

// Other existing functions and code...
// Only keep the wrapper at the top of the file

static const CliBuiltinCommand BUILTIN_COMMANDS[] = {
    { "help",     "Show help message",      cli_cmd_help,     CLI_PRIV_USER },
    { "clear",    "Clear the screen",       cli_cmd_clear,    CLI_PRIV_USER },
    { "info",     "Show system info",       cli_cmd_info,     CLI_PRIV_USER },
    { "exit",     "Exit shell",             cli_cmd_exit,     CLI_PRIV_USER },
    { "whoami",   "Show current user",      cli_cmd_whoami,   CLI_PRIV_USER },
    { "ps",       "Realtime process list",  cli_cmd_ps,       CLI_PRIV_USER },
    { "echo",     "Print text",             cli_cmd_echo,     CLI_PRIV_USER },
    { "shutdown", "Shutdown the system",    cli_cmd_shutdown, CLI_PRIV_ROOT },
    { "restart",  "Restart the system",     cli_cmd_restart,  CLI_PRIV_ROOT },
    { "ai",       "Query AI engine",        cli_cmd_ai,       CLI_PRIV_ADMIN },
    { "policy",   "Show/reload policy",     cli_cmd_policy,   CLI_PRIV_ROOT },
    { "login",    "Switch user session",    cli_cmd_login,    CLI_PRIV_USER },
    { "su",       "Run command as root",    cli_cmd_su,       CLI_PRIV_USER },
    { "logout",   "Return to default user", cli_cmd_logout,   CLI_PRIV_USER },
    { "users",    "List system users",      cli_cmd_users,    CLI_PRIV_ADMIN },
    { "test",     "Run kernel self-tests",  cli_cmd_test,     CLI_PRIV_ROOT },
    { "auditlog", "Show deny audit log",    cli_cmd_auditlog, CLI_PRIV_ADMIN },
    { "log",      "View kernel log",        cli_cmd_log,      CLI_PRIV_ADMIN },
    { "ls",       "List directory",         cli_cmd_ls,       CLI_PRIV_USER  },
    { "cat",      "Read file contents",     cli_cmd_cat,      CLI_PRIV_USER  },
    { "write",    "Write/create file",      cli_cmd_write,    CLI_PRIV_USER  },
    { "append",   "Append to file",         cli_cmd_append,   CLI_PRIV_USER  },
    { "rm",       "Remove file",            cli_cmd_rm,       CLI_PRIV_ADMIN },
    { "mkdir",    "Create directory",       cli_cmd_mkdir,    CLI_PRIV_USER  },
    { "exec",     "Run ELF program",        cli_cmd_exec,     CLI_PRIV_USER  },
    { "date",     "Show current date/time", cli_cmd_date,     CLI_PRIV_USER  },
    { "uptime",   "Show system uptime",     cli_cmd_uptime,   CLI_PRIV_USER  },
    { "lspci",    "List PCI devices",       cli_cmd_lspci,    CLI_PRIV_USER  },
    { "ahci",     "Show AHCI controller",   cli_cmd_ahci,     CLI_PRIV_USER  },
    { "nvme",     "Show NVMe controller",   cli_cmd_nvme,     CLI_PRIV_USER  },
    { "ping",     "Ping a host (ICMP)",     cli_cmd_ping,     CLI_PRIV_USER  },
    { "kill",     "Send signal to process", cli_cmd_kill,     CLI_PRIV_USER  },
    { "grep",     "Filter lines by pattern", cli_cmd_grep,    CLI_PRIV_USER  },
    { "wc",       "Count lines/words/bytes", cli_cmd_wc,      CLI_PRIV_USER  },
    { "usockbind", "Bind local unix socket", cli_cmd_usockbind, CLI_PRIV_ADMIN },
    { "usocksend", "Send to unix socket",    cli_cmd_usocksend, CLI_PRIV_USER  },
    { "usockrecv", "Recv from unix socket",  cli_cmd_usockrecv, CLI_PRIV_USER  },
    { "usockclose","Close unix socket",      cli_cmd_usockclose, CLI_PRIV_ADMIN },
    { "dlopen",   "Load shared library",     cli_cmd_dlopen,    CLI_PRIV_ADMIN },
    { "dlsym",    "Resolve symbol",          cli_cmd_dlsym,     CLI_PRIV_USER  },
    { "dlcall",   "Call resolved symbol",    cli_cmd_dlcall,    CLI_PRIV_USER  },
    { "dlclose",  "Unload shared library",   cli_cmd_dlclose,   CLI_PRIV_ADMIN },
    { "dllist",   "List shared libraries",   cli_cmd_dllist,    CLI_PRIV_USER  },

    { "chmod",    "Change file permissions",  cli_cmd_chmod,    CLI_PRIV_USER  },
    { "chown",    "Change file owner/group",  cli_cmd_chown,    CLI_PRIV_ROOT  },
    { "sync",     "Flush block cache",        cli_cmd_sync,     CLI_PRIV_USER  },
    { "tcphandshake", "Test TCP handshake",    cli_cmd_tcphandshake, CLI_PRIV_USER },
    { "tcpstat",  "Show TCP sockets",         cli_cmd_tcpstat,  CLI_PRIV_USER  },
};

// TCP status command implementation now inlined above
#define BUILTIN_COMMANDS_COUNT (sizeof(BUILTIN_COMMANDS) / sizeof(BUILTIN_COMMANDS[0]))

// =============================================================================
// Utility: String Functions
// =============================================================================


static int cli_str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!needle[0]) return 1;
    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (needle[j] && haystack[i + j] && haystack[i + j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

static void cli_strcpy(char *dst, const char *src, size_t max) {
    for (size_t i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[max - 1] = '\0';
}

static void cli_memset(void *ptr, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)ptr;
    while (n--) *p++ = val;
}

static void cli_capture_begin(uint8_t echo) {
    g_cli_capture_active = 1;
    g_cli_capture_echo = echo;
    g_cli_capture_len = 0;
    g_cli_capture_buf[0] = '\0';
}

static int cli_capture_end(char *dst, int max) {
    int n = g_cli_capture_len;
    if (dst && max > 0) {
        int copy = (n < max - 1) ? n : (max - 1);
        for (int i = 0; i < copy; i++) dst[i] = g_cli_capture_buf[i];
        dst[copy] = '\0';
        n = copy;
    }
    g_cli_capture_active = 0;
    g_cli_capture_echo = 1;
    return n;
}

static void cli_pipe_set_input(const char *buf, int len) {
    if (!buf || len <= 0) {
        g_cli_pipe_input_len = 0;
        g_cli_pipe_input[0] = '\0';
        return;
    }
    if (len >= CLI_PIPE_BUF_SIZE) len = CLI_PIPE_BUF_SIZE - 1;
    for (int i = 0; i < len; i++) g_cli_pipe_input[i] = buf[i];
    g_cli_pipe_input[len] = '\0';
    g_cli_pipe_input_len = len;
}

static void cli_pipe_clear_input(void) {
    g_cli_pipe_input_len = 0;
    g_cli_pipe_input[0] = '\0';
}

static int cli_pipe_has_input(void) {
    return g_cli_pipe_input_len > 0;
}

static int cli_pipe_copy_input(uint8_t *dst, int max) {
    if (!dst || max <= 0 || !cli_pipe_has_input()) return -1;
    int copy = (g_cli_pipe_input_len < max) ? g_cli_pipe_input_len : max;
    for (int i = 0; i < copy; i++) dst[i] = (uint8_t)g_cli_pipe_input[i];
    return copy;
}

static void cli_write_raw(const char *s) {
    if (!s) return;
    while (*s) {
        char c = *s++;
        if (g_cli_capture_active && g_cli_capture_len < CLI_PIPE_BUF_SIZE - 1) {
            g_cli_capture_buf[g_cli_capture_len++] = c;
            g_cli_capture_buf[g_cli_capture_len] = '\0';
        }
        if (!g_cli_capture_active || g_cli_capture_echo) {
            char out[2] = { c, '\0' };
            vga_print(out);
        }
    }
}

static void cli_write_uint_dec(uint32_t val) {
    char tmp[16];
    int n = 0;
    if (val == 0) tmp[n++] = '0';
    while (val > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (n > 0) {
        char out[2] = { tmp[--n], '\0' };
        cli_write_raw(out);
    }
}

static void cli_write_int_dec(int val) {
    if (val < 0) {
        cli_write_raw("-");
        val = -val;
    }
    cli_write_uint_dec((uint32_t)val);
}

static void cli_write_hex32(uint32_t val) {
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        uint8_t d = (uint8_t)(val & 0x0F);
        hex[i] = (char)(d < 10 ? ('0' + d) : ('A' + d - 10));
        val >>= 4;
    }
    hex[8] = '\0';
    cli_write_raw(hex);
}

// =============================================================================
// Output Functions
// =============================================================================

// Helper: print single character
static void cli_putchar(char c) {
    char buf[2] = {c, '\0'};
    cli_write_raw(buf);
}

// Print string s left/right aligned in a field of `width` chars
static void cli_print_field_s(const char *s, int width, int left) {
    int len = 0;
    if (!s) s = "";
    while (s[len]) len++;
    if (len > width) len = width;          // truncate
    if (!left) for (int i = len; i < width; i++) cli_putchar(' ');
    for (int i = 0; i < len; i++) cli_putchar(s[i]);
    if (left)  for (int i = len; i < width; i++) cli_putchar(' ');
}

// Print integer left/right aligned in a field of `width` chars
static void cli_print_field_d(int val, int width, int left) {
    char tmp[12]; int tlen = 0, neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { tmp[tlen++] = '0'; }
    while (val > 0) { tmp[tlen++] = (char)('0' + val % 10); val /= 10; }
    if (neg) tmp[tlen++] = '-';
    int digs = tlen;
    if (!left) for (int i = digs; i < width; i++) cli_putchar(' ');
    for (int i = tlen - 1; i >= 0; i--) cli_putchar(tmp[i]);
    if (left)  for (int i = digs; i < width; i++) cli_putchar(' ');
}

void cli_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p == '%' && *(p + 1)) {
            p++;

            // Parse flags
            int left = 0;
            if (*p == '-') { left = 1; p++; }
            // skip other flags
            while (*p == '+' || *p == ' ') p++;

            // Parse width
            int width = 0;
            while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }

            switch (*p) {
                case 's': {
                    const char *str = va_arg(args, const char *);
                    if (width > 0) cli_print_field_s(str, width, left);
                    else { if (str) cli_write_raw(str); }
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    if (width > 0) cli_print_field_d(val, width, left);
                    else cli_write_int_dec(val);
                    break;
                }
                case 'x': {
                    uint32_t val = va_arg(args, uint32_t);
                    cli_write_hex32(val);
                    break;
                }
                case '%':
                    cli_putchar('%');
                    break;
            }
        } else if (*p == '\n') {
            cli_putchar('\n');
        } else if (*p == '\t') {
            cli_write_raw("    ");
        } else {
            cli_putchar(*p);
        }
    }
    
    va_end(args);
}

void cli_print_prompt(CliSession *session) {
    if (!session) return;
    cli_printf("%s@vernisOS:%s> ", session->username, "/");
}

static int cli_command_pipe_safe(const char *name) {
    if (!name) return 0;
    if (cli_streq(name, "ps") || cli_streq(name, "shutdown") ||
        cli_streq(name, "restart") || cli_streq(name, "clear") ||
        cli_streq(name, "login") || cli_streq(name, "su") ||
        cli_streq(name, "logout") || cli_streq(name, "exec")) {
        return 0;
    }
    return 1;
}

static int cli_read_text_source(const ParsedCommand *cmd, int arg_index,
                                uint8_t *buf, int max, const char *usage) {
    if (!buf || max <= 0) return -1;

    if (cmd->argc > arg_index) {
        int n = kfs_read_file(cmd->argv[arg_index], buf, (size_t)(max - 1));
        if (n < 0) {
            cli_printf("%s: %s: not found\n", cmd->argv[0], cmd->argv[arg_index]);
            return -1;
        }
        buf[n] = '\0';
        return n;
    }

    if (cli_pipe_has_input()) {
        int n = cli_pipe_copy_input(buf, max - 1);
        if (n < 0) return -1;
        buf[n] = '\0';
        return n;
    }

    if (usage) cli_printf("Usage: %s\n", usage);
    return -1;
}

// =============================================================================
// Session Management
// =============================================================================

CliShell* cli_shell_init(void) {
    if (g_shell) return g_shell;

    static CliShell shell;
    cli_memset(&shell, 0, sizeof(CliShell));
    shell.next_session_id = 1;
    shell.is_interactive = true;

    g_shell = &shell;
    return g_shell;
}

CliSession* cli_session_create(CliShell *shell, const char *username, CliPrivilegeLevel priv) {
    if (!shell || shell->session_count >= CLI_MAX_SESSIONS) {
        vga_print("[cli] ERROR: session pool exhausted\n");
        return NULL;
    }

    static CliSession pool[CLI_MAX_SESSIONS];
    
    CliSession *session = &pool[shell->session_count];
    cli_memset(session, 0, sizeof(CliSession));
    
    session->session_id = shell->next_session_id++;
    session->uid = shell->session_count;
    session->privilege = priv;
    session->is_active = true;
    session->commands_executed = 0;
    
    // Copy username
    if (username) {
        cli_strcpy(session->username, username, sizeof(session->username));
    } else {
        cli_strcpy(session->username, "user", sizeof(session->username));
    }

    shell->sessions[shell->session_count] = session;
    shell->session_count++;

    return session;
}

void cli_session_destroy(CliShell *shell, CliSession *session) {
    if (!shell || !session) return;
    session->is_active = false;
    vga_print("[cli] session destroyed\n");
}

CliSession* cli_shell_get_active_session(CliShell *shell) {
    if (!shell || shell->session_count == 0) return NULL;
    return shell->sessions[0];  // Return first active session
}

// =============================================================================
// Command Parsing
// =============================================================================

int cli_parse_command(const char *input, ParsedCommand *out_cmd) {
    if (!input || !out_cmd) return CLI_ERR_SYNTAX;

    cli_memset(out_cmd, 0, sizeof(ParsedCommand));
    
    // Copy command line
    cli_strcpy(out_cmd->command, input, CLI_MAX_INPUT_LEN);
    
    // Simple tokenizer: split by spaces
    char *p = out_cmd->command;
    out_cmd->argc = 0;
    
    while (*p && out_cmd->argc < CLI_MAX_ARGS) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        // Check for special characters
        if (*p == '&') {
            out_cmd->background = true;
            p++;
            continue;
        }
        if (*p == '<') {
            p++;
            while (*p == ' ') p++;
            out_cmd->input_redirect = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
            continue;
        }
        if (*p == '>') {
            p++;
            while (*p == ' ') p++;
            out_cmd->output_redirect = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
            continue;
        }
        if (*p == '|') {
            p++;
            while (*p == ' ') p++;
            out_cmd->pipe_to = p;
            while (*p) p++;
            break;
        }
        
        // Collect token
        out_cmd->argv[out_cmd->argc++] = p;
        
        // Find end of token
        while (*p && *p != ' ' && *p != '\t' && *p != '&' && *p != '<' && *p != '>') 
            p++;
        
        if (*p) *p++ = '\0';
    }

    if (out_cmd->argc == 0) return CLI_ERR_SYNTAX;
    return CLI_OK;
}

// =============================================================================
// Built-in Commands
// =============================================================================

const CliBuiltinCommand* cli_find_builtin(const char *name) {
    if (!name) return NULL;
    
    for (size_t i = 0; i < BUILTIN_COMMANDS_COUNT; i++) {
        if (cli_strcmp(BUILTIN_COMMANDS[i].name, name) == 0) {
            return &BUILTIN_COMMANDS[i];
        }
    }
    return NULL;
}

static int cli_cmd_help(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;
    cli_printf("\n=== VernisOS Shell Help ===\n");
    cli_printf("Built-in commands:\n\n");
    
    for (size_t i = 0; i < BUILTIN_COMMANDS_COUNT; i++) {
        cli_printf("  %-10s - %s\n", 
                   BUILTIN_COMMANDS[i].name,
                   BUILTIN_COMMANDS[i].description);
    }
    cli_printf("\n");
    return CLI_OK;
}

static int cli_cmd_clear(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    vga_clear_screen();
    return CLI_OK;
}

static int cli_cmd_info(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;

    // Uptime
    uint32_t ticks   = kernel_get_ticks();
    uint32_t hz      = kernel_get_timer_hz();
    if (hz == 0) hz = 100;
    uint32_t seconds = ticks / hz;
    uint32_t minutes = seconds / 60;
    uint32_t hours   = minutes / 60;
    seconds %= 60;
    minutes %= 60;

    // Process count from live scheduler
    void *sched = get_kernel_scheduler();
    uint32_t nproc = sched ? (uint32_t)scheduler_get_process_count(sched) : 0;

    // AI bridge status string
    const char *ai_str;
    switch (ai_bridge_status()) {
        case AI_STATUS_READY:   ai_str = "ready (in-kernel Rust)"; break;
        case AI_STATUS_BUSY:    ai_str = "busy";                   break;
        default:                ai_str = "offline";                break;
    }

    // Architecture label (compile-time)
#ifdef __x86_64__
    const char *arch = "x86-64 (64-bit)";
#else
    const char *arch = "x86 (32-bit)";
#endif

    cli_printf("\n");
    cli_printf("  +---------------------------------+\n");
    cli_printf("  |     VernisOS System Info        |\n");
    cli_printf("  +---------------------------------+\n");
    cli_printf("  OS          : VernisOS\n");
    cli_printf("  Version     : 0.1.0\n");
    cli_printf("  Arch        : %s\n", arch);
    cli_printf("  Uptime      : %d:%d:%d (hh:mm:ss)\n",
               (int)hours, (int)minutes, (int)seconds);
    cli_printf("  Heap        : 2 MB\n");
    cli_printf("  Processes   : %d\n", (int)nproc);
    cli_printf("  AI Bridge   : %s\n", ai_str);
    cli_printf("  User        : %s\n", session->username);
    cli_printf("  +---------------------------------+\n");
    cli_printf("\n");
    return CLI_OK;
}

static int cli_cmd_exit(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;
    cli_printf("Goodbye %s!\n", session->username);
    session->is_active = false;
    return CLI_OK;
}

static int cli_cmd_whoami(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;
    const char *role = "root";
    if (session->privilege == CLI_PRIV_ADMIN) role = "admin";
    else if (session->privilege == CLI_PRIV_USER) role = "user";
    cli_printf("%s (privilege: %s)\n", session->username, role);
    return CLI_OK;
}

// Helper: write decimal integer into buf, return chars written
static int ps_itoa(char *buf, size_t max, int val) {
    if (max < 2) return 0;
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[12]; int n = 0;
    if (val == 0) { tmp[n++] = '0'; }
    else { while (val) { tmp[n++] = (char)('0' + val % 10); val /= 10; } }
    int pos = 0;
    if (neg && pos < (int)max - 1) buf[pos++] = '-';
    for (int j = n - 1; j >= 0 && pos < (int)max - 1; j--) buf[pos++] = tmp[j];
    buf[pos] = '\0';
    return pos;
}

// Helper: concatenate src onto buf at position pos
static int ps_cat(char *buf, size_t max, int pos, const char *src) {
    while (*src && pos < (int)max - 1) buf[pos++] = *src++;
    buf[pos] = '\0';
    return pos;
}

static int ps_render_table(void *sched) {
    if (!sched) {
        cli_printf("scheduler not ready\n");
        return CLI_ERR_EXEC_FAILED;
    }

    // Fetch PID list
    size_t pids[32];
    size_t count = scheduler_get_pid_list(sched, pids, 32);
    size_t total_known = scheduler_get_process_count(sched);

    // Fallback: some builds can report empty PID list through FFI even when
    // scheduler state is populated. Probe PID space directly in that case.
    if (count == 0 && total_known > 0) {
        size_t found = 0;
        size_t probe_limit = (total_known * 16);
        if (probe_limit < 64) probe_limit = 64;
        if (probe_limit > 4096) probe_limit = 4096;

        for (size_t pid = 1; pid <= probe_limit && found < 32; pid++) {
            PsRow probe;
            if (scheduler_get_ps_row(sched, pid, &probe)) {
                pids[found++] = pid;
            }
        }
        count = found;
    }

    static const char *state_name[] = {
        "New", "Standby", "Running", "Waiting",
        "Suspended", "Terminated", "Zombie"
    };
    static const char *type_name[] = { "Kernel", "System", "User" };

    // Header
    cli_printf("%-5s %-6s %-8s %-10s %-7s %-6s %-8s %-10s %s\n",
               "PID", "PPID", "PRIORITY", "STATE", "TYPE",
               "CPU%", "MEMORY", "UPTIME", "COMMAND");
    cli_printf("===== ====== ======== ========== ======= ====== ======== ========== ================\n");

    for (size_t i = 0; i < count; i++) {
        PsRow row;
        if (!scheduler_get_ps_row(sched, pids[i], &row)) continue;

        const char *st   = (row.state < 7) ? state_name[row.state] : "?";
        const char *type = (row.ptype < 3) ? type_name[row.ptype]  : "?";
        const char *name = (const char *)row.command;
        if (!name[0]) name = "(none)";

        // CPU%: (cpu_time_ms / uptime_ms) * 100
        // Use integer math: pct_x10 = cpu_time_ms * 1000 / uptime_ms
        char cpu_buf[8]; cpu_buf[0] = '\0';
        uint64_t uptime_ms = row.uptime_secs * 1000;
        if (uptime_ms > 0) {
            uint64_t pct_x10 = row.cpu_time_ms * 1000 / uptime_ms;
            int whole = (int)(pct_x10 / 10);
            int frac  = (int)(pct_x10 % 10);
            int p = ps_itoa(cpu_buf, sizeof(cpu_buf), whole);
            p = ps_cat(cpu_buf, sizeof(cpu_buf), p, ".");
            char d[2] = { (char)('0' + frac), '\0' };
            ps_cat(cpu_buf, sizeof(cpu_buf), p, d);
        } else {
            ps_cat(cpu_buf, sizeof(cpu_buf), 0, "0.0");
        }

        // MEM: auto-scale B → KB → MB → GB
        char mem_buf[12]; mem_buf[0] = '\0';
        if (row.mem_rss == 0) {
            ps_cat(mem_buf, sizeof(mem_buf), 0, "0B");
        } else if (row.mem_rss < 1024) {
            int p = ps_itoa(mem_buf, sizeof(mem_buf), (int)row.mem_rss);
            ps_cat(mem_buf, sizeof(mem_buf), p, "B");
        } else if (row.mem_rss < 1048576) {
            int kb = (int)(row.mem_rss / 1024);
            int frac = (int)(row.mem_rss % 1024 * 10 / 1024);
            int p = ps_itoa(mem_buf, sizeof(mem_buf), kb);
            if (frac > 0) {
                p = ps_cat(mem_buf, sizeof(mem_buf), p, ".");
                char d[2] = { (char)('0' + frac), '\0' };
                p = ps_cat(mem_buf, sizeof(mem_buf), p, d);
            }
            ps_cat(mem_buf, sizeof(mem_buf), p, "KB");
        } else if (row.mem_rss < (size_t)1073741824) {
            int mb = (int)(row.mem_rss / 1048576);
            int frac = (int)(row.mem_rss % 1048576 * 10 / 1048576);
            int p = ps_itoa(mem_buf, sizeof(mem_buf), mb);
            if (frac > 0) {
                p = ps_cat(mem_buf, sizeof(mem_buf), p, ".");
                char d[2] = { (char)('0' + frac), '\0' };
                p = ps_cat(mem_buf, sizeof(mem_buf), p, d);
            }
            ps_cat(mem_buf, sizeof(mem_buf), p, "MB");
        } else {
            int gb = (int)(row.mem_rss / 1073741824);
            int p = ps_itoa(mem_buf, sizeof(mem_buf), gb);
            ps_cat(mem_buf, sizeof(mem_buf), p, "GB");
        }

        // Format uptime: Xs / XmXs / XhXm
        char up_buf[12]; up_buf[0] = '\0';
        uint64_t up = row.uptime_secs;
        if (up < 60) {
            int p = ps_itoa(up_buf, sizeof(up_buf), (int)up);
            ps_cat(up_buf, sizeof(up_buf), p, "s");
        } else if (up < 3600) {
            int p = ps_itoa(up_buf, sizeof(up_buf), (int)(up / 60));
            p = ps_cat(up_buf, sizeof(up_buf), p, "m");
            char sec[4]; ps_itoa(sec, sizeof(sec), (int)(up % 60));
            p = ps_cat(up_buf, sizeof(up_buf), p, sec);
            ps_cat(up_buf, sizeof(up_buf), p, "s");
        } else {
            int p = ps_itoa(up_buf, sizeof(up_buf), (int)(up / 3600));
            p = ps_cat(up_buf, sizeof(up_buf), p, "h");
            char min[4]; ps_itoa(min, sizeof(min), (int)(up % 3600 / 60));
            p = ps_cat(up_buf, sizeof(up_buf), p, min);
            ps_cat(up_buf, sizeof(up_buf), p, "m");
        }

        cli_printf("%-5d %-6d %-8d %-10s %-7s %-6s %-8s %-10s %s\n",
            (int)row.pid, (int)row.ppid, (int)row.priority,
            st, type, cpu_buf, mem_buf, up_buf, name);
    }

    cli_printf("===== ====== ======== ========== ======= ====== ======== ========== ================\n");
    cli_printf("  Total: %d shown / %d known\n", (int)count, (int)total_known);
    return CLI_OK;
}

// Called from GUI main loop tick (Rust) to refresh ps monitor without blocking UI.
void cli_gui_tick(void) {
    if (!g_ps_gui_live) return;
    if (!kernel_is_gui_mode()) {
        g_ps_gui_live = 0;
        return;
    }

    uint32_t now = kernel_get_ticks();
    if (g_ps_gui_next_tick != 0 && (now - g_ps_gui_next_tick) > 0x80000000u) {
        return;
    }
    if (g_ps_gui_next_tick != 0 && now < g_ps_gui_next_tick) {
        return;
    }

    void *sched = get_kernel_scheduler();
    if (!sched) return;

    vga_clear_screen();
    ps_render_table(sched);
    cli_printf("\n[ps realtime gui] press q or Esc to stop\n");

    if (g_ps_gui_interval_ticks == 0) g_ps_gui_interval_ticks = 1;
    g_ps_gui_next_tick = now + g_ps_gui_interval_ticks;
}

static int cli_cmd_ps(CliSession *session, const ParsedCommand *cmd) {
    (void)session;

    // Get live scheduler pointer
    void *sched = get_kernel_scheduler();
    if (!sched) {
        cli_printf("scheduler not ready\n");
        return CLI_ERR_EXEC_FAILED;
    }

    if (cmd->argc != 1) {
        cli_printf("Usage: ps\n");
        cli_printf("  Realtime process monitor (press 'q' to quit)\n");
        return CLI_OK;
    }

    // In GUI terminal, use non-blocking monitor mode updated from GUI ticks.
    if (kernel_is_gui_mode()) {
        uint32_t hz = kernel_get_timer_hz();
        if (hz == 0) hz = 100;
        g_ps_gui_interval_ticks = (500u * hz + 999u) / 1000u;
        if (g_ps_gui_interval_ticks == 0) g_ps_gui_interval_ticks = 1;
        g_ps_gui_next_tick = 0; // force immediate first draw
        g_ps_gui_live = 1;
        cli_printf("[ps] GUI realtime started\n");
        return CLI_OK;
    }

    {
        const uint32_t interval_ms = 500;
        const uint32_t auto_exit_ms = 30000;
        uint32_t hz = kernel_get_timer_hz();
        if (hz == 0) hz = 100;
        uint32_t interval_ticks = (interval_ms * hz + 999) / 1000;
        uint32_t auto_exit_ticks = (auto_exit_ms * hz + 999) / 1000;
        if (interval_ticks == 0) interval_ticks = 1;
        if (auto_exit_ticks == 0) auto_exit_ticks = hz;

        // Drain any stale key pressed before entering ps (especially Enter).
        {
            char drain;
            while (keyboard_read_char(&drain) || serial_read_char(&drain)) {
                (void)drain;
            }
        }

        uint32_t started = kernel_get_ticks();

        while (1) {
            vga_clear_screen();
            ps_render_table(sched);
            cli_printf("\n[ps realtime] press q / Esc to quit\n");

            uint32_t start = kernel_get_ticks();
            while ((kernel_get_ticks() - start) < interval_ticks) {
                char c = '\0';
                if (keyboard_read_char(&c) || serial_read_char(&c)) {
                    if (c == 'q' || c == 'Q' || (unsigned char)c == 27) {
                        vga_clear_screen();
                        cli_printf("Exited ps realtime mode.\n");
                        return CLI_OK;
                    }
                }

                if ((kernel_get_ticks() - started) >= auto_exit_ticks) {
                    vga_clear_screen();
                    cli_printf("Exited ps realtime mode (auto timeout).\n");
                    return CLI_OK;
                }

                kernel_idle_work();
                __asm__ volatile("hlt");
            }
        }
    }
}

static int cli_cmd_echo(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc == 1 && cli_pipe_has_input()) {
        cli_printf("%s", g_cli_pipe_input);
        if (g_cli_pipe_input_len == 0 || g_cli_pipe_input[g_cli_pipe_input_len - 1] != '\n')
            cli_printf("\n");
        return CLI_OK;
    }
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) cli_putchar(' ');
        cli_printf("%s", cmd->argv[i]);
    }
    cli_printf("\n");
    return CLI_OK;
}

static int cli_cmd_shutdown(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;
    cli_printf("Shutting down VernisOS...\n");
    cli_printf("Goodbye %s!\n", session->username);
    session->is_active = false;
    system_shutdown();
    return CLI_OK;  // unreachable
}

static int cli_cmd_restart(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;
    cli_printf("Restarting VernisOS...\n");
    cli_printf("See you soon %s!\n", session->username);
    session->is_active = false;
    system_restart();
    return CLI_OK;  // unreachable
}

static int cli_cmd_ai(CliSession *session, const ParsedCommand *cmd) {
    (void)session;

    // ai status — check bridge + in-kernel engine
    if (cmd->argc == 2) {
        const char *sub = cmd->argv[1];
        if (sub[0] == 's' && sub[1] == 't' && sub[2] == 'a') {
            int st = ai_bridge_status();
            switch (st) {
                case AI_STATUS_OFFLINE: cli_printf("AI bridge: offline\n"); break;
                case AI_STATUS_READY:   cli_printf("AI bridge: ready\n"); break;
                case AI_STATUS_BUSY:    cli_printf("AI bridge: busy\n"); break;
                default:                cli_printf("AI bridge: error\n"); break;
            }
            // In-kernel AI engine stats
            cli_printf("AI engine: in-kernel (Rust)\n");
            cli_printf("  events:    ");
            uint64_t ec = ai_kernel_engine_event_count();
            // Print u64 as decimal
            if (ec == 0) { cli_printf("0"); }
            else {
                char nbuf[20]; int ni = 0; char tmp[20]; uint64_t v = ec;
                while (v) { tmp[ni++] = (char)('0' + (v % 10)); v /= 10; }
                for (int j = ni - 1; j >= 0; j--) { nbuf[ni - 1 - j] = tmp[j]; }
                nbuf[ni] = '\0';
                cli_printf(nbuf);
            }
            cli_printf("\n");
            cli_printf("  anomalies: ");
            {
                uint32_t av = ai_kernel_engine_anomaly_count();
                char ab[12]; int ai2 = 0; char at[12];
                if (av == 0) { at[ai2++] = '0'; }
                else { while (av) { at[ai2++] = (char)('0' + av % 10); av /= 10; } }
                for (int j = ai2 - 1; j >= 0; j--) ab[ai2 - 1 - j] = at[j];
                ab[ai2] = '\0';
                cli_printf(ab);
            }
            cli_printf("\n");
            cli_printf("  processes: ");
            {
                uint32_t pv = ai_kernel_engine_active_procs();
                char pb[12]; int pi = 0; char pt[12];
                if (pv == 0) { pt[pi++] = '0'; }
                else { while (pv) { pt[pi++] = (char)('0' + pv % 10); pv /= 10; } }
                for (int j = pi - 1; j >= 0; j--) pb[pi - 1 - j] = pt[j];
                pb[pi] = '\0';
                cli_printf(pb);
            }
            cli_printf("\n");
            return CLI_OK;
        }
    }

    // ai <word1> [word2] ... — join into query string
    if (cmd->argc < 2) {
        cli_printf("Usage: ai <query>  |  ai status\n");
        return CLI_OK;
    }

    // Build query from remaining args
    char query[AI_MSG_MAX];
    size_t pos = 0;
    for (int i = 1; i < cmd->argc && pos < AI_MSG_MAX - 2; i++) {
        if (i > 1) query[pos++] = ' ';
        for (const char *p = cmd->argv[i]; *p && pos < AI_MSG_MAX - 1; p++)
            query[pos++] = *p;
    }
    query[pos] = '\0';

    cli_printf("AI> %s\n", query);

    char resp[AI_MSG_MAX];
    int n = ai_query_sync(query, resp, AI_MSG_MAX);
    if (n < 0) {
        cli_printf("[ai] No response (bridge offline or timeout)\n");
    } else {
        cli_printf("AI: %s\n", resp);
    }
    return CLI_OK;
}

// =============================================================================
// Command: policy — show/reload policy config
// =============================================================================

static int cli_cmd_policy(CliSession *session, const ParsedCommand *cmd) {
    (void)session;

    if (cmd->argc >= 2) {
        // "policy show"
        if (cmd->argv[1][0] == 's' && cmd->argv[1][1] == 'h') {
            cli_printf("Policy System (Phase 12)\n");
            cli_printf("  Version : ");
            {
                char vb[8]; uint16_t v = ai_kernel_engine_policy_version();
                ps_itoa(vb, sizeof(vb), (int)v);
                cli_printf(vb);
            }
            cli_printf("\n");
            cli_printf("  Source  : os.img sector 4096 (VPOL binary)\n");
            cli_printf("  Engine  : in-kernel Rust AI\n");

            // Bridge status
            AiBridgeStatus bst = ai_bridge_status();
            cli_printf("  Bridge  : ");
            switch (bst) {
                case AI_STATUS_OFFLINE: cli_printf("OFFLINE"); break;
                case AI_STATUS_READY:   cli_printf("READY");   break;
                case AI_STATUS_BUSY:    cli_printf("BUSY");    break;
                case AI_STATUS_ERROR:   cli_printf("ERROR");   break;
            }
            cli_printf("\n");

            // Engine stats
            cli_printf("  Events  : ");
            { char b[20]; uint64_t ec = ai_kernel_engine_event_count();
              ps_itoa(b, sizeof(b), (int)(uint32_t)ec); cli_printf(b); }
            cli_printf("\n");
            cli_printf("  Anomaly : ");
            { char b[12]; ps_itoa(b, sizeof(b), (int)ai_kernel_engine_anomaly_count()); cli_printf(b); }
            cli_printf("\n");
            return CLI_OK;
        }

        // "policy reload"
        if (cmd->argv[1][0] == 'r' && cmd->argv[1][1] == 'e') {
            cli_printf("Reloading policy from disk...\n");
            policy_load_from_disk();
            cli_printf("Policy version: ");
            { char vb[8]; ps_itoa(vb, sizeof(vb), (int)ai_kernel_engine_policy_version()); cli_printf(vb); }
            cli_printf("\n");
            return CLI_OK;
        }
    }

    cli_printf("Usage: policy show   - show current policy info\n");
    cli_printf("       policy reload - reload policy from disk\n");
    return CLI_OK;
}

// =============================================================================
// Phase 13: Authentication Commands
// =============================================================================

// Helper: read password with echo disabled (shows '*' characters)
static int cli_read_password(char *buf, size_t max_len) {
    if (!buf || max_len < 2) return 0;
    size_t len = 0;

    while (len < max_len - 1) {
        char c = '\0';
        if (!keyboard_read_char(&c)) {
            if (!serial_read_char(&c)) {
                kernel_idle_work();
                continue;
            }
        }

        if (c == '\n' || c == '\r') {
            cli_printf("\n");
            break;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                cli_printf("\b \b");
            }
            continue;
        }
        if (c < 0x20) continue;

        buf[len++] = c;
        cli_printf("*");
    }

    buf[len] = '\0';
    return (int)len;
}

static int cli_cmd_login(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) {
        cli_printf("Usage: login <username>\n");
        return CLI_OK;
    }

    const char *username = cmd->argv[1];
    const UserRecord *user = userdb_find_user(username);
    if (!user) {
        cli_printf("Unknown user: %s\n", username);
        return CLI_OK;
    }

    if (!(user->flags & USER_FLAG_ACTIVE)) {
        cli_printf("Account disabled\n");
        return CLI_OK;
    }

    if (user->flags & USER_FLAG_LOCKED) {
        cli_printf("Account locked\n");
        return CLI_OK;
    }

    // Check if password required
    if (!(user->flags & USER_FLAG_NO_PASSWORD)) {
        cli_printf("Password: ");
        char password[64];
        int pw_len = cli_read_password(password, sizeof(password));
        if (pw_len <= 0) {
            cli_printf("Login cancelled\n");
            return CLI_OK;
        }

        int result = userdb_authenticate(username, password);
        // Clear password from memory
        cli_memset(password, 0, sizeof(password));

        if (result < 0) {
            cli_printf("Authentication failed\n");
            ai_kernel_engine_feed(AI_EVT_DENY, username,
                                  (uint64_t)kernel_get_ticks());
            return CLI_OK;
        }
    }

    // Switch session to new user
    cli_strcpy(session->username, username, sizeof(session->username));
    session->privilege = (CliPrivilegeLevel)user->privilege;
    int uid = userdb_find_uid(username);
    if (uid >= 0) session->uid = (uint32_t)uid;
    cli_printf("Logged in as %s\n", username);
    return CLI_OK;
}

static int cli_cmd_su(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) {
        cli_printf("Usage: su <command> [args...]\n");
        cli_printf("  Run a command with root privileges\n");
        return CLI_OK;
    }

    // Already root? Just run the command
    if (session->privilege == CLI_PRIV_ROOT) {
        // Build sub-command
        ParsedCommand sub_cmd;
        cli_memset(&sub_cmd, 0, sizeof(sub_cmd));
        // Copy args starting from argv[1]
        char cmd_buf[CLI_MAX_INPUT_LEN];
        size_t pos = 0;
        for (int i = 1; i < cmd->argc && pos < CLI_MAX_INPUT_LEN - 1; i++) {
            if (i > 1 && pos < CLI_MAX_INPUT_LEN - 1) cmd_buf[pos++] = ' ';
            const char *a = cmd->argv[i];
            while (*a && pos < CLI_MAX_INPUT_LEN - 1) cmd_buf[pos++] = *a++;
        }
        cmd_buf[pos] = '\0';

        int parse_result = cli_parse_command(cmd_buf, &sub_cmd);
        if (parse_result != CLI_OK) return parse_result;

        uint32_t pid = 0;
        return cli_execute_command(session, &sub_cmd, &pid);
    }

    // Need root password
    cli_printf("Password: ");
    char password[64];
    int pw_len = cli_read_password(password, sizeof(password));
    if (pw_len <= 0) {
        cli_printf("Cancelled\n");
        return CLI_OK;
    }

    int result = userdb_authenticate("root", password);
    cli_memset(password, 0, sizeof(password));

    if (result < 0) {
        cli_printf("Authentication failed\n");
        ai_kernel_engine_feed(AI_EVT_DENY, "su",
                              (uint64_t)kernel_get_ticks());
        return CLI_OK;
    }

    // Temporarily elevate to root
    CliPrivilegeLevel saved_priv = session->privilege;
    uint32_t saved_uid = session->uid;
    char saved_name[32];
    cli_strcpy(saved_name, session->username, sizeof(saved_name));

    session->privilege = CLI_PRIV_ROOT;
    session->uid = 0;
    cli_strcpy(session->username, "root", sizeof(session->username));

    // Build and execute sub-command
    ParsedCommand sub_cmd;
    cli_memset(&sub_cmd, 0, sizeof(sub_cmd));
    char cmd_buf[CLI_MAX_INPUT_LEN];
    size_t pos = 0;
    for (int i = 1; i < cmd->argc && pos < CLI_MAX_INPUT_LEN - 1; i++) {
        if (i > 1 && pos < CLI_MAX_INPUT_LEN - 1) cmd_buf[pos++] = ' ';
        const char *a = cmd->argv[i];
        while (*a && pos < CLI_MAX_INPUT_LEN - 1) cmd_buf[pos++] = *a++;
    }
    cmd_buf[pos] = '\0';

    int parse_result = cli_parse_command(cmd_buf, &sub_cmd);
    int exec_result = CLI_OK;
    if (parse_result == CLI_OK) {
        uint32_t pid = 0;
        exec_result = cli_execute_command(session, &sub_cmd, &pid);
    }

    // Restore original privilege
    session->privilege = saved_priv;
    session->uid = saved_uid;
    cli_strcpy(session->username, saved_name, sizeof(session->username));

    return exec_result;
}

static int cli_cmd_logout(CliSession *session, const ParsedCommand *cmd) {
    (void)cmd;

    // If already root with no-password, just inform
    if (session->privilege == CLI_PRIV_ROOT) {
        const UserRecord *root_user = userdb_find_user("root");
        if (root_user && (root_user->flags & USER_FLAG_NO_PASSWORD)) {
            cli_printf("Already at default root session\n");
            return CLI_OK;
        }
    }

    // Reset to default user (root for bare-metal, or first sandbox user)
    cli_strcpy(session->username, "root", sizeof(session->username));
    session->privilege = CLI_PRIV_ROOT;
    cli_printf("Logged out — returned to root\n");
    return CLI_OK;
}

static int cli_cmd_users(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;

    uint8_t count = userdb_user_count();
    if (count == 0) {
        cli_printf("No users loaded\n");
        return CLI_OK;
    }

    cli_printf("\n  %-12s %-8s %-8s\n", "USERNAME", "PRIV", "STATUS");
    cli_printf("  %-12s %-8s %-8s\n", "--------", "----", "------");

    for (uint8_t i = 0; i < count; i++) {
        const UserRecord *u = userdb_get_user(i);
        if (!u) continue;

        const char *priv_str = "user";
        if (u->privilege == 0) priv_str = "root";
        else if (u->privilege == 50) priv_str = "admin";

        const char *status = "active";
        if (u->flags & USER_FLAG_LOCKED) status = "locked";
        else if (!(u->flags & USER_FLAG_ACTIVE)) status = "disabled";

        cli_printf("  %-12s %-8s %-8s\n", u->username, priv_str, status);
    }
    cli_printf("\n");
    return CLI_OK;
}

// =============================================================================
// test — Run kernel self-tests (Phase 14)
// =============================================================================
static int cli_cmd_test(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    selftest_run_all();
    return CLI_OK;
}

// =============================================================================
// auditlog — Show deny audit log (Phase 14)
// =============================================================================
static int cli_cmd_auditlog(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    auditlog_print_all();
    return CLI_OK;
}

// Phase 16: Kernel log viewer
static int cli_cmd_log(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc >= 2) {
        // "log clear"
        if (cmd->argv[1][0] == 'c') {
            klog_clear();
            cli_printf("Kernel log buffer cleared.\n");
            return CLI_OK;
        }
        // "log level <N>" — set min level (0=FATAL..5=TRACE)
        if (cmd->argv[1][0] == 'l' && cmd->argc >= 3) {
            int lvl = cmd->argv[2][0] - '0';
            if (lvl >= 0 && lvl <= 5) {
                klog_set_level((KlogLevel)lvl);
                cli_printf("Log level set to %s\n", klog_level_name((KlogLevel)lvl));
            } else {
                cli_printf("Usage: log level <0-5> (0=FATAL..5=TRACE)\n");
            }
            return CLI_OK;
        }
        // "log <N>" — show last N entries
        int n = 0;
        const char *p = cmd->argv[1];
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        if (n > 0) {
            cli_printf("=== Kernel Log (last %d) ===\n", n);
            klog_print_recent((uint32_t)n);
            return CLI_OK;
        }
    }
    // Default: show all recent
    cli_printf("=== Kernel Log ===\n");
    klog_print_recent(0);
    return CLI_OK;
}

// =============================================================================
// Command Execution
// =============================================================================

static int cli_execute_single_command(CliSession *session, const ParsedCommand *cmd, uint32_t *out_pid) {
    if (!session || !cmd || cmd->argc == 0) {
        if (out_pid) *out_pid = 0;
        return CLI_ERR_SYNTAX;
    }

    // Check if privilege is sufficient
    const CliBuiltinCommand *builtin = cli_find_builtin(cmd->argv[0]);
    
    if (builtin) {
        // Check static privilege from command table
        if (session->privilege > builtin->min_privilege) {
            cli_printf("Permission denied: '%s' requires higher privilege\n",
                      cmd->argv[0]);
            ai_kernel_engine_feed(AI_EVT_DENY, cmd->argv[0],
                                  (uint64_t)kernel_get_ticks());
            auditlog_record((uint64_t)kernel_get_ticks(), session->username,
                            cmd->argv[0], (uint8_t)session->privilege,
                            (uint8_t)builtin->min_privilege);
            klog(KLOG_WARN, "cli", cmd->argv[0]);
            return CLI_ERR_NO_PERMISSION;
        }
        
        // Check dynamic policy access rules
        if (!policy_check_command(cmd->argv[0], (uint8_t)session->privilege)) {
            cli_printf("Policy denied: '%s' not allowed for %s\n",
                      cmd->argv[0], session->username);
            ai_kernel_engine_feed(AI_EVT_DENY, cmd->argv[0],
                                  (uint64_t)kernel_get_ticks());
            auditlog_record((uint64_t)kernel_get_ticks(), session->username,
                            cmd->argv[0], (uint8_t)session->privilege, 0);
            klog(KLOG_WARN, "policy", cmd->argv[0]);
            return CLI_ERR_NO_PERMISSION;
        }

        session->commands_executed++;
        klog(KLOG_DEBUG, "cli", cmd->argv[0]);
        return builtin->handler(session, cmd);
    }

    // Unknown command
    cli_printf("Unknown command: %s\n", cmd->argv[0]);
    return CLI_ERR_UNKNOWN_CMD;
}

int cli_execute_command(CliSession *session, const ParsedCommand *cmd, uint32_t *out_pid) {
    if (!session || !cmd || cmd->argc == 0) {
        if (out_pid) *out_pid = 0;
        return CLI_ERR_SYNTAX;
    }

    if (cmd->pipe_to && cmd->pipe_to[0]) {
        if (!cli_command_pipe_safe(cmd->argv[0])) {
            cli_printf("pipe: command '%s' cannot be piped\n", cmd->argv[0]);
            return CLI_ERR_EXEC_FAILED;
        }

        ParsedCommand next_cmd;
        int parse_result = cli_parse_command(cmd->pipe_to, &next_cmd);
        if (parse_result != CLI_OK) {
            cli_printf("pipe: invalid command after '|'\n");
            return parse_result;
        }

        cli_capture_begin(0);
        int left_rc = cli_execute_single_command(session, cmd, 0);
        int cap_len = cli_capture_end(g_cli_capture_buf, CLI_PIPE_BUF_SIZE);

        if (left_rc != CLI_OK) {
            if (cap_len > 0) cli_printf("%s", g_cli_capture_buf);
            return left_rc;
        }

        cli_pipe_set_input(g_cli_capture_buf, cap_len);
        int rc = cli_execute_command(session, &next_cmd, out_pid);
        cli_pipe_clear_input();
        return rc;
    }

    return cli_execute_single_command(session, cmd, out_pid);
}

// =============================================================================
// Line Processing
// =============================================================================

int cli_process_line(CliShell *shell, CliSession *session, const char *line) {
    if (!shell || !session || !line) return CLI_ERR_SYNTAX;

    // Stop GUI ps monitor with: q (then Enter)
    if (g_ps_gui_live) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if ((p[0] == 'q' || p[0] == 'Q') && p[1] == '\0') {
            g_ps_gui_live = 0;
            cli_printf("Exited ps realtime mode.\n");
            return CLI_OK;
        }
    }

    // Parse command
    ParsedCommand cmd;
    int parse_result = cli_parse_command(line, &cmd);
    if (parse_result != CLI_OK) {
        cli_printf("Syntax error\n");
        return parse_result;
    }

    // Execute
    uint32_t pid = 0;
    return cli_execute_command(session, &cmd, &pid);
}

// =============================================================================
// Line Reading — full line editor with cursor, history, arrow keys
// =============================================================================

// Redraw the input line from (srow, scol), set hardware cursor at edit position
static void redraw_line(size_t srow, size_t scol,
                        const char *line, size_t len, size_t cursor) {
    vga_set_pos(srow, scol);
    for (size_t i = 0; i < len; i++) cli_putchar(line[i]);
    vga_clear_to_eol(srow, scol + len);
    vga_set_cursor(srow, scol + cursor);
}

int cli_readline(char *buf, size_t max_len) {
    if (!buf || max_len < 2) return 0;

    char   line[CLI_MAX_INPUT_LEN];
    size_t len     = 0;   // current string length
    size_t cur     = 0;   // cursor position within string
    int    hist_off = 0;  // 0 = not browsing history

    cli_memset(line, 0, sizeof(line));

    // Record where the prompt ended so we can redraw in-place
    size_t srow, scol;
    vga_get_pos(&srow, &scol);
    vga_set_cursor(srow, scol);

    while (1) {
        char c = '\0';
        if (!keyboard_read_char(&c)) {
            if (!serial_read_char(&c)) {
                kernel_idle_work();   // process deferred AI work
                __asm__ volatile("hlt");
                continue;
            }
        }

        unsigned char uc = (unsigned char)c;

        // ---- Enter ----
        if (uc == '\n' || uc == '\r') {
            vga_set_pos(srow, scol + len);
            vga_clear_to_eol(srow, scol + len);
            cli_putchar('\n');
            cli_strcpy(buf, line, max_len);
            return (int)len;
        }

        // ---- Backspace ----
        if (uc == '\b' || uc == 0x7F) {
            if (cur > 0) {
                for (size_t i = cur - 1; i < len - 1; i++) line[i] = line[i + 1];
                len--;
                cur--;
                line[len] = '\0';
                redraw_line(srow, scol, line, len, cur);
            }
            continue;
        }

        // ---- Delete ----
        if (uc == KEY_DEL) {
            if (cur < len) {
                for (size_t i = cur; i < len - 1; i++) line[i] = line[i + 1];
                len--;
                line[len] = '\0';
                redraw_line(srow, scol, line, len, cur);
            }
            continue;
        }

        // ---- Left arrow ----
        if (uc == KEY_LEFT) {
            if (cur > 0) { cur--; vga_set_cursor(srow, scol + cur); }
            continue;
        }

        // ---- Right arrow ----
        if (uc == KEY_RIGHT) {
            if (cur < len) { cur++; vga_set_cursor(srow, scol + cur); }
            continue;
        }

        // ---- Home ----
        if (uc == KEY_HOME) {
            cur = 0;
            vga_set_cursor(srow, scol);
            continue;
        }

        // ---- End ----
        if (uc == KEY_END) {
            cur = len;
            vga_set_cursor(srow, scol + cur);
            continue;
        }

        // ---- Up arrow — history previous ----
        if (uc == KEY_UP && g_shell) {
            size_t hist_count = g_shell->history_index < (size_t)CLI_HISTORY_SIZE
                                ? g_shell->history_index
                                : (size_t)CLI_HISTORY_SIZE;
            if ((size_t)hist_off < hist_count) {
                hist_off++;
                size_t base = g_shell->history_index + (size_t)CLI_HISTORY_SIZE * 2;
                size_t idx  = (base - (size_t)hist_off) % (size_t)CLI_HISTORY_SIZE;
                const char *h = g_shell->history[idx];
                len = 0;
                while (h[len] && len < max_len - 1) { line[len] = h[len]; len++; }
                line[len] = '\0';
                cur = len;
                redraw_line(srow, scol, line, len, cur);
            }
            continue;
        }

        // ---- Down arrow — history next / clear ----
        if (uc == KEY_DOWN && g_shell) {
            if (hist_off > 1) {
                hist_off--;
                size_t base = g_shell->history_index + (size_t)CLI_HISTORY_SIZE * 2;
                size_t idx  = (base - (size_t)hist_off) % (size_t)CLI_HISTORY_SIZE;
                const char *h = g_shell->history[idx];
                len = 0;
                while (h[len] && len < max_len - 1) { line[len] = h[len]; len++; }
                line[len] = '\0';
                cur = len;
                redraw_line(srow, scol, line, len, cur);
            } else if (hist_off == 1) {
                hist_off = 0;
                len = 0; cur = 0; line[0] = '\0';
                redraw_line(srow, scol, line, 0, 0);
            }
            continue;
        }

        // ---- Printable character — insert at cursor ----
        // Only accept ASCII 32-127, reject anything outside this range
        // (including special key codes 0x80-0x86 which may appear as negative chars)
        if (uc >= 32 && uc < 127 && len < max_len - 1) {
            for (size_t i = len; i > cur; i--) line[i] = line[i - 1];
            line[cur] = (char)uc;  // Use uc to ensure ASCII range
            len++;
            cur++;
            line[len] = '\0';
            redraw_line(srow, scol, line, len, cur);
        }
    }

    /* unreachable */
    buf[0] = '\0';
    return 0;
}

// =============================================================================
// History
// =============================================================================

static void cli_history_push(CliShell *shell, const char *line) {
    if (!shell || !line || !line[0]) return;
    size_t slot = shell->history_index % (size_t)CLI_HISTORY_SIZE;
    cli_strcpy(shell->history[slot], line, CLI_MAX_INPUT_LEN);
    shell->history_index++;
}

// =============================================================================
// Filesystem Commands (VernisFS)
// =============================================================================

// Helper: join argv[start..argc-1] into buf with spaces
static int cli_join_args(const ParsedCommand *cmd, int start,
                         char *buf, int max) {
    int pos = 0;
    for (int i = start; i < cmd->argc && pos < max - 1; i++) {
        if (i > start && pos < max - 1) buf[pos++] = ' ';
        for (int j = 0; cmd->argv[i][j] && pos < max - 1; j++)
            buf[pos++] = cmd->argv[i][j];
    }
    buf[pos] = '\0';
    return pos;
}

static int cli_cmd_ls(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    int long_fmt = 0;
    const char *path = "/";

    for (int i = 1; i < cmd->argc; i++) {
        if (cli_streq(cmd->argv[i], "-l"))
            long_fmt = 1;
        else
            path = cmd->argv[i];
    }

    char entries[VFS_MAX_FILES][VFS_MAX_FILENAME];
    int count = kfs_list_dir(path, entries, VFS_MAX_FILES);
    if (count < 0) {
        cli_printf("ls: VernisFS not ready\n");
        return 1;
    }

    cli_printf("%s\n", path);
    for (int i = 0; i < count; i++) {
        const VfsFileEntry *e = kfs_find_file(entries[i]);
        // Basename: everything after last '/'
        const char *name = entries[i];
        for (const char *p = entries[i]; *p; p++)
            if (*p == '/') name = p + 1;
        if (long_fmt && e) {
            uint16_t m = e->mode;
            char perm[11];
            perm[0] = (e->type == VFS_TYPE_DIRECTORY) ? 'd' : '-';
            perm[1] = (m & VFS_PERM_UR) ? 'r' : '-';
            perm[2] = (m & VFS_PERM_UW) ? 'w' : '-';
            perm[3] = (m & VFS_PERM_UX) ? 'x' : '-';
            perm[4] = (m & VFS_PERM_GR) ? 'r' : '-';
            perm[5] = (m & VFS_PERM_GW) ? 'w' : '-';
            perm[6] = (m & VFS_PERM_GX) ? 'x' : '-';
            perm[7] = (m & VFS_PERM_OR) ? 'r' : '-';
            perm[8] = (m & VFS_PERM_OW) ? 'w' : '-';
            perm[9] = (m & VFS_PERM_OX) ? 'x' : '-';
            perm[10] = '\0';
            cli_printf("  %s %3u %3u %6u %s\n",
                       perm, e->uid, e->gid, e->size, name);
        } else if (e && e->type == VFS_TYPE_DIRECTORY)
            cli_printf("  [dir] %s\n", name);
        else if (e)
            cli_printf("  [file] %-20s %u B\n", name, e->size);
    }
    if (count == 0) cli_printf("  (empty)\n");
    return CLI_OK;
}

static int cli_cmd_cat(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2 && !cli_pipe_has_input()) { cli_printf("Usage: cat <path>\n"); return 1; }

    if (cmd->argc < 2 && cli_pipe_has_input()) {
        cli_printf("%s", g_cli_pipe_input);
        if (g_cli_pipe_input_len == 0 || g_cli_pipe_input[g_cli_pipe_input_len - 1] != '\n')
            cli_printf("\n");
        return CLI_OK;
    }

    if (kfs_check_perm(cmd->argv[1], (uint16_t)session->uid, 'r') < 0) {
        cli_printf("cat: %s: permission denied\n", cmd->argv[1]);
        return 1;
    }

    uint8_t buf[512];
    int n = kfs_read_file(cmd->argv[1], buf, sizeof(buf) - 1);
    if (n < 0) {
        cli_printf("cat: %s: not found\n", cmd->argv[1]);
        return 1;
    }
    buf[n] = '\0';
    cli_printf("%s\n", (char *)buf);
    return CLI_OK;
}

static int cli_cmd_write(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) {
        cli_printf("Usage: write <path> <content...>\n");
        return 1;
    }
    // Check write permission if file exists
    if (kfs_find_file(cmd->argv[1]) &&
        kfs_check_perm(cmd->argv[1], (uint16_t)session->uid, 'w') < 0) {
        cli_printf("write: %s: permission denied\n", cmd->argv[1]);
        return 1;
    }
    if (cmd->argc == 2 && cli_pipe_has_input()) {
        int n = kfs_write_file(cmd->argv[1], (const uint8_t *)g_cli_pipe_input, (size_t)g_cli_pipe_input_len);
        if (n < 0) { cli_printf("write: failed\n"); return 1; }
        cli_printf("Wrote %d bytes -> %s\n", n, cmd->argv[1]);
        return CLI_OK;
    }
    if (cmd->argc < 3) {
        cli_printf("Usage: write <path> <content...>\n");
        return 1;
    }
    char content[256];
    int len = cli_join_args(cmd, 2, content, (int)sizeof(content));
    int n = kfs_write_file(cmd->argv[1], (const uint8_t *)content, (size_t)len);
    if (n < 0) { cli_printf("write: failed\n"); return 1; }
    cli_printf("Wrote %d bytes -> %s\n", n, cmd->argv[1]);
    return CLI_OK;
}

static int cli_cmd_append(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) {
        cli_printf("Usage: append <path> <content...>\n");
        return 1;
    }
    if (kfs_find_file(cmd->argv[1]) &&
        kfs_check_perm(cmd->argv[1], (uint16_t)session->uid, 'w') < 0) {
        cli_printf("append: %s: permission denied\n", cmd->argv[1]);
        return 1;
    }
    if (cmd->argc == 2 && cli_pipe_has_input()) {
        int n = kfs_append_file(cmd->argv[1], (const uint8_t *)g_cli_pipe_input, (size_t)g_cli_pipe_input_len);
        if (n < 0) { cli_printf("append: failed\n"); return 1; }
        cli_printf("File %s now %d bytes\n", cmd->argv[1], n);
        return CLI_OK;
    }
    if (cmd->argc < 3) {
        cli_printf("Usage: append <path> <content...>\n");
        return 1;
    }
    char content[256];
    int len = cli_join_args(cmd, 2, content, (int)sizeof(content));
    int n = kfs_append_file(cmd->argv[1], (const uint8_t *)content, (size_t)len);
    if (n < 0) { cli_printf("append: failed\n"); return 1; }
    cli_printf("File %s now %d bytes\n", cmd->argv[1], n);
    return CLI_OK;
}

static int cli_cmd_rm(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) { cli_printf("Usage: rm <path>\n"); return 1; }
    if (kfs_check_perm(cmd->argv[1], (uint16_t)session->uid, 'w') < 0) {
        cli_printf("rm: %s: permission denied\n", cmd->argv[1]);
        return 1;
    }
    if (kfs_delete_file(cmd->argv[1]) < 0) {
        cli_printf("rm: %s: not found\n", cmd->argv[1]);
        return 1;
    }
    cli_printf("Removed %s\n", cmd->argv[1]);
    return CLI_OK;
}

static int cli_cmd_mkdir(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) { cli_printf("Usage: mkdir <path>\n"); return 1; }
    if (kfs_mkdir(cmd->argv[1]) < 0) {
        cli_printf("mkdir: failed (full or already exists)\n");
        return 1;
    }
    cli_printf("Created %s\n", cmd->argv[1]);
    return CLI_OK;
}

// =============================================================================
// Phase 47: chmod / chown
// =============================================================================

static uint16_t cli_parse_octal(const char *s) {
    uint16_t val = 0;
    while (*s >= '0' && *s <= '7') {
        val = (uint16_t)(val * 8 + (*s - '0'));
        s++;
    }
    return val;
}

static int cli_cmd_chmod(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 3) {
        cli_printf("Usage: chmod <mode> <path>\n");
        cli_printf("  e.g. chmod 755 /bin/hello64\n");
        return 1;
    }

    const char *mode_str = cmd->argv[1];
    const char *path = cmd->argv[2];

    // Only owner or root can chmod
    const VfsFileEntry *e = kfs_find_file(path);
    if (!e) { cli_printf("chmod: %s: not found\n", path); return 1; }
    if (session->uid != 0 && session->uid != e->uid) {
        cli_printf("chmod: permission denied\n");
        return 1;
    }

    uint16_t mode = cli_parse_octal(mode_str);
    if (mode > 0777) { cli_printf("chmod: invalid mode\n"); return 1; }

    if (kfs_chmod(path, mode) < 0) {
        cli_printf("chmod: failed\n");
        return 1;
    }
    cli_printf("Mode of %s set to %c%c%c\n", path,
               '0' + ((mode >> 6) & 7),
               '0' + ((mode >> 3) & 7),
               '0' + (mode & 7));
    return CLI_OK;
}

static int cli_cmd_chown(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 3) {
        cli_printf("Usage: chown <uid>[:<gid>] <path>\n");
        return 1;
    }
    // Only root can chown
    if (session->uid != 0) {
        cli_printf("chown: permission denied (root only)\n");
        return 1;
    }

    const char *owner_str = cmd->argv[1];
    const char *path = cmd->argv[2];

    const VfsFileEntry *e = kfs_find_file(path);
    if (!e) { cli_printf("chown: %s: not found\n", path); return 1; }

    // Parse uid[:gid]
    uint16_t new_uid = 0, new_gid = e->gid;
    const char *p = owner_str;
    while (*p >= '0' && *p <= '9') {
        new_uid = (uint16_t)(new_uid * 10 + (*p - '0'));
        p++;
    }
    if (*p == ':') {
        p++;
        new_gid = 0;
        while (*p >= '0' && *p <= '9') {
            new_gid = (uint16_t)(new_gid * 10 + (*p - '0'));
            p++;
        }
    }

    if (kfs_chown(path, new_uid, new_gid) < 0) {
        cli_printf("chown: failed\n");
        return 1;
    }
    cli_printf("Owner of %s set to %u:%u\n", path, new_uid, new_gid);
    return CLI_OK;
}

// Phase 48: sync — flush block cache to disk
static int cli_cmd_sync(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    BcacheStats before = bcache_get_stats();
    int rc = bcache_sync();
    BcacheStats after = bcache_get_stats();
    uint32_t flushed = after.writebacks - before.writebacks;
    if (rc < 0) {
        cli_printf("sync: error flushing cache\n");
        return 1;
    }
    cli_printf("sync: %u blocks written back\n", flushed);
    cli_printf("  cache stats: hits=%u misses=%u writebacks=%u evictions=%u\n",
               after.hits, after.misses, after.writebacks, after.evictions);
    return CLI_OK;
}

// exec — Load and run an ELF binary from VernisFS (Phase 19)
static int cli_cmd_exec(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) {
        cli_printf("Usage: exec <path>\n");
        cli_printf("  e.g. exec /bin/hello64\n");
        return 1;
    }
    if (kfs_check_perm(cmd->argv[1], (uint16_t)session->uid, 'x') < 0) {
        cli_printf("exec: %s: permission denied\n", cmd->argv[1]);
        return 1;
    }
    if (!g_elf_exec_fn) {
        cli_printf("exec: ELF loader not available\n");
        return 1;
    }
    cli_printf("Loading %s...\n", cmd->argv[1]);
    int ret = g_elf_exec_fn(cmd->argv[1]);
    if (ret < 0) {
        cli_printf("exec: failed to load %s\n", cmd->argv[1]);
        return 1;
    }
    cli_printf("Process started from %s\n", cmd->argv[1]);
    return CLI_OK;
}

// =============================================================================
// Phase 21: date + uptime commands
// =============================================================================

extern void kernel_rtc_read(uint8_t *hour, uint8_t *min, uint8_t *sec,
                            uint8_t *day, uint8_t *month, uint16_t *year);

static int cli_cmd_date(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    uint8_t h, m, s, day, mon;
    uint16_t year;
    kernel_rtc_read(&h, &m, &s, &day, &mon, &year);
    cli_printf("%04d-%02d-%02d %02d:%02d:%02d UTC\n",
               (int)year, (int)mon, (int)day,
               (int)h, (int)m, (int)s);
    return CLI_OK;
}

static int cli_cmd_uptime(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    uint32_t ticks = kernel_get_ticks();
    uint32_t hz    = kernel_get_timer_hz();
    if (hz == 0) hz = 100;
    uint32_t total_sec = ticks / hz;
    uint32_t days    = total_sec / 86400;
    uint32_t hours   = (total_sec % 86400) / 3600;
    uint32_t minutes = (total_sec % 3600) / 60;
    uint32_t seconds = total_sec % 60;
    if (days > 0) {
        cli_printf("up %d day%s, %02d:%02d:%02d\n",
                   (int)days, days == 1 ? "" : "s",
                   (int)hours, (int)minutes, (int)seconds);
    } else {
        cli_printf("up %02d:%02d:%02d\n",
                   (int)hours, (int)minutes, (int)seconds);
    }
    return CLI_OK;
}

// =============================================================================
// Phase 22: lspci + ping commands
// =============================================================================

// Simple PCI vendor/device/class lookup tables (expand as needed)
typedef struct { uint16_t id; const char *name; } PciIdName;
static const PciIdName pci_vendors[] = {
    {0x8086, "Intel"}, {0x10DE, "NVIDIA"}, {0x1234, "QEMU"}, {0x1022, "AMD"}, {0, NULL}
};
static const PciIdName pci_devices[] = {
    {0x100E, "82540EM Gigabit"}, {0x2922, "ICH9 SATA"}, {0x1AF4, "Virtio"}, {0, NULL}
};
static const PciIdName pci_classes[] = {
    {0x01, "Mass Storage"}, {0x02, "Network"}, {0x03, "Display"}, {0x06, "Bridge"}, {0x0C, "Serial Bus"}, {0, NULL}
};
static const char *pci_lookup(uint16_t id, const PciIdName *tbl) {
    for (int i = 0; tbl[i].name; i++) if (tbl[i].id == id) return tbl[i].name;
    return "Unknown";
}
static int cli_cmd_lspci(CliSession *session, const ParsedCommand *cmd) {
    (void)session; (void)cmd;
    int n = kernel_pci_count();
    if (n == 0) { cli_printf("No PCI devices found.\n"); return CLI_OK; }
    cli_printf("Bus  Slot  Vendor      Device      Class\n");
    cli_printf("---  ----  ----------  ----------  ---------------\n");
    for (int i = 0; i < n; i++) {
        uint16_t vendor, device;
        uint8_t cls, sub, bus, slot;
        kernel_pci_get(i, &vendor, &device, &cls, &sub, &bus, &slot);
        const char *vname = pci_lookup(vendor, pci_vendors);
        const char *dname = pci_lookup(device, pci_devices);
        const char *cname = pci_lookup(cls, pci_classes);
        cli_printf("%02x   %02x    %04x %-7s  %04x %-10s  %02x:%02x %-13s\n",
            (int)bus, (int)slot, (int)vendor, vname, (int)device, dname, (int)cls, (int)sub, cname);
    }
    cli_printf("%d device(s)\n", n);
    return CLI_OK;
}

static int cli_cmd_ahci(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (!kernel_ahci_available()) {
        cli_printf("AHCI controller not available.\n");
        return CLI_OK;
    }
    uint32_t ver = kernel_ahci_version();
    uint16_t major = (uint16_t)(ver >> 16);
    uint16_t minor = (uint16_t)(ver & 0xFFFFu);
    cli_printf("AHCI online: version %u.%u\n", (unsigned)major, (unsigned)minor);
    cli_printf("Implemented ports: %d\n", kernel_ahci_ports());
    uint32_t pi = kernel_ahci_pi();
    cli_printf("PI bitmap: 0x%08x\n", pi);

    if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "ports")) {
        cli_printf("Port  Type    SSTS      SIG       CMD       TFD\n");
        cli_printf("----  ------  --------  --------  --------  --------\n");
        for (int p = 0; p < 32; p++) {
            if (!(pi & (1u << (uint32_t)p))) continue;
            uint32_t ssts = 0, sig = 0, pcmd = 0, tfd = 0, isr = 0;
            if (kernel_ahci_port_info(p, &ssts, &sig, &pcmd, &tfd, &isr) != 0) continue;

            const char *type = "EMPTY";
            uint32_t det = ssts & 0x0Fu;
            uint32_t ipm = (ssts >> 8) & 0x0Fu;
            if (det == 3u && ipm == 1u) {
                if (sig == 0x00000101u) type = "SATA";
                else if (sig == 0xEB140101u) type = "ATAPI";
                else if (sig == 0xC33C0101u) type = "SEMB";
                else if (sig == 0x96690101u) type = "PM";
                else type = "UNK";
            } else {
                type = "NO-LINK";
            }

            cli_printf("%2d    %-6s  %08x  %08x  %08x  %08x\n",
                       p, type, ssts, sig, pcmd, tfd);
            (void)isr;
        }
    } else if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "identify")) {
        if (cmd->argc >= 3) {
            int port = 0;
            const char *s = cmd->argv[2];
            while (*s >= '0' && *s <= '9') {
                port = (port * 10) + (*s - '0');
                s++;
            }
            int rc = kernel_ahci_identify(port);
            if (rc == 0) {
                cli_printf("AHCI port %d identify OK: %s\n", port, kernel_ahci_model(port));
            } else {
                cli_printf("AHCI port %d identify failed (%d)\n", port, rc);
            }
        } else {
            uint32_t pi2 = kernel_ahci_pi();
            for (int p = 0; p < 32; p++) {
                if (!(pi2 & (1u << (uint32_t)p))) continue;
                int rc = kernel_ahci_identify(p);
                if (rc == 0) cli_printf("p%d: %s\n", p, kernel_ahci_model(p));
                else cli_printf("p%d: identify failed (%d)\n", p, rc);
            }
        }
    } else if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "model")) {
        uint32_t pi2 = kernel_ahci_pi();
        for (int p = 0; p < 32; p++) {
            if (!(pi2 & (1u << (uint32_t)p))) continue;
            if (kernel_ahci_identified(p)) cli_printf("p%d: %s\n", p, kernel_ahci_model(p));
            else cli_printf("p%d: (not identified)\n", p);
        }
    } else if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "read")) {
        if (cmd->argc < 4) {
            cli_printf("Usage: ahci read <port> <lba> [sectors]\n");
            return 1;
        }
        int port = simple_atoi(cmd->argv[2]);
        uint64_t lba = simple_atou64(cmd->argv[3]);
        uint32_t sectors = 1;
        if (cmd->argc >= 5) {
            int sv = simple_atoi(cmd->argv[4]);
            if (sv > 0) sectors = (uint32_t)sv;
        }
        if (sectors > 8) sectors = 8;

        uint8_t buf[4096];
        int rc = kernel_ahci_read(port, lba, sectors, buf, sizeof(buf));
        if (rc < 0) {
            cli_printf("AHCI read failed (%d)\n", rc);
            return 1;
        }

        cli_printf("AHCI read OK: port=%d lba=%d sectors=%d bytes=%d\n",
                   port, (int)lba, (int)sectors, rc);
        int dump = rc;
        if (dump > 128) dump = 128;
        for (int i = 0; i < dump; i += 16) {
            cli_printf("%04x: ", i);
            for (int j = 0; j < 16 && (i + j) < dump; j++) {
                cli_printf("%02x ", buf[i + j]);
            }
            cli_printf("\n");
        }
    } else if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "write")) {
        if (cmd->argc < 5) {
            cli_printf("Usage: ahci write <port> <lba> <fill-byte-hex>\n");
            return 1;
        }
        int port = simple_atoi(cmd->argv[2]);
        uint64_t lba = simple_atou64(cmd->argv[3]);
        /* parse hex fill byte */
        const char *hx = cmd->argv[4];
        uint8_t fill = 0;
        for (int d = 0; d < 2 && hx[d]; d++) {
            uint8_t nibble = 0;
            char c = hx[d];
            if (c >= '0' && c <= '9') nibble = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
            fill = (uint8_t)((fill << 4) | nibble);
        }

        uint8_t wbuf[512];
        for (int i = 0; i < 512; i++) wbuf[i] = fill;

        int rc = kernel_ahci_write(port, lba, 1, wbuf, 512);
        if (rc < 0) {
            cli_printf("AHCI write failed (%d)\n", rc);
            return 1;
        }
        cli_printf("AHCI write OK: port=%d lba=%d fill=0x%02x bytes=%d\n",
                   port, (int)lba, (int)fill, rc);
    } else if (cmd->argc >= 2) {
        cli_printf("Usage: ahci [ports|identify [port]|model|read|write]\n");
    }
    return CLI_OK;
}

static int cli_cmd_nvme(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (!kernel_nvme_available()) {
        cli_printf("NVMe controller not available.\n");
        return CLI_OK;
    }
    uint32_t ver = kernel_nvme_version();
    uint16_t major = (uint16_t)(ver >> 16);
    uint16_t minor = (uint16_t)((ver >> 8) & 0xFFu);
    uint16_t ter   = (uint16_t)(ver & 0xFFu);
    cli_printf("NVMe online: version %u.%u.%u\n",
               (unsigned)major, (unsigned)minor, (unsigned)ter);
    if (kernel_nvme_identified()) {
        cli_printf("Model:  %s\n", kernel_nvme_model());
        cli_printf("Serial: %s\n", kernel_nvme_serial());
    } else {
        cli_printf("Controller identified: no\n");
    }

    if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "read")) {
        if (cmd->argc < 3) {
            cli_printf("Usage: nvme read <lba> [sectors]\n");
            return 1;
        }
        uint64_t lba = simple_atou64(cmd->argv[2]);
        uint32_t sectors = 1;
        if (cmd->argc >= 4) {
            int sv = simple_atoi(cmd->argv[3]);
            if (sv > 0) sectors = (uint32_t)sv;
        }
        if (sectors > 8) sectors = 8;

        uint8_t buf[4096];
        int rc = kernel_nvme_read(lba, sectors, buf, sizeof(buf));
        if (rc < 0) {
            cli_printf("NVMe read failed (%d)\n", rc);
            return 1;
        }
        cli_printf("NVMe read OK: lba=%d sectors=%d bytes=%d\n",
                   (int)lba, (int)sectors, rc);
        int dump = rc;
        if (dump > 128) dump = 128;
        for (int i = 0; i < dump; i += 16) {
            cli_printf("%04x: ", i);
            for (int j = 0; j < 16 && (i + j) < dump; j++) {
                cli_printf("%02x ", buf[i + j]);
            }
            cli_printf("\n");
        }
    } else if (cmd->argc >= 2 && cli_streq(cmd->argv[1], "write")) {
        if (cmd->argc < 4) {
            cli_printf("Usage: nvme write <lba> <fill-byte-hex>\n");
            return 1;
        }
        uint64_t lba = simple_atou64(cmd->argv[2]);
        const char *hx = cmd->argv[3];
        uint8_t fill = 0;
        for (int d = 0; d < 2 && hx[d]; d++) {
            uint8_t nibble = 0;
            char c = hx[d];
            if (c >= '0' && c <= '9') nibble = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
            fill = (uint8_t)((fill << 4) | nibble);
        }

        uint8_t wbuf[512];
        for (int i = 0; i < 512; i++) wbuf[i] = fill;

        int rc = kernel_nvme_write(lba, 1, wbuf, 512);
        if (rc < 0) {
            cli_printf("NVMe write failed (%d)\n", rc);
            return 1;
        }
        cli_printf("NVMe write OK: lba=%d fill=0x%02x bytes=%d\n",
                   (int)lba, (int)fill, rc);
    } else if (cmd->argc >= 2) {
        cli_printf("Usage: nvme [read <lba> [sectors]|write <lba> <fill-hex>]\n");
    }
    return CLI_OK;
}


static uint64_t simple_atou64(const char *s) {
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint64_t)(*s - '0');
        s++;
    }
    return v;
}

static int cli_cmd_ping(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: ping <a.b.c.d> [count]\n");
        return 1;
    }
    if (!kernel_net_available()) {
        cli_printf("No network device available.\n");
        return 1;
    }
    // Parse IP address a.b.c.d
    const char *s = cmd->argv[1];
    uint8_t ip[4] = {0,0,0,0};
    int part = 0;
    for (int i = 0; s[i] && part < 4; i++) {
        if (s[i] == '.') { part++; continue; }
        if (s[i] >= '0' && s[i] <= '9')
            ip[part] = ip[part] * 10 + (s[i] - '0');
    }
    if (part != 3) {
        cli_printf("Invalid IP address: %s\n", cmd->argv[1]);
        return 1;
    }
    int count = 4;
    if (cmd->argc >= 3) {
        count = simple_atoi(cmd->argv[2]);
        if (count < 1) count = 1;
        if (count > 20) count = 20;
    }

    cli_printf("PING %d.%d.%d.%d: %d packets\n",
               (int)ip[0], (int)ip[1], (int)ip[2], (int)ip[3], count);
    int ok = kernel_net_ping(ip[0], ip[1], ip[2], ip[3], count);
    if (ok < 0) {
        if (ok == -2) cli_printf("ARP resolution failed.\n");
        else          cli_printf("Network error.\n");
        return 1;
    }
    cli_printf("%d/%d packets received\n", ok, count);
    return CLI_OK;
}

// =============================================================================
// Phase 23: kill command
// =============================================================================

static int cli_cmd_kill(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 3) {
        cli_printf("Usage: kill <pid> <signal>\n");
        cli_printf("Signals: 2=SIGINT, 9=SIGKILL, 15=SIGTERM\n");
        return 1;
    }
    
    uint32_t pid = (uint32_t)simple_atoi(cmd->argv[1]);
    uint8_t sig = (uint8_t)simple_atoi(cmd->argv[2]);
    
    if (pid == 0) {
        cli_printf("kill: invalid PID\n");
        return 1;
    }
    if (sig == 0) {
        cli_printf("kill: invalid signal\n");
        return 1;
    }
    
    // Call SYS_KILL syscall via int 0x80: rax/eax=63, rbx/ebx=pid, rcx/ecx=sig
    long result = 0;
#ifdef __x86_64__
    __asm__ volatile(
        "mov $63, %%rax\n\t"
        "mov %1, %%rbx\n\t"
        "mov %2, %%rcx\n\t"
        "int $0x80\n\t"
        "mov %%rax, %0\n\t"
        : "=r"(result)
        : "r"((long)pid), "r"((long)sig)
        : "rax", "rbx", "rcx"
    );
#else
    __asm__ volatile(
        "mov $63, %%eax\n\t"
        "mov %1, %%ebx\n\t"
        "mov %2, %%ecx\n\t"
        "int $0x80\n\t"
        "mov %%eax, %0\n\t"
        : "=r"(result)
        : "r"((long)pid), "r"((long)sig)
        : "eax", "ebx", "ecx"
    );
#endif
    
    if (result < 0) {
        cli_printf("kill: failed (process not found or error)\n");
        return 1;
    }
    cli_printf("Signal %d sent to PID %d\n", sig, pid);
    return CLI_OK;
}

static int cli_cmd_grep(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: grep <pattern> [path]\n");
        return 1;
    }

    uint8_t buf[4096];
    int n = cli_read_text_source(cmd, 2, buf, sizeof(buf), "grep <pattern> [path]");
    if (n < 0) return 1;

    const char *pattern = cmd->argv[1];
    char line[512];
    int line_len = 0;
    int matches = 0;

    for (int i = 0; i <= n; i++) {
        char c = (i == n) ? '\n' : (char)buf[i];
        if (c == '\r') continue;
        if (c == '\n' || line_len >= (int)sizeof(line) - 1) {
            line[line_len] = '\0';
            if (cli_str_contains(line, pattern)) {
                cli_printf("%s\n", line);
                matches++;
            }
            line_len = 0;
            continue;
        }
        line[line_len++] = c;
    }

    return matches > 0 ? CLI_OK : 1;
}

static int cli_cmd_wc(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    uint8_t buf[4096];
    int n = cli_read_text_source(cmd, 1, buf, sizeof(buf), "wc [path]");
    if (n < 0) return 1;

    int lines = 0;
    int words = 0;
    int in_word = 0;
    for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    cli_printf("%d %d %d\n", lines, words, n);
    return CLI_OK;
}

static uint32_t cli_session_pid_hint(const CliSession *session) {
    if (!session) return 1;
    return session->current_pid ? session->current_pid : (session->uid + 1);
}

static int cli_cmd_usockbind(CliSession *session, const ParsedCommand *cmd) {
    if (cmd->argc < 2) {
        cli_printf("Usage: usockbind <path> [owner_pid]\n");
        return 1;
    }
    uint32_t owner = (cmd->argc >= 3) ? (uint32_t)simple_atoi(cmd->argv[2]) : cli_session_pid_hint(session);
    if (owner == 0) owner = 1;

    int rc = ipc_usock_bind(cmd->argv[1], owner);
    if (rc < 0) {
        cli_printf("usockbind: failed (%d)\n", rc);
        return 1;
    }
    cli_printf("unix socket bound: %s (owner=%d)\n", cmd->argv[1], owner);
    return CLI_OK;
}

static int cli_cmd_usocksend(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: usocksend <path> <data...>\n");
        return 1;
    }

    const uint8_t *data = 0;
    uint32_t len = 0;
    char tmp[256];

    if (cmd->argc >= 3) {
        int n = cli_join_args(cmd, 2, tmp, (int)sizeof(tmp));
        data = (const uint8_t *)tmp;
        len = (uint32_t)n;
    } else if (cli_pipe_has_input()) {
        data = (const uint8_t *)g_cli_pipe_input;
        len = (uint32_t)g_cli_pipe_input_len;
    } else {
        cli_printf("Usage: usocksend <path> <data...>\n");
        return 1;
    }

    int rc = ipc_usock_send(cmd->argv[1], data, len);
    if (rc < 0) {
        cli_printf("usocksend: failed (%d)\n", rc);
        return 1;
    }
    cli_printf("usocksend: wrote %d bytes to %s\n", rc, cmd->argv[1]);
    return CLI_OK;
}

static int cli_cmd_usockrecv(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: usockrecv <path> [max_bytes]\n");
        return 1;
    }
    int max = 255;
    if (cmd->argc >= 3) {
        max = simple_atoi(cmd->argv[2]);
        if (max < 1) max = 1;
        if (max > 1024) max = 1024;
    }

    uint8_t buf[1025];
    int rc = ipc_usock_recv(cmd->argv[1], buf, (uint32_t)max);
    if (rc < 0) {
        cli_printf("usockrecv: failed (%d)\n", rc);
        return 1;
    }
    if (rc == 0) {
        cli_printf("usockrecv: empty\n");
        return CLI_OK;
    }
    buf[rc] = '\0';
    cli_printf("%s\n", (char *)buf);
    return CLI_OK;
}

static int cli_cmd_usockclose(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: usockclose <path>\n");
        return 1;
    }
    int rc = ipc_usock_close(cmd->argv[1]);
    if (rc < 0) {
        cli_printf("usockclose: failed (%d)\n", rc);
        return 1;
    }
    cli_printf("unix socket closed: %s\n", cmd->argv[1]);
    return CLI_OK;
}

static int cli_cmd_dlopen(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: dlopen <path> [name]\n");
        return 1;
    }
    const char *name = (cmd->argc >= 3) ? cmd->argv[2] : 0;
    int rc = dylib_open(cmd->argv[1], name);
    if (rc < 0) {
        cli_printf("dlopen: failed (%d)\n", rc);
        return 1;
    }
    cli_printf("dlopen: handle=%d\n", rc);
    return CLI_OK;
}

static int cli_cmd_dlsym(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 3) {
        cli_printf("Usage: dlsym <handle> <symbol>\n");
        return 1;
    }
    uint32_t handle = (uint32_t)simple_atoi(cmd->argv[1]);
    int rc = dylib_resolve(handle, cmd->argv[2]);
    if (rc < 0) {
        cli_printf("dlsym: not found (%d)\n", rc);
        return 1;
    }
    cli_printf("dlsym: %s -> fn%d\n", cmd->argv[2], rc);
    return CLI_OK;
}

static int cli_cmd_dlcall(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 3) {
        cli_printf("Usage: dlcall <handle> <symbol> [arg]\n");
        return 1;
    }
    uint32_t handle = (uint32_t)simple_atoi(cmd->argv[1]);
    uint32_t arg = (cmd->argc >= 4) ? (uint32_t)simple_atoi(cmd->argv[3]) : 0;
    int rc = dylib_call(handle, cmd->argv[2], arg);
    if (rc < 0) {
        cli_printf("dlcall: failed (%d)\n", rc);
        return 1;
    }
    cli_printf("dlcall: return=%d\n", rc);
    return CLI_OK;
}

static int cli_cmd_dlclose(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    if (cmd->argc < 2) {
        cli_printf("Usage: dlclose <handle>\n");
        return 1;
    }
    uint32_t handle = (uint32_t)simple_atoi(cmd->argv[1]);
    int rc = dylib_close(handle);
    if (rc < 0) {
        cli_printf("dlclose: failed (%d)\n", rc);
        return 1;
    }
    cli_printf("dlclose: handle=%d closed\n", handle);
    return CLI_OK;
}

static int cli_cmd_dllist(CliSession *session, const ParsedCommand *cmd) {
    (void)session;
    (void)cmd;
    int n = dylib_list();
    cli_printf("dllist: %d loaded\n", n);
    return CLI_OK;
}

// =============================================================================
// Shell Loop
// =============================================================================

void cli_shell_loop(CliSession *session) {
    if (!session) return;

    vga_enable_cursor();

    cli_printf("\n");
    cli_printf("Welcome to VernisOS Shell!\n");
    cli_printf("Type 'help' for command list\n");
    cli_printf("\n");

    char input_buf[CLI_MAX_INPUT_LEN];

    while (session->is_active) {
        cli_print_prompt(session);

        cli_memset(input_buf, 0, CLI_MAX_INPUT_LEN);
        int read_len = cli_readline(input_buf, CLI_MAX_INPUT_LEN);

        if (read_len == 0) continue;

        cli_history_push(g_shell, input_buf);
        cli_process_line(g_shell, session, input_buf);
    }
}
