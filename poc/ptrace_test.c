#include <stdio.h> // printf
#include <sys/ptrace.h> // ptrace
#include <sys/types.h> // pid_t
#include <sys/wait.h> // wait
#include <unistd.h> // fork and execvp
#include <sys/user.h>

int main() {

    pid_t child = fork();

    if (child == 0) {
        // fork() return 0 to child

        // tells the parent to TRACEME
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        char *args[] = {"ls", NULL};
        // Child runs "ls"
        execvp("ls", args);

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
            long syscall_num = regs.orig_rax;
            printf("syscall: %ld\n", syscall_num);
            }
            entering = !entering;
            ptrace(PTRACE_SYSCALL, child, 0, 0);
        }
    }
}