// Agent Flight Recorder — minimal ptrace tracer.
//
// Forks the Claude CLI under ptrace, watches for openat / execve / signals,
// and prints them to stdout as plain text. The Qt UI reads this stream.
//
// Build: gcc -O2 -Wall recorder.c -o recorder
// Run:   ANTHROPIC_API_KEY=sk-... ./recorder "your query here"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>

// Paths we don't care about — runtime internals every process touches.
const char *noise_prefixes[] = {
    "/proc/self/",
    "/sys/fs/cgroup",
    "/sys/devices/system/cpu",
    "/dev/urandom",
    NULL
};

int is_noise(const char *path) {
    for (int i = 0; noise_prefixes[i] != NULL; i++) {
        if (strncmp(path, noise_prefixes[i], strlen(noise_prefixes[i])) == 0)
            return 1;
    }
    return 0;
}

// Read a null-terminated string out of the child process's memory.
// We use this for syscall arguments like paths and argv strings.
void read_string_from_child(pid_t pid, long addr, char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, pid, addr + i, 0);
        if (errno != 0) break;
        memcpy(buf + i, &word, sizeof(long));
        for (int j = 0; j < (int)sizeof(long); j++) {
            if (((char *)&word)[j] == '\0') {
                buf[i + j] = '\0';
                return;
            }
        }
        i += sizeof(long);
    }
    buf[i] = '\0';
}

// openat(dirfd, pathname, flags) — pathname pointer is in rsi.
void handle_openat(pid_t pid, struct user_regs_struct *regs) {
    char path[512];
    read_string_from_child(pid, regs->rsi, path, 512);
    if (path[0] == '\0' || is_noise(path)) return;
    printf("[OPEN] %s\n", path);
    fflush(stdout);
}

// execve(filename, argv, envp) — filename in rdi, argv array in rsi.
void handle_execve(pid_t pid, struct user_regs_struct *regs) {
    char path[512];
    read_string_from_child(pid, regs->rdi, path, 512);
    if (path[0] == '\0') return;
    printf("[EXEC] %s", path);

    // Walk argv until we hit a NULL pointer (or hit a sane upper bound).
    long argv_addr = regs->rsi;
    for (int i = 1; i < 32; i++) {
        errno = 0;
        long arg_ptr = ptrace(PTRACE_PEEKDATA, pid,
                              argv_addr + i * sizeof(long), 0);
        if (errno != 0 || arg_ptr == 0) break;
        char arg[256];
        read_string_from_child(pid, arg_ptr, arg, 256);
        printf(" %s", arg);
    }
    printf("\n");
    fflush(stdout);
}

void handle_syscall(pid_t pid, struct user_regs_struct *regs) {
    long n = regs->orig_rax;
    if (n == SYS_openat)      handle_openat(pid, regs);
    else if (n == SYS_execve) handle_execve(pid, regs);
}

// Map signal numbers to names so [SIGNAL] events are readable.
const char *signal_name(int sig) {
    switch (sig) {
        case SIGSTOP: return "SIGSTOP";
        case SIGTSTP: return "SIGTSTP";
        case SIGTTIN: return "SIGTTIN";
        case SIGTTOU: return "SIGTTOU";
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGKILL: return "SIGKILL";
        case SIGTERM: return "SIGTERM";
        case SIGINT:  return "SIGINT";
        case SIGCHLD: return "SIGCHLD";
        default:      return "OTHER";
    }
}

// We trace Claude and every process it spawns. Each process has its own
// syscall-entry/exit toggle (entry has args we want, exit doesn't), so we
// track that here. 256 is plenty — real sessions stay well under 50.
#define MAX_PROCS 256
struct { pid_t pid; int entering; } procs[MAX_PROCS];
int proc_count = 0;

int *get_entering(pid_t pid) {
    for (int i = 0; i < proc_count; i++) {
        if (procs[i].pid == pid) return &procs[i].entering;
    }
    if (proc_count < MAX_PROCS) {
        procs[proc_count].pid = pid;
        procs[proc_count].entering = 1;
        return &procs[proc_count++].entering;
    }
    return NULL;  // out of slots — shouldn't happen in practice
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s \"your query here\"\n", argv[0]);
        return 1;
    }
    const char *query = argv[1];

    pid_t child = fork();
    if (child == 0) {
        // Child: ask to be traced, then become claude.
        // ANTHROPIC_API_KEY is inherited from the parent's environment —
        // set it in your shell, never hardcode it here.
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        char *args[] = { "claude", "-p", (char *)query, NULL };
        execvp("claude", args);
        perror("execvp");
        return 1;
    }

    // Parent: wait for the child's first stop (caused by PTRACE_TRACEME).
    int status;
    waitpid(child, &status, 0);

    // Tell the kernel:
    //  - also trace any process the agent spawns (CLONE/FORK/VFORK)
    //  - tag syscall stops with bit 0x80 so we can distinguish them
    //    from real signal stops
    ptrace(PTRACE_SETOPTIONS, child, 0,
           PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
           PTRACE_O_TRACEEXEC  | PTRACE_O_TRACESYSGOOD);
    ptrace(PTRACE_SYSCALL, child, 0, 0);

    // The main loop: wait for a stop event from any tracee and handle it.
    while (1) {
        pid_t stopped = waitpid(-1, &status, __WALL);
        if (stopped < 0) break;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (stopped == child) break;   // claude itself exited
            continue;                       // a subprocess exited; keep going
        }

        int sig = WSTOPSIG(status);

        if (sig == (SIGTRAP | 0x80)) {
            // Syscall-stop. Log the syscall on entry, then toggle the flag
            // so we skip the matching exit-stop.
            int *entering = get_entering(stopped);
            if (entering && *entering) {
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, stopped, 0, &regs) == 0) {
                    handle_syscall(stopped, &regs);
                }
            }
            if (entering) *entering = !*entering;
            ptrace(PTRACE_SYSCALL, stopped, 0, 0);
        } else if (sig == SIGTRAP) {
            // Ptrace event (fork/clone/exec). Just continue tracing.
            ptrace(PTRACE_SYSCALL, stopped, 0, 0);
        } else {
            // A real signal was delivered to the tracee. Log it,
            // then forward it so the tracee actually receives it.
            printf("[SIGNAL] %s pid=%d\n", signal_name(sig), stopped);
            fflush(stdout);
            ptrace(PTRACE_SYSCALL, stopped, 0, sig);
        }
    }
    return 0;
}
