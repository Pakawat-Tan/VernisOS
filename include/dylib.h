#ifndef VERNISOS_DYLIB_H
#define VERNISOS_DYLIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DYLIB_MAX_LIBS      4
#define DYLIB_NAME_MAX      24
#define DYLIB_PATH_MAX      48
#define DYLIB_MAX_FILE_SIZE 8192

#define DYLIB_OK            0
#define DYLIB_ERR_INVAL    (-1)
#define DYLIB_ERR_NOSLOT   (-2)
#define DYLIB_ERR_NOTFOUND (-3)
#define DYLIB_ERR_IO       (-4)
#define DYLIB_ERR_FORMAT   (-5)

void    dylib_init(void);
int32_t dylib_open(const char *path, const char *name);
int32_t dylib_close(uint32_t handle);
int32_t dylib_list(void);
int32_t dylib_resolve(uint32_t handle, const char *symbol);
int32_t dylib_call(uint32_t handle, const char *symbol, uint32_t arg);

#ifdef __cplusplus
}
#endif

#endif // VERNISOS_DYLIB_H
