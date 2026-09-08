#include "mlrt.h"
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  mlrt trace run [--steps N] [--peek ADDRESS] -- PROGRAM [ARGS...]\n"
            "  mlrt trace attach PID [--steps N] [--peek ADDRESS]\n");
}

static void print_regs(pid_t pid) {
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) < 0) {
        fprintf(stderr, "ptrace(GETREGS): %s\n", strerror(errno));
        return;
    }
#if defined(__x86_64__)
    printf("RIP=0x%llx RSP=0x%llx RBP=0x%llx RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx\n",
           (unsigned long long)regs.rip, (unsigned long long)regs.rsp,
           (unsigned long long)regs.rbp, (unsigned long long)regs.rax,
           (unsigned long long)regs.rbx, (unsigned long long)regs.rcx,
           (unsigned long long)regs.rdx);
#elif defined(__i386__)
    printf("EIP=0x%lx ESP=0x%lx EBP=0x%lx EAX=0x%lx EBX=0x%lx ECX=0x%lx EDX=0x%lx\n",
           regs.eip, regs.esp, regs.ebp, regs.eax, regs.ebx, regs.ecx, regs.edx);
#else
    printf("register display is implemented for x86/x86-64 only\n");
#endif
}

static void peek_word(pid_t pid, unsigned long long address) {
    errno = 0;
    long value = ptrace(PTRACE_PEEKDATA, pid, (void *)(uintptr_t)address, NULL);
    if (value == -1 && errno) {
        fprintf(stderr, "ptrace(PEEKDATA 0x%llx): %s\n", address, strerror(errno));
        return;
    }
    printf("memory[0x%llx] = 0x%0*lx\n", address, (int)(sizeof(long) * 2), (unsigned long)value);
}

static int trace_loop(pid_t pid, long steps, int have_peek, unsigned long long peek_addr, int attached) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return 1;
    }
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "tracee did not stop as expected\n");
        return 1;
    }

    if (ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)(uintptr_t)(PTRACE_O_EXITKILL | PTRACE_O_TRACEEXEC)) < 0) {
        fprintf(stderr, "ptrace(SETOPTIONS): %s\n", strerror(errno));
    }
    printf("tracee %d stopped by signal %d (%s)\n", (int)pid, WSTOPSIG(status), strsignal(WSTOPSIG(status)));
    print_regs(pid);
    if (have_peek) peek_word(pid, peek_addr);

    for (long i = 0; i < steps; ++i) {
        if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) < 0) {
            fprintf(stderr, "ptrace(SINGLESTEP): %s\n", strerror(errno));
            break;
        }
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            break;
        }
        if (WIFEXITED(status)) {
            printf("tracee exited with status %d\n", WEXITSTATUS(status));
            return 0;
        }
        if (WIFSIGNALED(status)) {
            printf("tracee terminated by signal %d (%s)\n", WTERMSIG(status), strsignal(WTERMSIG(status)));
            return 0;
        }
        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            printf("step %ld: stopped by signal %d (%s)\n", i + 1, sig, strsignal(sig));
            print_regs(pid);
            if (have_peek) peek_word(pid, peek_addr);
        }
    }

    if (attached) {
        if (ptrace(PTRACE_DETACH, pid, NULL, NULL) < 0) {
            fprintf(stderr, "ptrace(DETACH): %s\n", strerror(errno));
            return 1;
        }
        printf("detached from %d\n", (int)pid);
        return 0;
    }

    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        fprintf(stderr, "ptrace(CONT): %s\n", strerror(errno));
        return 1;
    }
    for (;;) {
        if (waitpid(pid, &status, 0) < 0) {
            if (errno == EINTR) continue;
            perror("waitpid");
            return 1;
        }
        if (WIFEXITED(status)) {
            printf("tracee exited with status %d\n", WEXITSTATUS(status));
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            printf("tracee terminated by signal %d (%s)\n", WTERMSIG(status), strsignal(WTERMSIG(status)));
            return 128 + WTERMSIG(status);
        }
        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            unsigned event = (unsigned)status >> 16;
            if (event == PTRACE_EVENT_EXEC) {
                printf("trace event: exec\n");
                print_regs(pid);
                sig = 0;
            } else {
                printf("tracee received signal %d (%s)\n", sig, strsignal(sig));
            }
            if (ptrace(PTRACE_CONT, pid, NULL, (void *)(uintptr_t)sig) < 0) {
                fprintf(stderr, "ptrace(CONT): %s\n", strerror(errno));
                return 1;
            }
        }
    }
}

int tracer_cli_main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    long steps = 0;
    int have_peek = 0;
    unsigned long long peek_addr = 0;

    if (strcmp(argv[1], "attach") == 0) {
        if (argc < 3) { usage(); return 2; }
        char *end = NULL;
        long parsed = strtol(argv[2], &end, 10);
        if (!end || *end || parsed <= 0) { fprintf(stderr, "invalid PID\n"); return 2; }
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) steps = strtol(argv[++i], NULL, 10);
            else if (strcmp(argv[i], "--peek") == 0 && i + 1 < argc) {
                if (mlrt_parse_u64(argv[++i], &peek_addr) != 0) return 2;
                have_peek = 1;
            } else { usage(); return 2; }
        }
        pid_t pid = (pid_t)parsed;
        if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
            fprintf(stderr, "ptrace(ATTACH %d): %s\n", (int)pid, strerror(errno));
            return 1;
        }
        return trace_loop(pid, steps, have_peek, peek_addr, 1);
    }

    if (strcmp(argv[1], "run") != 0) { usage(); return 2; }
    int sep = -1;
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) { sep = i; break; }
        if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) steps = strtol(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--peek") == 0 && i + 1 < argc) {
            if (mlrt_parse_u64(argv[++i], &peek_addr) != 0) return 2;
            have_peek = 1;
        } else { usage(); return 2; }
    }
    if (sep < 0 || sep + 1 >= argc) { usage(); return 2; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            perror("ptrace(TRACEME)");
            _exit(126);
        }
        raise(SIGSTOP);
        execvp(argv[sep + 1], &argv[sep + 1]);
        perror(argv[sep + 1]);
        _exit(errno == ENOENT ? 127 : 126);
    }
    return trace_loop(pid, steps, have_peek, peek_addr, 0);
}
