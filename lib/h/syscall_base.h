#ifndef VERNISOS_SYSCALL_H
#define VERNISOS_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

// FFI functions
ssize_t syscall_handler(uint32_t sys_num, size_t arg1, size_t arg2, size_t arg3);
ssize_t ffi_sys_write(const uint8_t* ptr, size_t len);
void ffi_sys_exit(int32_t code);
ssize_t ffi_sys_getpid(void);
void ffi_sys_dump_registers(void);
void ffi_sys_dump_scheduler(void);
void ffi_sys_dump_memory(size_t addr);
void ffi_sys_dump_syscalls(void);
void ffi_sys_dump_all(void);

#ifdef __cplusplus
}
#endif

#endif // VERNISOS_SYSCALL_H 