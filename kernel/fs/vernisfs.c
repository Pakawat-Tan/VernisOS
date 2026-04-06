// vernisfs.c — VernisOS Simple Filesystem implementation
//
// Phase 13: Sector-based filesystem with ATA PIO read/write.
// Stored at sector 5120+ in os.img.

#include "vernisfs.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);

// =============================================================================
// ATA PIO — shared port I/O
// =============================================================================

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_CMD        0x1F7
#define ATA_STATUS     0x1F7

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_SR_BSY     0x80
#define ATA_SR_DRQ     0x08
#define ATA_SR_ERR     0x01

static inline void outb_vfs(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_vfs(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline uint16_t inw_vfs(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw_vfs(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static int ata_wait_ready_vfs(void) {
    uint32_t timeout = 1000000;
    while (timeout--) {
        uint8_t status = inb_vfs(ATA_STATUS);
        if (!(status & ATA_SR_BSY)) {
            if (status & ATA_SR_DRQ) return 1;
            if (status & ATA_SR_ERR) return -1;
        }
    }
    return 0;
}

static int ata_wait_bsy_clear(void) {
    uint32_t timeout = 1000000;
    while (timeout--) {
        uint8_t status = inb_vfs(ATA_STATUS);
        if (!(status & ATA_SR_BSY)) return 1;
    }
    return 0;
}

int vfs_ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf) {
    outb_vfs(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb_vfs(ATA_SECCOUNT, count);
    outb_vfs(ATA_LBA_LO, lba & 0xFF);
    outb_vfs(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb_vfs(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb_vfs(ATA_CMD, ATA_CMD_READ);

    for (uint8_t s = 0; s < count; s++) {
        int ready = ata_wait_ready_vfs();
        if (ready <= 0) return -1;

        uint16_t *dst = (uint16_t*)(buf + s * 512);
        for (int i = 0; i < 256; i++) {
            dst[i] = inw_vfs(ATA_DATA);
        }
    }
    return count;
}

int vfs_ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf) {
    outb_vfs(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb_vfs(ATA_SECCOUNT, count);
    outb_vfs(ATA_LBA_LO, lba & 0xFF);
    outb_vfs(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb_vfs(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb_vfs(ATA_CMD, ATA_CMD_WRITE);

    for (uint8_t s = 0; s < count; s++) {
        if (!ata_wait_bsy_clear()) return -1;
        int ready = ata_wait_ready_vfs();
        if (ready <= 0) return -1;

        const uint16_t *src = (const uint16_t*)(buf + s * 512);
        for (int i = 0; i < 256; i++) {
            outw_vfs(ATA_DATA, src[i]);
        }

        // Flush cache
        outb_vfs(ATA_CMD, 0xE7);
        ata_wait_bsy_clear();
    }
    return count;
}

// =============================================================================
// Pluggable disk I/O (defaults to ATA PIO above)
// =============================================================================

static vfs_disk_read_fn  g_disk_read  = vfs_ata_read_sectors;
static vfs_disk_write_fn g_disk_write = vfs_ata_write_sectors;

void vfs_set_disk_ops(vfs_disk_read_fn rfn, vfs_disk_write_fn wfn) {
    if (rfn) g_disk_read  = rfn;
    if (wfn) g_disk_write = wfn;
}

vfs_disk_read_fn vfs_get_disk_read(void) { return g_disk_read; }
vfs_disk_write_fn vfs_get_disk_write(void) { return g_disk_write; }

// =============================================================================
// Filesystem State
// =============================================================================

static VfsSuperblock g_superblock;
static VfsFileEntry  g_filetable[VFS_MAX_FILES];
static bool          g_vfs_ready = false;

// Helper: string compare
static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

// Helper: string copy
static void vfs_strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
}

// =============================================================================
// Filesystem Operations
// =============================================================================

bool vfs_init(void) {
    serial_print("[vfs] Initializing VernisFS at sector ");
    serial_print_dec(VFS_START_SECTOR);
    serial_print("...\n");

    // Read superblock (1 sector)
    uint8_t sb_buf[512];
    for (int i = 0; i < 512; i++) sb_buf[i] = 0;
    if (g_disk_read(VFS_START_SECTOR + VFS_SUPERBLOCK_SECTOR, 1, sb_buf) < 0) {
        serial_print("[vfs] disk read failed for superblock\n");
        return false;
    }

    // Parse superblock
    const VfsSuperblock *sb = (const VfsSuperblock *)sb_buf;
    if (sb->magic != VFS_MAGIC) {
        serial_print("[vfs] No VernisFS found (bad magic)\n");
        return false;
    }

    g_superblock = *sb;
    serial_print("[vfs] Found VernisFS v");
    serial_print_dec(g_superblock.version);
    serial_print(", ");
    serial_print_dec(g_superblock.file_count);
    serial_print(" files\n");

    // Read file table (4 sectors = 2048 bytes)
    uint8_t ft_buf[VFS_FILETABLE_SECTORS * 512];
    if (g_disk_read(VFS_START_SECTOR + VFS_FILETABLE_SECTOR,
                             VFS_FILETABLE_SECTORS, ft_buf) < 0) {
        serial_print("[vfs] disk read failed for file table\n");
        return false;
    }

    // Copy file entries
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        const VfsFileEntry *entry = (const VfsFileEntry *)(ft_buf + i * sizeof(VfsFileEntry));
        g_filetable[i] = *entry;
    }

    g_vfs_ready = true;
    serial_print("[vfs] Ready\n");
    return true;
}

const VfsFileEntry *vfs_find_file(const char *path) {
    if (!g_vfs_ready) return NULL;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        if (g_filetable[i].type != VFS_TYPE_EMPTY &&
            vfs_strcmp(g_filetable[i].filename, path) == 0) {
            return &g_filetable[i];
        }
    }
    return NULL;
}

// Phase 47: mutable access for chmod/chown
static VfsFileEntry *vfs_find_file_mut(const char *path) {
    if (!g_vfs_ready) return NULL;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        if (g_filetable[i].type != VFS_TYPE_EMPTY &&
            vfs_strcmp(g_filetable[i].filename, path) == 0) {
            return &g_filetable[i];
        }
    }
    return NULL;
}

int vfs_read_file(const char *path, uint8_t *buf, size_t max_len) {
    const VfsFileEntry *entry = vfs_find_file(path);
    if (!entry) return -1;

    uint32_t size = entry->size;
    if (size > max_len) size = (uint32_t)max_len;

    // Calculate sectors needed
    uint32_t sectors = (size + 511) / 512;
    if (sectors > 255) sectors = 255;

    uint32_t abs_sector = VFS_START_SECTOR + VFS_DATA_SECTOR + entry->start_sector;

    // Read in chunks of up to 128 sectors (64KB)
    uint32_t read = 0;
    while (read < sectors) {
        uint8_t chunk = (sectors - read > 128) ? 128 : (uint8_t)(sectors - read);
        if (g_disk_read(abs_sector + read, chunk, buf + read * 512) < 0) {
            return -1;
        }
        read += chunk;
    }

    return (int)size;
}

// Write back file table and superblock to disk
static int vfs_flush_metadata(void) {
    // Write superblock
    uint8_t sb_buf[512];
    for (int i = 0; i < 512; i++) sb_buf[i] = 0;
    VfsSuperblock *sb = (VfsSuperblock *)sb_buf;
    *sb = g_superblock;

    if (g_disk_write(VFS_START_SECTOR + VFS_SUPERBLOCK_SECTOR, 1, sb_buf) < 0)
        return -1;

    // Write file table
    uint8_t ft_buf[VFS_FILETABLE_SECTORS * 512];
    for (int i = 0; i < (int)sizeof(ft_buf); i++) ft_buf[i] = 0;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        VfsFileEntry *dst = (VfsFileEntry *)(ft_buf + i * sizeof(VfsFileEntry));
        *dst = g_filetable[i];
    }

    if (g_disk_write(VFS_START_SECTOR + VFS_FILETABLE_SECTOR,
                              VFS_FILETABLE_SECTORS, ft_buf) < 0)
        return -1;

    return 0;
}

int vfs_write_file(const char *path, const uint8_t *data, size_t len) {
    if (!g_vfs_ready) return -1;

    // Find existing file or empty slot
    int slot = -1;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        if (g_filetable[i].type != VFS_TYPE_EMPTY &&
            vfs_strcmp(g_filetable[i].filename, path) == 0) {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0) {
        // Find empty slot
        for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
            if (g_filetable[i].type == VFS_TYPE_EMPTY) {
                slot = (int)i;
                break;
            }
        }
        if (slot < 0) return -1; // No free slots

        // Initialize new entry
        vfs_strncpy(g_filetable[slot].filename, path, VFS_MAX_FILENAME);
        g_filetable[slot].type = VFS_TYPE_REGULAR;
        g_filetable[slot].flags = 0;
        g_filetable[slot].mode = VFS_MODE_DEFAULT_FILE;
        g_filetable[slot].uid = 0;
        g_filetable[slot].gid = 0;
        g_filetable[slot].start_sector = g_superblock.first_free_sector;
    }

    // Calculate sectors needed
    uint32_t sectors = ((uint32_t)len + 511) / 512;
    if (sectors > 255) return -1; // Too large

    // Write data
    uint32_t abs_sector = VFS_START_SECTOR + VFS_DATA_SECTOR + g_filetable[slot].start_sector;

    // Pad last sector with zeros
    uint8_t pad_buf[512];
    uint32_t full_sectors = (uint32_t)len / 512;
    uint32_t remainder = (uint32_t)len % 512;

    // Write full sectors
    for (uint32_t s = 0; s < full_sectors; s++) {
        if (g_disk_write(abs_sector + s, 1, data + s * 512) < 0)
            return -1;
    }

    // Write partial last sector (padded)
    if (remainder > 0) {
        for (int i = 0; i < 512; i++) pad_buf[i] = 0;
        for (uint32_t i = 0; i < remainder; i++)
            pad_buf[i] = data[full_sectors * 512 + i];
        if (g_disk_write(abs_sector + full_sectors, 1, pad_buf) < 0)
            return -1;
    }

    // Update entry
    g_filetable[slot].size = (uint32_t)len;

    // Update free sector pointer if this is a new file
    uint32_t end_sector = g_filetable[slot].start_sector + sectors;
    if (end_sector > g_superblock.first_free_sector)
        g_superblock.first_free_sector = end_sector;

    // Count active files
    uint16_t count = 0;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        if (g_filetable[i].type != VFS_TYPE_EMPTY) count++;
    }
    g_superblock.file_count = count;

    // Flush metadata to disk
    return vfs_flush_metadata() == 0 ? (int)len : -1;
}

uint16_t vfs_file_count(void) {
    return g_vfs_ready ? g_superblock.file_count : 0;
}

// =============================================================================
// Additional filesystem operations
// =============================================================================

// Returns 1 if 'child' is a direct child of 'parent', 0 otherwise
static int vfs_is_child_of(const char *child, const char *parent) {
    // Find last '/' in child
    int last_slash = 0;
    for (int i = 0; child[i]; i++)
        if (child[i] == '/') last_slash = i;

    if (last_slash == 0) {
        // Direct child of root: parent must be "/"
        return parent[0] == '/' && parent[1] == '\0';
    }

    // Parent = child[0..last_slash-1], must equal 'parent'
    for (int i = 0; i < last_slash; i++) {
        if (child[i] != parent[i] || parent[i] == '\0') return 0;
    }
    return parent[last_slash] == '\0';
}

int vfs_list_dir(const char *dir_path,
                 char out[][VFS_MAX_FILENAME], int max) {
    if (!g_vfs_ready) return -1;
    int count = 0;
    for (uint16_t i = 0; i < VFS_MAX_FILES && count < max; i++) {
        if (g_filetable[i].type == VFS_TYPE_EMPTY) continue;
        if (vfs_strcmp(g_filetable[i].filename, dir_path) == 0) continue; // skip self
        if (vfs_is_child_of(g_filetable[i].filename, dir_path)) {
            vfs_strncpy(out[count], g_filetable[i].filename, VFS_MAX_FILENAME);
            count++;
        }
    }
    return count;
}

int vfs_delete_file(const char *path) {
    if (!g_vfs_ready) return -1;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        if (g_filetable[i].type != VFS_TYPE_EMPTY &&
            vfs_strcmp(g_filetable[i].filename, path) == 0) {
            uint8_t *p = (uint8_t *)&g_filetable[i];
            for (size_t j = 0; j < sizeof(VfsFileEntry); j++) p[j] = 0;
            if (g_superblock.file_count > 0) g_superblock.file_count--;
            return vfs_flush_metadata() == 0 ? 0 : -1;
        }
    }
    return -1;
}

