// dylib.c — minimal shared-library loader over module loader

#include "dylib.h"
#include "module.h"
#include "vfs.h"
#include <stddef.h>

typedef struct {
    uint8_t  used;
    uint32_t handle;
    uint32_t mid;
    uint32_t size;
    char     name[DYLIB_NAME_MAX];
    char     path[DYLIB_PATH_MAX];
} DylibSlot;

static DylibSlot g_dylibs[DYLIB_MAX_LIBS];
static uint32_t  g_next_handle = 1;
static uint8_t   g_dylib_storage[DYLIB_MAX_LIBS][DYLIB_MAX_FILE_SIZE];

extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);

static void d_memset(void *dst, uint8_t val, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

static void d_strncpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (!dst || max == 0) return;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int32_t dylib_find_handle(uint32_t handle) {
    for (int i = 0; i < DYLIB_MAX_LIBS; i++) {
        if (g_dylibs[i].used && g_dylibs[i].handle == handle) return i;
    }
    return -1;
}

static int32_t dylib_symbol_to_fnid(const char *symbol) {
    if (!symbol || symbol[0] != 'f' || symbol[1] != 'n') return DYLIB_ERR_NOTFOUND;
    int id = 0;
    int i = 2;
    if (!symbol[i]) return DYLIB_ERR_NOTFOUND;
    while (symbol[i] >= '0' && symbol[i] <= '9') {
        id = id * 10 + (symbol[i] - '0');
        i++;
    }
    if (symbol[i] != '\0') return DYLIB_ERR_NOTFOUND;
    if (id < 0 || id >= MOD_MAX_EXPORTS) return DYLIB_ERR_NOTFOUND;
    return id;
}

void dylib_init(void) {
    d_memset(g_dylibs, 0, sizeof(g_dylibs));
    d_memset(g_dylib_storage, 0, sizeof(g_dylib_storage));
    g_next_handle = 1;
    serial_print("[dylib] initialized\n");
}

int32_t dylib_open(const char *path, const char *name) {
    if (!path || !path[0]) return DYLIB_ERR_INVAL;

    int32_t free_slot = -1;
    for (int i = 0; i < DYLIB_MAX_LIBS; i++) {
        if (!g_dylibs[i].used) { free_slot = i; break; }
    }
    if (free_slot < 0) return DYLIB_ERR_NOSLOT;

    int n = kfs_read_file(path, g_dylib_storage[free_slot], DYLIB_MAX_FILE_SIZE);
    if (n <= 0) return DYLIB_ERR_IO;
    if (n < (int)sizeof(ModHeader)) return DYLIB_ERR_FORMAT;

    uint32_t addr = (uint32_t)(uintptr_t)g_dylib_storage[free_slot];
    int32_t mid = module_load(addr, (uint32_t)n);
    if (mid < 0) return mid;

    DylibSlot *s = &g_dylibs[free_slot];
    s->used = 1;
    s->handle = g_next_handle++;
    s->mid = (uint32_t)mid;
    s->size = (uint32_t)n;
    d_strncpy(s->path, path, DYLIB_PATH_MAX);

    if (name && name[0]) {
        d_strncpy(s->name, name, DYLIB_NAME_MAX);
    } else {
        ModInfo info;
        if (module_info((uint32_t)mid, &info) == MOD_OK) {
            d_strncpy(s->name, info.name, DYLIB_NAME_MAX);
        } else {
            d_strncpy(s->name, "lib", DYLIB_NAME_MAX);
        }
    }

    return (int32_t)s->handle;
}

int32_t dylib_close(uint32_t handle) {
    int32_t idx = dylib_find_handle(handle);
    if (idx < 0) return DYLIB_ERR_NOTFOUND;

    module_unload(g_dylibs[idx].mid);
    d_memset(&g_dylibs[idx], 0, sizeof(DylibSlot));
    d_memset(g_dylib_storage[idx], 0, DYLIB_MAX_FILE_SIZE);
    return DYLIB_OK;
}

int32_t dylib_list(void) {
    int32_t count = 0;
    serial_print("[dylib] loaded libs:\n");
    for (int i = 0; i < DYLIB_MAX_LIBS; i++) {
        if (!g_dylibs[i].used) continue;
        serial_print("  h=");
        serial_print_dec(g_dylibs[i].handle);
        serial_print(" name=");
        serial_print(g_dylibs[i].name);
        serial_print(" path=");
        serial_print(g_dylibs[i].path);
        serial_print("\n");
        count++;
    }
    if (!count) serial_print("  (none)\n");
    return count;
}

int32_t dylib_resolve(uint32_t handle, const char *symbol) {
    int32_t idx = dylib_find_handle(handle);
    if (idx < 0) return DYLIB_ERR_NOTFOUND;

    ModInfo info;
    if (module_info(g_dylibs[idx].mid, &info) != MOD_OK) return DYLIB_ERR_NOTFOUND;

    int32_t fn_id = dylib_symbol_to_fnid(symbol);
    if (fn_id < 0) return fn_id;
    if ((uint32_t)fn_id >= info.fn_count) return DYLIB_ERR_NOTFOUND;
    return fn_id;
}

int32_t dylib_call(uint32_t handle, const char *symbol, uint32_t arg) {
    int32_t idx = dylib_find_handle(handle);
    if (idx < 0) return DYLIB_ERR_NOTFOUND;

    int32_t fn_id = dylib_resolve(handle, symbol);
    if (fn_id < 0) return fn_id;
    return module_call(g_dylibs[idx].mid, (uint32_t)fn_id, arg);
}
