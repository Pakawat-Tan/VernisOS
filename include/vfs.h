// vfs.h — filesystem abstraction layer over concrete backends (VernisFS for now)

#ifndef VERNISOS_VFS_H
#define VERNISOS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vernisfs.h"

typedef enum {
    KFS_BACKEND_NONE = 0,
    KFS_BACKEND_VERNISFS = 1,
    KFS_BACKEND_AHCI = 2,
    KFS_BACKEND_NVME = 3,
} KfsBackend;

// Initialize filesystem abstraction and mount default backend.
bool kfs_init(void);

// Query current mount state.
bool kfs_ready(void);
KfsBackend kfs_backend(void);
const char *kfs_backend_name(void);

// Filesystem operations (delegated to mounted backend).
const VfsFileEntry *kfs_find_file(const char *path);
int kfs_read_file(const char *path, uint8_t *buf, size_t max_len);
int kfs_write_file(const char *path, const uint8_t *data, size_t len);
int kfs_append_file(const char *path, const uint8_t *data, size_t len);
int kfs_mkdir(const char *path);
int kfs_delete_file(const char *path);
int kfs_list_dir(const char *dir_path, char out[][VFS_MAX_FILENAME], int max);
uint16_t kfs_file_count(void);

// Phase 47: File permissions
int kfs_chmod(const char *path, uint16_t mode);
int kfs_chown(const char *path, uint16_t uid, uint16_t gid);
// Check if uid can perform operation on file (0=ok, -1=denied)
// op: 'r'=read, 'w'=write, 'x'=execute
int kfs_check_perm(const char *path, uint16_t uid, char op);

#endif // VERNISOS_VFS_H
