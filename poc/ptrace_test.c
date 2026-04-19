#include <stdio.h> // printf
#include <sys/ptrace.h> // ptrace
#include <sys/types.h> // pid_t
#include <sys/wait.h> // wait
#include <unistd.h> // fork and execvp
#include <sys/user.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_EXECVE 59
#define SYS_OPENAT 257


const char *noise_prefixes[] = {
    "/proc/self/stat",
    "/proc/self/maps",
    "/proc/self/cgroup",
    "/sys/fs/cgroup",
    "/sys/devices/system/cpu",
    "/dev/urandom",
    "/proc/self/maps",
    NULL
};

int is_noise(const char *path) {
    for (int i = 0; noise_prefixes[i] != NULL; i++) {
        if (strncmp(path, noise_prefixes[i], strlen(noise_prefixes[i])) == 0) return 1;
    }
    return 0;
}
void read_string_from_child(pid_t pid, long addr, char *buf, int max) {
    int i = 0;
    int found_null = 0;
    while (i < max - 1) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, addr + i, 0);

        if (errno != 0) break;
        memcpy(buf + i, &word, sizeof(long));

        char *chunk = (char *)&word;
       
        for (int j = 0; j < sizeof(long); j++) {
            if (chunk[j] == '\0') {
                found_null = 1;
                break;
            }
        }
        if (found_null) break;
        i += sizeof(long);
    }
    if (!found_null) buf[i] = '\0';
}

void handle_openat(pid_t pid, struct user_regs_struct *regs) {
    char path[512];
    read_string_from_child(pid, regs->rsi, path, 512);
    if (path[0] == '\0') return;
    if (is_noise(path)) return;
    printf("[OPEN] %s\n", path);
}

void handle_execve(pid_t pid, struct user_regs_struct *regs) {
    char path[512] = {0};
    read_string_from_child(pid, regs->rdi, path, 512);
    if (path[0] == '\0') return;
    printf("[EXEC] %s\n", path);

    long argv_addr = regs->rsi;
    int i = 1;
    while (1) {
        errno = 0;
        long arg_ptr = ptrace(PTRACE_PEEKDATA, pid, argv_addr + i * sizeof(long), 0);
        if (errno != 0 || arg_ptr == 0) break;

        char arg[256] = {0};
        read_string_from_child(pid, arg_ptr, arg, 256);
        printf(" %s", arg);
        i++;
    }
    printf("\n");
}

void dispatch_syscall(pid_t pid, struct user_regs_struct *regs) {
    long syscall_num = regs->orig_rax;

    if (syscall_num == SYS_OPENAT || syscall_num == SYS_OPEN) {
        handle_openat(pid, regs);
    } else if (syscall_num == SYS_EXECVE) {
        handle_execve(pid, regs);
    }
}

int main() {

    char query[512];
    printf("Enter query for Claude: ");
    fgets(query, sizeof(query), stdin);
    query[strcspn(query, "\n")] = 0;

    pid_t child = fork();

    if (child == 0) {

        // tells the parent to TRACEME
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        char *args[] = {"/usr/local/bin/claude", "-p", query, NULL};
        // Child runs 
        setenv("ANTHROPIC_API_KEY", "api key", 1);
        printf("child: launching claude\n");
        execvp("/usr/local/bin/claude", args);
        perror("execvp failed");

    } else {
        // parent
        int status;
        int entering = 1;
        while (1) {
            wait(&status); // wait for child to stop
            if (WIFEXITED(status)) break;
            if (entering) {
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, child, 0, &regs);
            dispatch_syscall(child, &regs);
            }
            entering = !entering;
            ptrace(PTRACE_SYSCALL, child, 0, 0);
        }
    }
}