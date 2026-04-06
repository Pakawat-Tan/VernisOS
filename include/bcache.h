// bcache.h — VernisOS Disk Block Cache
//
// Phase 48: LRU block cache between VFS and disk backend.
// Caches 512-byte sectors with dirty tracking and write-back.

#ifndef VERNISOS_BCACHE_H
#define VERNISOS_BCACHE_H

#include <stdint.h>
#include <stddef.h>

// Cache configuration
#define BCACHE_NUM_BLOCKS   64      // Number of cached sectors
#define BCACHE_BLOCK_SIZE   512     // Bytes per sector

// Cache statistics
typedef struct {
    uint32_t hits;
    uint32_t misses;
    uint32_t writebacks;
    uint32_t evictions;
} BcacheStats;

// Initialize block cache and intercept disk I/O.
// Must be called AFTER vfs_init() / kfs_init().
void bcache_init(void);

// Read sectors through cache.
// Same signature as vfs_disk_read_fn.
int bcache_read(uint32_t lba, uint8_t count, uint8_t *buf);

// Write sectors through cache (marks dirty, deferred write).
// Same signature as vfs_disk_write_fn.
int bcache_write(uint32_t lba, uint8_t count, const uint8_t *buf);

// Flush all dirty blocks to disk. Returns 0 on success, -1 on error.
int bcache_sync(void);

// Periodic flush — call from timer IRQ at low frequency.
// Only flushes if dirty blocks exist.
void bcache_tick(void);

// Get cache statistics.
BcacheStats bcache_get_stats(void);

// Invalidate the entire cache (discard all entries).
void bcache_invalidate(void);

#endif // VERNISOS_BCACHE_H
