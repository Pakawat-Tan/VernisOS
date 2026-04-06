// klog.h — Kernel Structured Logging System (Phase 16)
//
// Provides severity-tagged, timestamped log entries with a ring buffer
// for in-memory log history (viewable via CLI "log" command).

#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>
#include <stddef.h>

// Log severity levels (lower = more critical)
typedef enum {
    KLOG_FATAL = 0,
    KLOG_ERROR = 1,
    KLOG_WARN  = 2,
    KLOG_INFO  = 3,
    KLOG_DEBUG = 4,
    KLOG_TRACE = 5,
} KlogLevel;

// Single log entry stored in the ring buffer
#define KLOG_TAG_MAX  12
#define KLOG_MSG_MAX  80
#define KLOG_RING_SIZE 64

typedef struct {
    uint64_t timestamp;             // Kernel ticks
    KlogLevel level;                // Severity
    char tag[KLOG_TAG_MAX];         // Subsystem tag (e.g., "ipc", "sched")
    char message[KLOG_MSG_MAX];     // Log message
} KlogEntry;

// Initialize the logging system
void klog_init(void);

// Log a message with tag and severity
void klog(KlogLevel level, const char *tag, const char *msg);

// Set minimum severity level (logs below this are suppressed from serial)
void klog_set_level(KlogLevel min_level);

// Get current minimum level
KlogLevel klog_get_level(void);

// Get ring buffer entry count
uint32_t klog_count(void);

// Get total log entries ever recorded
uint64_t klog_total(void);

// Print recent log entries to CLI (newest first). 0 = all in buffer.
void klog_print_recent(uint32_t count);

// Clear the ring buffer
void klog_clear(void);

// Severity name string
const char *klog_level_name(KlogLevel level);

// Convenience macros
#define KLOG_FATAL(tag, msg)  klog(KLOG_FATAL, (tag), (msg))
#define KLOG_ERROR(tag, msg)  klog(KLOG_ERROR, (tag), (msg))
#define KLOG_WARN(tag, msg)   klog(KLOG_WARN,  (tag), (msg))
#define KLOG_INFO(tag, msg)   klog(KLOG_INFO,  (tag), (msg))
#define KLOG_DEBUG(tag, msg)  klog(KLOG_DEBUG, (tag), (msg))
#define KLOG_TRACE(tag, msg)  klog(KLOG_TRACE, (tag), (msg))

#endif // KLOG_H
