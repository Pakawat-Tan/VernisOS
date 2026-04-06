// vfs.c — kernel filesystem abstraction layer
// Currently mounts VernisFS as the default backend.
// Can use NVMe or AHCI DMA when a suitable controller is present.

#include "vfs.h"
#include "scheduler_base.h"

typedef struct {
    const VfsFileEntry *(*find_file)(const char *path);
    int (*read_file)(const char *path, uint8_t *buf, size_t max_len);
    int (*write_file)(const char *path, const uint8_t *data, size_t len);
    int (*append_file)(const char *path, const uint8_t *data, size_t len);
    int (*mkdir_fn)(const char *path);
    int (*delete_file)(const char *path);
    int (*list_dir)(const char *dir_path, char out[][VFS_MAX_FILENAME], int max);
    uint16_t (*file_count)(void);
} KfsOps;

extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);
extern uint32_t kernel_get_ticks(void);
extern uint32_t kernel_get_timer_hz(void);

/* AHCI kernel API (weak — link succeeds even without AHCI driver) */
extern int  kernel_ahci_available(void);
extern int  kernel_ahci_identified(int port);
extern uint32_t kernel_ahci_pi(void);
extern int  kernel_ahci_read(int port, uint64_t lba, uint32_t sectors,
                             uint8_t *out, uint32_t out_max);
extern int  kernel_ahci_write(int port, uint64_t lba, uint32_t sectors,
                              const uint8_t *data, uint32_t data_len);

/* NVMe kernel API (weak — link succeeds even without NVMe driver) */
extern int  kernel_nvme_available(void);
extern int  kernel_nvme_identified(void);
extern int  kernel_nvme_read(uint64_t lba, uint32_t sectors,
                             uint8_t *out, uint32_t out_max);
extern int  kernel_nvme_write(uint64_t lba, uint32_t sectors,
                              const uint8_t *data, uint32_t data_len);

static KfsBackend g_backend = KFS_BACKEND_NONE;
static const KfsOps *g_ops = (const KfsOps *)0;

static const VfsFileEntry KFS_NODE_PROC = {
    .filename = "/proc",
    .type = VFS_TYPE_DIRECTORY,
};
static const VfsFileEntry KFS_NODE_DEV = {
    .filename = "/dev",
    .type = VFS_TYPE_DIRECTORY,
};
static const VfsFileEntry KFS_NODE_PROC_UPTIME = {
    .filename = "/proc/uptime",
    .type = VFS_TYPE_REGULAR,
};
static const VfsFileEntry KFS_NODE_PROC_PS = {
    .filename = "/proc/ps",
    .type = VFS_TYPE_REGULAR,
};
static const VfsFileEntry KFS_NODE_PROC_FS = {
    .filename = "/proc/fs",
    .type = VFS_TYPE_REGULAR,
};
static const VfsFileEntry KFS_NODE_DEV_NULL = {
    .filename = "/dev/null",
    .type = VFS_TYPE_REGULAR,
};
static const VfsFileEntry KFS_NODE_DEV_ZERO = {
    .filename = "/dev/zero",
    .type = VFS_TYPE_REGULAR,
};

static int kfs_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static int kfs_streq(const char *a, const char *b) {
    return kfs_strcmp(a, b) == 0;
}

static int kfs_path_is_pseudo(const char *path) {
    if (!path) return 0;
    return kfs_streq(path, "/proc") ||
           kfs_streq(path, "/dev") ||
           kfs_streq(path, "/proc/uptime") ||
           kfs_streq(path, "/proc/ps") ||
           kfs_streq(path, "/proc/fs") ||
           kfs_streq(path, "/dev/null") ||
           kfs_streq(path, "/dev/zero");
}

static const VfsFileEntry *kfs_find_pseudo(const char *path) {
    if (!path) return (const VfsFileEntry *)0;
    if (kfs_streq(path, "/proc")) return &KFS_NODE_PROC;
    if (kfs_streq(path, "/dev")) return &KFS_NODE_DEV;
    if (kfs_streq(path, "/proc/uptime")) return &KFS_NODE_PROC_UPTIME;
    if (kfs_streq(path, "/proc/ps")) return &KFS_NODE_PROC_PS;
    if (kfs_streq(path, "/proc/fs")) return &KFS_NODE_PROC_FS;
    if (kfs_streq(path, "/dev/null")) return &KFS_NODE_DEV_NULL;
    if (kfs_streq(path, "/dev/zero")) return &KFS_NODE_DEV_ZERO;
    return (const VfsFileEntry *)0;
}

