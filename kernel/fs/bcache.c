// bcache.c — VernisOS Disk Block Cache implementation
//
// Phase 48: LRU write-back block cache.
// Intercepts g_disk_read / g_disk_write in VernisFS to provide
// sector-level caching with dirty tracking.

#include "bcache.h"
#include "vernisfs.h"

extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);

// --------------------------------------------------------------------------
// Cache block entry
// --------------------------------------------------------------------------

#define BCACHE_FLAG_VALID  0x01
#define BCACHE_FLAG_DIRTY  0x02

typedef struct {
    uint32_t lba;           // Sector address
    uint8_t  flags;         // BCACHE_FLAG_*
    uint8_t  _pad[3];
    uint32_t access_tick;   // LRU ordering
    uint8_t  data[BCACHE_BLOCK_SIZE];
} BcacheBlock;

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

static BcacheBlock  g_cache[BCACHE_NUM_BLOCKS];
static BcacheStats  g_stats;
static uint32_t     g_tick;               // Monotonic access counter
static uint32_t     g_flush_interval;     // Ticks between auto-flushes (timer Hz)
static uint32_t     g_flush_counter;      // Counts timer ticks

// Saved real disk ops (the actual backend — ATA / AHCI / NVMe)
static vfs_disk_read_fn  g_real_read;
static vfs_disk_write_fn g_real_write;

static int g_bcache_ready = 0;

// --------------------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------------------

