// VernisOS Phase 14: Deny Audit Log
// Ring buffer for recording permission denials — viewable via `auditlog` command.
#ifndef VERNISOS_AUDITLOG_H
#define VERNISOS_AUDITLOG_H

#include <stdint.h>

#define AUDIT_LOG_SIZE    32    // Max entries in ring buffer
#define AUDIT_CMD_MAX     48    // Max command string length stored
#define AUDIT_USER_MAX    16    // Max username length stored

typedef struct {
    uint64_t timestamp;               // Kernel ticks when denied
    char     username[AUDIT_USER_MAX]; // Who tried
    char     command[AUDIT_CMD_MAX];   // What command
    uint8_t  privilege;               // User's privilege at time of denial
    uint8_t  required;                // Required privilege for command
} AuditEntry;

// Initialize the audit log (call once at boot)
void auditlog_init(void);

// Record a denial event
void auditlog_record(uint64_t timestamp, const char *username,
                     const char *command, uint8_t privilege, uint8_t required);

// Get total number of denials recorded (lifetime)
uint32_t auditlog_total_count(void);

// Get number of entries currently in ring buffer
uint32_t auditlog_current_count(void);

// Print all entries in the ring buffer (newest first)
void auditlog_print_all(void);

#endif // VERNISOS_AUDITLOG_H
