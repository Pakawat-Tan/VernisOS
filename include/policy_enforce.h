// policy_enforce.h — Policy enforcement for CLI commands
//
// Phase 13: Checks commands against loaded access_rules via AI engine.

#ifndef VERNISOS_POLICY_ENFORCE_H
#define VERNISOS_POLICY_ENFORCE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Check if a command is allowed for the given privilege level.
// privilege: 0=root, 50=admin, 100=user (lower = more privileged)
// Returns true if allowed.
bool policy_check_command(const char *command, uint8_t privilege);

// Map policy min_privilege values (0/1/2) to CLI privilege levels (0/50/100).
// Policy YAML uses 0=root, 1=admin, 2=user.
uint8_t policy_map_privilege(uint8_t policy_priv);

#endif // VERNISOS_POLICY_ENFORCE_H