static void bcache_memcpy(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

static void bcache_memset(uint8_t *dst, uint8_t val, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = val;
}

// Find cache entry for a given LBA. Returns index or -1.
static int bcache_find(uint32_t lba) {
    for (int i = 0; i < BCACHE_NUM_BLOCKS; i++) {
        if ((g_cache[i].flags & BCACHE_FLAG_VALID) && g_cache[i].lba == lba)
            return i;
    }
    return -1;
}

// Find LRU (least recently used) slot. Prefer invalid, then oldest.
static int bcache_lru(void) {
    int best = 0;
    uint32_t best_tick = 0xFFFFFFFF;
    for (int i = 0; i < BCACHE_NUM_BLOCKS; i++) {
        if (!(g_cache[i].flags & BCACHE_FLAG_VALID))
            return i;  // Empty slot — use immediately
        if (g_cache[i].access_tick < best_tick) {
            best_tick = g_cache[i].access_tick;
            best = i;
        }
    }
    return best;
}

// Write back a single dirty block. Returns 0 on success.
static int bcache_writeback(int idx) {
    if (idx < 0 || idx >= BCACHE_NUM_BLOCKS) return -1;
    BcacheBlock *b = &g_cache[idx];
    if (!(b->flags & BCACHE_FLAG_DIRTY)) return 0;

    int rc = g_real_write(b->lba, 1, b->data);
    if (rc < 0) return -1;

    b->flags &= (uint8_t)~BCACHE_FLAG_DIRTY;
    g_stats.writebacks++;
    return 0;
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

int bcache_read(uint32_t lba, uint8_t count, uint8_t *buf) {
    if (!g_bcache_ready || !g_real_read) return -1;

    for (uint8_t s = 0; s < count; s++) {
        uint32_t sector = lba + s;
        int idx = bcache_find(sector);

        if (idx >= 0) {
            // Cache hit
            g_stats.hits++;
            g_cache[idx].access_tick = ++g_tick;
            bcache_memcpy(buf + s * BCACHE_BLOCK_SIZE,
                          g_cache[idx].data, BCACHE_BLOCK_SIZE);
        } else {
            // Cache miss — read from disk
            g_stats.misses++;
            int slot = bcache_lru();

            // Evict if dirty
            if (g_cache[slot].flags & BCACHE_FLAG_DIRTY) {
                bcache_writeback(slot);
                g_stats.evictions++;
            } else if (g_cache[slot].flags & BCACHE_FLAG_VALID) {
                g_stats.evictions++;
            }

            // Read single sector from disk into cache
            int rc = g_real_read(sector, 1, g_cache[slot].data);
            if (rc < 0) return -1;

            g_cache[slot].lba = sector;
            g_cache[slot].flags = BCACHE_FLAG_VALID;
            g_cache[slot].access_tick = ++g_tick;

            bcache_memcpy(buf + s * BCACHE_BLOCK_SIZE,
                          g_cache[slot].data, BCACHE_BLOCK_SIZE);
        }
    }
    return (int)count;
}

int bcache_write(uint32_t lba, uint8_t count, const uint8_t *buf) {
    if (!g_bcache_ready || !g_real_write) return -1;

    for (uint8_t s = 0; s < count; s++) {
        uint32_t sector = lba + s;
        int idx = bcache_find(sector);

        if (idx < 0) {
            // Not in cache — allocate a slot
            idx = bcache_lru();

            // Evict if dirty
            if (g_cache[idx].flags & BCACHE_FLAG_DIRTY) {
                bcache_writeback(idx);
                g_stats.evictions++;
            } else if (g_cache[idx].flags & BCACHE_FLAG_VALID) {
                g_stats.evictions++;
            }

            g_cache[idx].lba = sector;
        }

        // Write data into cache block
        bcache_memcpy(g_cache[idx].data,
                      buf + s * BCACHE_BLOCK_SIZE, BCACHE_BLOCK_SIZE);
        g_cache[idx].flags = BCACHE_FLAG_VALID | BCACHE_FLAG_DIRTY;
        g_cache[idx].access_tick = ++g_tick;
    }
    return (int)count;
}

int bcache_sync(void) {
    if (!g_bcache_ready) return -1;
    int err = 0;
    for (int i = 0; i < BCACHE_NUM_BLOCKS; i++) {
        if (g_cache[i].flags & BCACHE_FLAG_DIRTY) {
            if (bcache_writeback(i) < 0) err = -1;
        }
    }
    return err;
}

void bcache_tick(void) {
    if (!g_bcache_ready) return;
    g_flush_counter++;
    // Flush every ~1 second (flush_interval set during init)
    if (g_flush_counter >= g_flush_interval) {
        g_flush_counter = 0;
        // Only flush if there are dirty blocks
        for (int i = 0; i < BCACHE_NUM_BLOCKS; i++) {
            if (g_cache[i].flags & BCACHE_FLAG_DIRTY) {
                bcache_sync();
                return;
            }
        }
    }
}

BcacheStats bcache_get_stats(void) {
    return g_stats;
}

void bcache_invalidate(void) {
    for (int i = 0; i < BCACHE_NUM_BLOCKS; i++) {
        g_cache[i].flags = 0;
        g_cache[i].lba = 0;
        g_cache[i].access_tick = 0;
    }
    g_tick = 0;
}

void bcache_init(void) {
    // Zero out all cache blocks
    for (int i = 0; i < BCACHE_NUM_BLOCKS; i++) {
        bcache_memset((uint8_t *)&g_cache[i], 0, sizeof(BcacheBlock));
    }
    g_stats.hits = 0;
    g_stats.misses = 0;
    g_stats.writebacks = 0;
    g_stats.evictions = 0;
    g_tick = 0;
    g_flush_counter = 0;
    g_flush_interval = 240;  // ~1 second at 240 Hz timer

    // Capture current disk ops as the "real" backend
    // (set by kfs_init → vfs_set_disk_ops() for AHCI/NVMe,
    //  or default ATA PIO)
    g_real_read  = vfs_get_disk_read();
    g_real_write = vfs_get_disk_write();

    // Install cache as the new disk I/O layer
    vfs_set_disk_ops(bcache_read, bcache_write);

    g_bcache_ready = 1;
    serial_print("[bcache] Block cache initialized (");
    serial_print_dec(BCACHE_NUM_BLOCKS);
    serial_print(" blocks, ");
    serial_print_dec(BCACHE_NUM_BLOCKS * BCACHE_BLOCK_SIZE / 1024);
    serial_print(" KB)\n");
}
