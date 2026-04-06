/* VernisOS User-Space Syscall Interface */
#ifndef VERNIS_SYSCALL_H
#define VERNIS_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* Syscall numbers — must match kernel definitions */
#define SYS_EXIT      60
#define SYS_WAITPID   61
#define SYS_GETPID    62
#define SYS_KILL      63
#define SYS_OPEN      66
#define SYS_READ_FD   67
#define SYS_WRITE_FD  68
#define SYS_CLOSE     69
#define SYS_DUP       70
#define SYS_DUP2      71
#define SYS_PIPE      72
#define SYS_FORK      73
#define SYS_EXECVE    74
#define SYS_SBRK      75

/* ---- Architecture-specific syscall inline ---- */

#ifdef __x86_64__

static inline int64_t _syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

#else /* i686 */

static inline int32_t _syscall3(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

#endif

/* ---- POSIX-like wrappers ---- */

static inline void _exit(int code) {
    _syscall3(SYS_EXIT, (size_t)code, 0, 0);
    __builtin_unreachable();
}

static inline int getpid(void) {
    return (int)_syscall3(SYS_GETPID, 0, 0, 0);
}

static inline int fork(void) {
    return (int)_syscall3(SYS_FORK, 0, 0, 0);
}

static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)argv; (void)envp;
    return (int)_syscall3(SYS_EXECVE, (size_t)path, 0, 0);
}

static inline int open(const char *path, int flags) {
    return (int)_syscall3(SYS_OPEN, (size_t)path, (size_t)flags, 0);
}

static inline int read(int fd, void *buf, size_t count) {
    return (int)_syscall3(SYS_READ_FD, (size_t)fd, (size_t)buf, (size_t)count);
}

static inline int write(int fd, const void *buf, size_t count) {
    return (int)_syscall3(SYS_WRITE_FD, (size_t)fd, (size_t)buf, (size_t)count);
}

static inline int close(int fd) {
    return (int)_syscall3(SYS_CLOSE, (size_t)fd, 0, 0);
}

static inline int dup(int oldfd) {
    return (int)_syscall3(SYS_DUP, (size_t)oldfd, 0, 0);
}

static inline int dup2(int oldfd, int newfd) {
    return (int)_syscall3(SYS_DUP2, (size_t)oldfd, (size_t)newfd, 0);
}

static inline int pipe(int fds[2]) {
    return (int)_syscall3(SYS_PIPE, (size_t)fds, 0, 0);
}

static inline int waitpid(int pid) {
    return (int)_syscall3(SYS_WAITPID, (size_t)pid, 0, 0);
}

static inline int kill(int pid, int sig) {
    return (int)_syscall3(SYS_KILL, (size_t)pid, (size_t)sig, 0);
}

static inline void *sbrk(int increment) {
    return (void *)_syscall3(SYS_SBRK, (size_t)increment, 0, 0);
}

/* Phase 46: mmap / munmap */
#define SYS_MMAP      76
#define SYS_MUNMAP    77

#define PROT_READ    0x01
#define PROT_WRITE   0x02
#define PROT_EXEC    0x04
#define MAP_ANONYMOUS 0x10
#define MAP_PRIVATE   0x20

/* mmap(length, prot, flags, path) — path=NULL for anonymous */
static inline void *mmap(size_t length, int prot, int flags, const char *path) {
    size_t prot_flags = ((size_t)flags << 8) | ((size_t)prot & 0xFF);
    return (void *)_syscall3(SYS_MMAP, length, prot_flags, (size_t)path);
}

static inline int munmap(void *addr, size_t length) {
    return (int)_syscall3(SYS_MUNMAP, (size_t)addr, length, 0);
}

#endif /* VERNIS_SYSCALL_H */