static int kfs_copy_str(char *dst, int max, const char *src) {
    int i = 0;
    if (max <= 0) return 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static int kfs_append_str(char *dst, int max, int pos, const char *src) {
    if (pos < 0) pos = 0;
    while (src && *src && pos < max - 1) dst[pos++] = *src++;
    if (max > 0) dst[pos < max ? pos : (max - 1)] = '\0';
    return pos;
}

static int kfs_append_u64(char *dst, int max, int pos, uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = n - 1; i >= 0 && pos < max - 1; i--) dst[pos++] = tmp[i];
    if (max > 0) dst[pos < max ? pos : (max - 1)] = '\0';
    return pos;
}

static int kfs_read_proc_uptime(uint8_t *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    char out[96];
    uint32_t hz = kernel_get_timer_hz();
    if (hz == 0) hz = 1;
    uint64_t ticks = kernel_get_ticks();
    uint64_t secs = ticks / hz;
    uint64_t rem = ticks % hz;
    uint64_t frac = (rem * 100) / hz;

    int pos = 0;
    pos = kfs_append_u64(out, (int)sizeof(out), pos, secs);
    pos = kfs_append_str(out, (int)sizeof(out), pos, ".");
    if (frac < 10) pos = kfs_append_str(out, (int)sizeof(out), pos, "0");
    pos = kfs_append_u64(out, (int)sizeof(out), pos, frac);
    pos = kfs_append_str(out, (int)sizeof(out), pos, "\n");

    size_t copy = (size_t)pos;
    if (copy > max_len) copy = max_len;
    for (size_t i = 0; i < copy; i++) buf[i] = (uint8_t)out[i];
    return (int)copy;
}

static int kfs_read_proc_fs(uint8_t *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    char out[192];
    int pos = 0;

    pos = kfs_append_str(out, (int)sizeof(out), pos, "backend=");
    pos = kfs_append_str(out, (int)sizeof(out), pos, kfs_backend_name());
    pos = kfs_append_str(out, (int)sizeof(out), pos, "\nready=");
    pos = kfs_append_u64(out, (int)sizeof(out), pos, kfs_ready() ? 1 : 0);
    pos = kfs_append_str(out, (int)sizeof(out), pos, "\nfiles=");
    pos = kfs_append_u64(out, (int)sizeof(out), pos, kfs_file_count());
    pos = kfs_append_str(out, (int)sizeof(out), pos, "\n");

    size_t copy = (size_t)pos;
    if (copy > max_len) copy = max_len;
    for (size_t i = 0; i < copy; i++) buf[i] = (uint8_t)out[i];
    return (int)copy;
}

static int kfs_read_proc_ps(uint8_t *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    char out[4096];
    int pos = 0;
    pos = kfs_append_str(out, (int)sizeof(out), pos, "PID STATE PRIO RING CMD\n");

    struct Scheduler *sched = get_kernel_scheduler();
    if (!sched) {
        pos = kfs_append_str(out, (int)sizeof(out), pos, "(scheduler not ready)\n");
    } else {
        size_t pids[64];
        size_t n = scheduler_get_pid_list(sched, pids, 64);
        for (size_t i = 0; i < n; i++) {
            PsRow row;
            if (!scheduler_get_ps_row(sched, pids[i], &row)) continue;
            pos = kfs_append_u64(out, (int)sizeof(out), pos, row.pid);
            pos = kfs_append_str(out, (int)sizeof(out), pos, " ");
            pos = kfs_append_u64(out, (int)sizeof(out), pos, row.state);
            pos = kfs_append_str(out, (int)sizeof(out), pos, " ");
            pos = kfs_append_u64(out, (int)sizeof(out), pos, row.priority);
            pos = kfs_append_str(out, (int)sizeof(out), pos, " ");
            pos = kfs_append_u64(out, (int)sizeof(out), pos, row.ring);
            pos = kfs_append_str(out, (int)sizeof(out), pos, " ");
            pos = kfs_append_str(out, (int)sizeof(out), pos, row.command);
            pos = kfs_append_str(out, (int)sizeof(out), pos, "\n");
            if (pos >= (int)sizeof(out) - 64) break;
        }
    }

    size_t copy = (size_t)pos;
    if (copy > max_len) copy = max_len;
    for (size_t i = 0; i < copy; i++) buf[i] = (uint8_t)out[i];
    return (int)copy;
}

static int kfs_list_dir_add(char out[][VFS_MAX_FILENAME], int max, int count, const char *path) {
    if (count < 0 || count >= max) return count;
    kfs_copy_str(out[count], VFS_MAX_FILENAME, path);
    return count + 1;
}

static const KfsOps KFS_OPS_VERNISFS = {
    .find_file = vfs_find_file,
    .read_file = vfs_read_file,
    .write_file = vfs_write_file,
    .append_file = vfs_append_file,
    .mkdir_fn = vfs_mkdir,
    .delete_file = vfs_delete_file,
    .list_dir = vfs_list_dir,
    .file_count = vfs_file_count,
};