int vfs_mkdir(const char *path) {
    if (!g_vfs_ready) return -1;
    // Don't create duplicate
    if (vfs_find_file(path)) return -1;
    for (uint16_t i = 0; i < VFS_MAX_FILES; i++) {
        if (g_filetable[i].type == VFS_TYPE_EMPTY) {
            vfs_strncpy(g_filetable[i].filename, path, VFS_MAX_FILENAME);
            g_filetable[i].type  = VFS_TYPE_DIRECTORY;
            g_filetable[i].size  = 0;
            g_filetable[i].flags = 0;
            g_filetable[i].mode  = VFS_MODE_DEFAULT_DIR;
            g_filetable[i].uid   = 0;
            g_filetable[i].gid   = 0;
            g_superblock.file_count++;
            return vfs_flush_metadata() == 0 ? 0 : -1;
        }
    }
    return -1;
}

int vfs_append_file(const char *path, const uint8_t *data, size_t len) {
    const VfsFileEntry *entry = vfs_find_file(path);
    if (!entry) return vfs_write_file(path, data, len);

    uint32_t existing = entry->size;
    uint32_t total    = existing + (uint32_t)len;
    if (total > 4096) total = 4096;  // hard cap per file

    // Read existing content
    static uint8_t tmp[4096];
    int r = vfs_read_file(path, tmp, existing);
    if (r < 0) return -1;

    // Append new data
    uint32_t copy = total - existing;
    for (uint32_t i = 0; i < copy; i++) tmp[existing + i] = data[i];

    return vfs_write_file(path, tmp, total);
}

// =============================================================================
// Phase 47: Permission helpers
// =============================================================================

int vfs_chmod(const char *path, uint16_t mode) {
    VfsFileEntry *entry = vfs_find_file_mut(path);
    if (!entry) return -1;
    entry->mode = mode & 0777;
    return vfs_flush_metadata();
}

int vfs_chown(const char *path, uint16_t uid, uint16_t gid) {
    VfsFileEntry *entry = vfs_find_file_mut(path);
    if (!entry) return -1;
    entry->uid = uid;
    entry->gid = gid;
    return vfs_flush_metadata();
}
