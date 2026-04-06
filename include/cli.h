#ifndef VERNISOS_CLI_H
#define VERNISOS_CLI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// CLI / Terminal System
// Phase 7: Shell, Command Parsing, User Sessions
//
// Architecture:
//   - Shell loop accepts user input
//   - Command parser splits args
//   - Built-in commands handled directly
//   - External commands -> new user processes (with sandbox)
//   - I/O redirection & pipes (future)
// =============================================================================

// ---- Special key codes (emitted by keyboard IRQ handler, above ASCII 127) ----
#define KEY_UP    0x80   // Up arrow    — history previous
#define KEY_DOWN  0x81   // Down arrow  — history next
#define KEY_LEFT  0x82   // Left arrow  — cursor left
#define KEY_RIGHT 0x83   // Right arrow — cursor right
#define KEY_DEL   0x84   // Delete key  — delete char at cursor
#define KEY_HOME  0x85   // Home key    — cursor to start
#define KEY_END   0x86   // End key     — cursor to end

// ---- Constants ----
#define CLI_MAX_INPUT_LEN      256     // Max command line length
#define CLI_MAX_ARGS           16      // Max command arguments
#define CLI_MAX_SESSIONS       8       // Max concurrent users
#define CLI_HISTORY_SIZE       50      // Command history buffer
#define CLI_PROMPT_LEN         32

// ---- Return codes ----
#define CLI_OK                 0
#define CLI_ERR_UNKNOWN_CMD    (-1)
#define CLI_ERR_SYNTAX         (-2)
#define CLI_ERR_NO_PERMISSION  (-3)
#define CLI_ERR_EXEC_FAILED    (-4)
#define CLI_ERR_NOT_FOUND      (-5)
#define CLI_ERR_SESSION_LIMIT  (-6)

// ---- User Privilege Levels ----
typedef enum {
    CLI_PRIV_ROOT   = 0,   // Full system access
    CLI_PRIV_ADMIN  = 50,  // Administrative tasks
    CLI_PRIV_USER   = 100  // Limited user access
} CliPrivilegeLevel;

// ---- User Session ----
typedef struct CliSession {
    uint32_t session_id;           // Unique session ID
    uint32_t uid;                  // User ID
    char     username[32];         // Username
    CliPrivilegeLevel privilege;   // User's privilege level
    uint32_t current_pid;          // Current running process PID (0 if idle)
    uint64_t session_created;      // Creation timestamp
    uint64_t commands_executed;    // Total commands run in session
    bool     is_active;            // Session is running
} CliSession;

// ---- Parsed Command ----
typedef struct ParsedCommand {
    char  command[CLI_MAX_INPUT_LEN];  // Full command line
    char *argv[CLI_MAX_ARGS];          // Parsed arguments  
    int   argc;                        // Argument count
    bool  background;                  // Run in background (&)
    char *input_redirect;              // < filename
    char *output_redirect;             // > filename
    char *pipe_to;                     // | next_command
} ParsedCommand;

// ---- Built-in Command Handler ----
typedef int (*CliCommandHandler)(CliSession *session, const ParsedCommand *cmd);

// ---- Built-in Command Entry ----
typedef struct {
    const char *name;                  // Command name (e.g., "help")
    const char *description;           // Short description
    CliCommandHandler handler;         // Function to execute
    CliPrivilegeLevel min_privilege;   // Min privilege required
} CliBuiltinCommand;

// ---- Shell Context ----
typedef struct {
    CliSession *sessions[CLI_MAX_SESSIONS];
    size_t session_count;
    uint32_t next_session_id;
    
    // Command history
    char history[CLI_HISTORY_SIZE][CLI_MAX_INPUT_LEN];
    size_t history_index;
    
    // For future: script execution, aliases, etc.
    bool is_interactive;
    uint32_t exit_code;
} CliShell;

// =============================================================================
// Session Management
// =============================================================================

// Initialize CLI shell
CliShell* cli_shell_init(void);

// Create new user session
CliSession* cli_session_create(CliShell *shell, const char *username, CliPrivilegeLevel priv);

// Destroy session
void cli_session_destroy(CliShell *shell, CliSession *session);

// Get active session
CliSession* cli_shell_get_active_session(CliShell *shell);

// =============================================================================
// Command Parsing & Execution
// =============================================================================

// Parse command line into argc/argv
int cli_parse_command(const char *input, ParsedCommand *out_cmd);

// Find built-in command
const CliBuiltinCommand* cli_find_builtin(const char *name);

// Execute command (built-in or external)
int cli_execute_command(
    CliSession *session,
    const ParsedCommand *cmd,
    uint32_t *out_pid  // PID of executed process (for external commands)
);

// =============================================================================
// Built-in Commands
// =============================================================================

// Built-in commands are implemented in cli.c as static functions
// (help, exit, whoami, ps, echo, pwd, cd, ulimit)

// =============================================================================
// Terminal I/O
// =============================================================================

// Read line from input
int cli_readline(char *buf, size_t max_len);

// Print prompt
void cli_print_prompt(CliSession *session);

// Print formatted output
void cli_printf(const char *fmt, ...);

// Print command history
void cli_print_history(CliShell *shell);

// =============================================================================
// Main Shell Loop
// =============================================================================

// Interactive shell session
void cli_shell_loop(CliSession *session);

// Process single command
int cli_process_line(CliShell *shell, CliSession *session, const char *line);

#ifdef __cplusplus
}
#endif

#endif // VERNISOS_CLI_H