/* ===================================================================
 * AHCI DMA adapter — translates VernisFS sector I/O to AHCI commands
 * =================================================================== */

static int g_ahci_port = -1;   /* AHCI port used for VernisFS disk I/O */

static int ahci_sector_read(uint32_t lba, uint8_t count, uint8_t *buf) {
    if (g_ahci_port < 0) return -1;
    uint8_t done = 0;
    while (done < count) {
        uint8_t chunk = (uint8_t)((count - done > 8) ? 8 : (count - done));
        int rc = kernel_ahci_read(g_ahci_port, (uint64_t)lba + done,
                                  (uint32_t)chunk, buf + done * 512,
                                  (uint32_t)chunk * 512u);
        if (rc < 0) return -1;
        done += chunk;
    }
    return (int)count;
}

static int ahci_sector_write(uint32_t lba, uint8_t count, const uint8_t *buf) {
    if (g_ahci_port < 0) return -1;
    uint8_t done = 0;
    while (done < count) {
        uint8_t chunk = (uint8_t)((count - done > 8) ? 8 : (count - done));
        int rc = kernel_ahci_write(g_ahci_port, (uint64_t)lba + done,
                                   (uint32_t)chunk, buf + done * 512,
                                   (uint32_t)chunk * 512u);
        if (rc < 0) return -1;
        done += chunk;
    }
    return (int)count;
}

static int kfs_try_ahci(void) {
    if (!kernel_ahci_available()) return 0;
    uint32_t pi = kernel_ahci_pi();
    for (int p = 0; p < 32; p++) {
        if (!(pi & (1u << (uint32_t)p))) continue;
        if (kernel_ahci_identified(p)) {
            g_ahci_port = p;
            vfs_set_disk_ops(ahci_sector_read, ahci_sector_write);
            serial_print("[kfs] AHCI port ");
            serial_print_dec((uint32_t)p);
            serial_print(" selected for disk I/O\n");
            return 1;
        }
    }
    return 0;
}

/* ===================================================================
 * NVMe adapter — translates VernisFS sector I/O to NVMe NVM commands
 * =================================================================== */

static int nvme_sector_read(uint32_t lba, uint8_t count, uint8_t *buf) {
    uint8_t done = 0;
    while (done < count) {
        uint8_t chunk = (uint8_t)((count - done > 8) ? 8 : (count - done));
        int rc = kernel_nvme_read((uint64_t)lba + done,
                                  (uint32_t)chunk, buf + done * 512,
                                  (uint32_t)chunk * 512u);
        if (rc < 0) return -1;
        done += chunk;
    }
    return (int)count;
}

static int nvme_sector_write(uint32_t lba, uint8_t count, const uint8_t *buf) {
    uint8_t done = 0;
    while (done < count) {
        uint8_t chunk = (uint8_t)((count - done > 8) ? 8 : (count - done));
        int rc = kernel_nvme_write((uint64_t)lba + done,
                                   (uint32_t)chunk, buf + done * 512,
                                   (uint32_t)chunk * 512u);
        if (rc < 0) return -1;
        done += chunk;
    }
    return (int)count;
}

static int kfs_try_nvme(void) {
    if (!kernel_nvme_available()) return 0;
    if (!kernel_nvme_identified()) return 0;
    vfs_set_disk_ops(nvme_sector_read, nvme_sector_write);
    serial_print("[kfs] NVMe selected for disk I/O\n");
    return 1;
}

bool kfs_init(void) {
    /* Priority: NVMe > AHCI > ATA PIO */
    int nvme = kfs_try_nvme();
    int ahci = 0;
    if (!nvme) ahci = kfs_try_ahci();

    if (vfs_init()) {
        g_backend = nvme ? KFS_BACKEND_NVME :
                   ahci ? KFS_BACKEND_AHCI : KFS_BACKEND_VERNISFS;
        g_ops = &KFS_OPS_VERNISFS;
        serial_print("[kfs] mounted backend: ");
        serial_print(kfs_backend_name());
        serial_print("\n");
        return true;
    }

    g_backend = KFS_BACKEND_NONE;
    g_ops = (const KfsOps *)0;
    serial_print("[kfs] no filesystem backend mounted\n");
    return false;
}

bool kfs_ready(void) {
    return g_ops != (const KfsOps *)0;
}

KfsBackend kfs_backend(void) {
    return g_backend;
}

const char *kfs_backend_name(void) {
    if (g_backend == KFS_BACKEND_NVME) return "vernisfs-nvme";
    if (g_backend == KFS_BACKEND_AHCI) return "vernisfs-ahci";
    if (g_backend == KFS_BACKEND_VERNISFS) return "vernisfs";
    return "none";
}

