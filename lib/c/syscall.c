#include "../h/syscall.h"
#include <stdio.h>
#include <string.h>

int main() {
    const char* msg = "Hello from C via Rust syscall!\n";
    ssize_t written = ffi_sys_write((const uint8_t*)msg, strlen(msg));
    printf("ffi_sys_write returned: %zd\n", written);
    ssize_t pid = ffi_sys_getpid();
    printf("Current PID: %zd\n", pid);
    ffi_sys_dump_all();
    return 0;
} 