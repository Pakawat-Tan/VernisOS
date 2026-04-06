// policy_enforce.c — Policy enforcement for CLI commands
//
// Phase 13: Queries AI engine access_rules to gate command execution.

#include "policy_enforce.h"
#include "ai_bridge.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void serial_print(const char *s);

// Map policy YAML privilege values to CLI privilege levels
// Policy uses: 0=root only, 1=admin+, 2=all users
// CLI uses:    0=CLI_PRIV_ROOT, 50=CLI_PRIV_ADMIN, 100=CLI_PRIV_USER
uint8_t policy_map_privilege(uint8_t policy_priv) {
    switch (policy_priv) {
        case 0:  return 0;    // Root only
        case 1:  return 50;   // Admin+
        case 2:  return 100;  // All users
        default: return 0;    // Unknown → require root
    }
}

bool policy_check_command(const char *command, uint8_t privilege) {
    if (!command) return false;

    // Calculate command length
    size_t len = 0;
    while (command[len]) len++;
    if (len == 0) return false;

    // Extract first word (the command name) for matching
    size_t cmd_len = 0;
    while (cmd_len < len && command[cmd_len] != ' ') cmd_len++;

    // For two-word commands like "policy reload", include second word
    size_t full_cmd_len = cmd_len;
    if (cmd_len < len && command[cmd_len] == ' ') {
        size_t word2_start = cmd_len + 1;
        size_t word2_end = word2_start;
        while (word2_end < len && command[word2_end] != ' ') word2_end++;
        // Try matching with both words
        full_cmd_len = word2_end;
    }

    // Query AI engine for access check (try full command first, then first word)
    uint8_t required = ai_kernel_engine_check_access(command, full_cmd_len);

    if (required == 255 && full_cmd_len != cmd_len) {
        // No match with two words — try single word
        required = ai_kernel_engine_check_access(command, cmd_len);
    }

    if (required == 255) {
        // No rule found — default: allow (no policy restriction)
        return true;
    }

    // Map policy privilege to CLI privilege
    uint8_t min_cli_priv = policy_map_privilege(required);

    // Check: session privilege must be <= required (lower = more privileged)
    return privilege <= min_cli_priv;
}
