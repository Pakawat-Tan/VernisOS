/* VernisOS User Shell (vsh) */
#include <stddef.h>
#include "syscall.h"
#include "libc.h"

#define VSH_LINE_MAX 256
#define VSH_ARG_MAX   16

static int has_char(const char *s, char c) {
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == c) return 1;
    }
    return 0;
}

static void strip_trailing_newline(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static size_t strnlen_local(const char *s, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (dst_sz == 0) return;
    size_t i = 0;
    while (i + 1 < dst_sz && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void safe_append(char *dst, size_t dst_sz, const char *src) {
    size_t n = strnlen_local(dst, dst_sz);
    if (n >= dst_sz) return;
    size_t i = 0;
    while ((n + i + 1) < dst_sz && src[i]) {
        dst[n + i] = src[i];
        i++;
    }
    dst[n + i] = '\0';
}

static int parse_argv(char *line, char *argv[], int max_argv) {
    int argc = 0;
    char *p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (argc >= max_argv - 1) break;
        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    argv[argc] = (char *)0;
    return argc;
}

static void resolve_exec_path(char *out, size_t out_sz, const char *cwd, const char *cmd) {
    if (cmd[0] == '/') {
        safe_copy(out, out_sz, cmd);
        return;
    }

    if (!has_char(cmd, '/')) {
        safe_copy(out, out_sz, "/bin/");
        safe_append(out, out_sz, cmd);
        return;
    }

    safe_copy(out, out_sz, cwd);
    if (out[0] && out[strlen(out) - 1] != '/') {
        safe_append(out, out_sz, "/");
    }
    safe_append(out, out_sz, cmd);
}

static void set_cwd(char *cwd, size_t cwd_sz, const char *path) {
    if (!path || !path[0]) return;
    if (path[0] == '/') {
        safe_copy(cwd, cwd_sz, path);
        return;
    }
    if (strcmp(path, ".") == 0) return;
    if (strcmp(path, "..") == 0) {
        size_t n = strlen(cwd);
        if (n <= 1) {
            safe_copy(cwd, cwd_sz, "/");
            return;
        }
        while (n > 0 && cwd[n - 1] == '/') { cwd[n - 1] = '\0'; n--; }
        while (n > 0 && cwd[n - 1] != '/') { cwd[n - 1] = '\0'; n--; }
        if (n == 0) safe_copy(cwd, cwd_sz, "/");
        return;
    }
    if (cwd[0] && cwd[strlen(cwd) - 1] != '/') safe_append(cwd, cwd_sz, "/");
    safe_append(cwd, cwd_sz, path);
}

int main(void) {
    char line[VSH_LINE_MAX];
    char cwd[64];
    safe_copy(cwd, sizeof(cwd), "/");

    puts("VernisOS vsh (user mode)");
    puts("type 'exit' to quit");

    for (;;) {
        write(1, cwd, strlen(cwd));
        write(1, " $ ", 3);

        int n = 0;
        while (n <= 0) {
            // TTY read is currently non-blocking in kernel; wait until a line arrives.
            n = read(0, line, sizeof(line) - 1);
        }
        line[n] = '\0';
        strip_trailing_newline(line);
        if (!line[0]) continue;

        char *argv[VSH_ARG_MAX];
        int argc = parse_argv(line, argv, VSH_ARG_MAX);
        if (argc <= 0) continue;

        if (strcmp(argv[0], "exit") == 0) {
            break;
        }

        if (strcmp(argv[0], "cd") == 0) {
            if (argc < 2) {
                puts("cd: missing operand");
                continue;
            }
            set_cwd(cwd, sizeof(cwd), argv[1]);
            continue;
        }

        if (strcmp(argv[0], "help") == 0) {
            puts("builtins: help, cd, exit");
            puts("external: command -> /bin/command");
            continue;
        }

        char exec_path[64];
        resolve_exec_path(exec_path, sizeof(exec_path), cwd, argv[0]);

        int pid = fork();
        if (pid < 0) {
            puts("fork failed");
            continue;
        }
        if (pid == 0) {
            execve(exec_path, argv, (char *const *)0);
            puts("execve failed");
            _exit(127);
        }

        (void)waitpid(pid);
    }

    return 0;
}