const VfsFileEntry *kfs_find_file(const char *path) {
    const VfsFileEntry *pseudo = kfs_find_pseudo(path);
    if (pseudo) return pseudo;
    return g_ops ? g_ops->find_file(path) : (const VfsFileEntry *)0;
}

int kfs_read_file(const char *path, uint8_t *buf, size_t max_len) {
    if (!path) return -1;
    if (kfs_streq(path, "/dev/null")) return 0;
    if (kfs_streq(path, "/dev/zero")) {
        for (size_t i = 0; i < max_len; i++) buf[i] = 0;
        return (int)max_len;
    }
    if (kfs_streq(path, "/proc/uptime")) return kfs_read_proc_uptime(buf, max_len);
    if (kfs_streq(path, "/proc/ps")) return kfs_read_proc_ps(buf, max_len);
    if (kfs_streq(path, "/proc/fs")) return kfs_read_proc_fs(buf, max_len);
    if (kfs_streq(path, "/proc") || kfs_streq(path, "/dev")) return -1;
    return g_ops ? g_ops->read_file(path, buf, max_len) : -1;
}

int kfs_write_file(const char *path, const uint8_t *data, size_t len) {
    (void)data;
    if (!path) return -1;
    if (kfs_streq(path, "/dev/null") || kfs_streq(path, "/dev/zero")) return (int)len;
    if (kfs_path_is_pseudo(path)) return -1;
    return g_ops ? g_ops->write_file(path, data, len) : -1;
}

int kfs_append_file(const char *path, const uint8_t *data, size_t len) {
    (void)data;
    if (!path) return -1;
    if (kfs_streq(path, "/dev/null") || kfs_streq(path, "/dev/zero")) return (int)len;
    if (kfs_path_is_pseudo(path)) return -1;
    return g_ops ? g_ops->append_file(path, data, len) : -1;
}

int kfs_mkdir(const char *path) {
    if (kfs_path_is_pseudo(path)) return -1;
    return g_ops ? g_ops->mkdir_fn(path) : -1;
}

int kfs_delete_file(const char *path) {
    if (kfs_path_is_pseudo(path)) return -1;
    return g_ops ? g_ops->delete_file(path) : -1;
}

int kfs_list_dir(const char *dir_path, char out[][VFS_MAX_FILENAME], int max) {
    if (!dir_path || !out || max <= 0) return -1;

    int count = 0;
    if (g_ops) {
        int base = g_ops->list_dir(dir_path, out, max);
        if (base > 0) count = base;
    }

    if (kfs_streq(dir_path, "/")) {
        count = kfs_list_dir_add(out, max, count, "/proc");
        count = kfs_list_dir_add(out, max, count, "/dev");
        return count;
    }
    if (kfs_streq(dir_path, "/proc")) {
        count = kfs_list_dir_add(out, max, count, "/proc/uptime");
        count = kfs_list_dir_add(out, max, count, "/proc/ps");
        count = kfs_list_dir_add(out, max, count, "/proc/fs");
        return count;
    }
    if (kfs_streq(dir_path, "/dev")) {
        count = kfs_list_dir_add(out, max, count, "/dev/null");
        count = kfs_list_dir_add(out, max, count, "/dev/zero");
        return count;
    }

    return count;
}

uint16_t kfs_file_count(void) {
    return g_ops ? g_ops->file_count() : 0;
}

// =============================================================================
// Phase 47: File permissions
// =============================================================================

int kfs_chmod(const char *path, uint16_t mode) {
    if (!path || kfs_path_is_pseudo(path)) return -1;
    return vfs_chmod(path, mode);
}

int kfs_chown(const char *path, uint16_t uid, uint16_t gid) {
    if (!path || kfs_path_is_pseudo(path)) return -1;
    return vfs_chown(path, uid, gid);
}

int kfs_check_perm(const char *path, uint16_t uid, char op) {
    if (!path) return -1;

    // Superuser bypasses all checks
    if (uid == 0) return 0;

    const VfsFileEntry *entry = kfs_find_file(path);
    if (!entry) return -1;

    uint16_t mode = entry->mode;
    // Backward compat: mode==0 means legacy file, treat as permissive
    if (mode == 0) return 0;

    uint16_t bits;
    if (uid == entry->uid) {
        // Owner bits
        bits = (mode >> 6) & 7;
    } else if (uid == entry->gid) {
        // Group bits (simple: gid == uid match)
        bits = (mode >> 3) & 7;
    } else {
        // Other bits
        bits = mode & 7;
    }

    switch (op) {
        case 'r': return (bits & 4) ? 0 : -1;
        case 'w': return (bits & 2) ? 0 : -1;
        case 'x': return (bits & 1) ? 0 : -1;
        default:  return -1;
    }
}
