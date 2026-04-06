// vernisfs.h — VernisOS Simple Filesystem
//
// Phase 13: Sector-based filesystem stored at sector 3072+ in os.img.
// Provides persistent storage for user DB, config, and future modules.

#ifndef VERNISOS_VERNISFS_H
#define VERNISOS_VERNISFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Disk layout constants
#define VFS_START_SECTOR     5120     // First sector of VernisFS on disk
#define VFS_SUPERBLOCK_SECTOR 0      // Relative: superblock at VFS_START_SECTOR + 0
#define VFS_FILETABLE_SECTOR  1      // Relative: file table starts at +1
#define VFS_FILETABLE_SECTORS 4      // 4 sectors = 2048 bytes for file table
#define VFS_DATA_SECTOR       5      // Relative: data blocks start at +5
#define VFS_MAX_FILES         32     // Maximum files in the table
#define VFS_MAX_FILENAME      32     // Max filename length (including path)
#define VFS_MAGIC             0x53465600  // "VFS\0" little-endian

// File types
#define VFS_TYPE_EMPTY     0
#define VFS_TYPE_REGULAR   1
#define VFS_TYPE_DIRECTORY 2

// File flags
#define VFS_FLAG_READONLY  0x01
#define VFS_FLAG_SYSTEM    0x02

// Phase 47: Unix-style permission bits (stored in mode field)
#define VFS_PERM_UR  0400   // owner read
#define VFS_PERM_UW  0200   // owner write
#define VFS_PERM_UX  0100   // owner execute
#define VFS_PERM_GR  0040   // group read
#define VFS_PERM_GW  0020   // group write
#define VFS_PERM_GX  0010   // group execute
#define VFS_PERM_OR  0004   // other read
#define VFS_PERM_OW  0002   // other write
#define VFS_PERM_OX  0001   // other execute

#define VFS_MODE_DEFAULT_FILE  0644   // rw-r--r--
#define VFS_MODE_DEFAULT_DIR   0755   // rwxr-xr-x
#define VFS_MODE_DEFAULT_EXEC  0755   // rwxr-xr-x

// Superblock (512 bytes, only first 32 used)
typedef struct {
    uint32_t magic;              // VFS_MAGIC
    uint16_t version;            // Filesystem version (1)
    uint16_t file_count;         // Number of active files
    uint32_t total_data_sectors; // Total data sectors available
    uint32_t first_free_sector;  // Next free data sector (relative to data start)
    uint8_t  reserved[16];       // Padding
} __attribute__((packed)) VfsSuperblock;

// File table entry (64 bytes)
typedef struct {
    char     filename[VFS_MAX_FILENAME]; // Null-terminated path (e.g., "/etc/shadow")
    uint32_t start_sector;       // Data sector offset (relative to VFS data start)
    uint32_t size;               // File size in bytes
    uint8_t  type;               // VFS_TYPE_*
    uint8_t  flags;              // VFS_FLAG_*
    uint16_t mode;               // Phase 47: Unix permission bits (e.g. 0755)
    uint16_t uid;                // Phase 47: owner user ID
    uint16_t gid;                // Phase 47: owner group ID
    uint8_t  reserved[16];       // Padding to 64 bytes
} __attribute__((packed)) VfsFileEntry;

// =============================================================================
// Disk I/O Backend (pluggable — default: ATA PIO)
// =============================================================================

typedef int (*vfs_disk_read_fn)(uint32_t lba, uint8_t count, uint8_t *buf);
typedef int (*vfs_disk_write_fn)(uint32_t lba, uint8_t count, const uint8_t *buf);

// Override the sector-level I/O used by VernisFS.
// Pass NULL to keep the current function.
void vfs_set_disk_ops(vfs_disk_read_fn rfn, vfs_disk_write_fn wfn);

// Get current disk I/O function pointers (for cache interposition)
vfs_disk_read_fn  vfs_get_disk_read(void);
vfs_disk_write_fn vfs_get_disk_write(void);

// =============================================================================
// ATA PIO Interface (read + write) — default disk backend
// =============================================================================

// Read sectors from disk (absolute LBA)
int vfs_ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf);

// Write sectors to disk (absolute LBA)
int vfs_ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf);

// =============================================================================
// Filesystem API
// =============================================================================

// Initialize VernisFS — reads superblock from disk
// Returns true if valid filesystem found
bool vfs_init(void);

// Find a file by path. Returns pointer to entry or NULL.
const VfsFileEntry *vfs_find_file(const char *path);

// Read file contents into buffer. Returns bytes read, or -1 on error.
int vfs_read_file(const char *path, uint8_t *buf, size_t max_len);

// Write file contents. Creates file if it doesn't exist, overwrites if it does.
// Returns bytes written, or -1 on error.
int vfs_write_file(const char *path, const uint8_t *data, size_t len);

// Append data to existing file (creates if not exists).
// Returns new total size, or -1 on error.
int vfs_append_file(const char *path, const uint8_t *data, size_t len);

// Create a directory entry. Returns 0 on success, -1 on error.
int vfs_mkdir(const char *path);

// Delete a file or empty directory. Returns 0 on success, -1 if not found.
int vfs_delete_file(const char *path);

// List directory contents into out[0..max-1] (full paths).
// Returns number of entries found, or -1 if VFS not ready.
int vfs_list_dir(const char *dir_path,
                 char out[][VFS_MAX_FILENAME], int max);

// Get filesystem stats
uint16_t vfs_file_count(void);

// Phase 47: File permissions
int vfs_chmod(const char *path, uint16_t mode);
int vfs_chown(const char *path, uint16_t uid, uint16_t gid);

#endif // VERNISOS_VERNISFS_H
