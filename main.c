#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <syscall.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reg.h>
#include <sys/user.h>
#include <unistd.h>
#include <errno.h>

#include "debuglib.h"


void run_debugger(pid_t child_pid)
{
    procmsg("debugger started\n");

    /* Wait for child to stop on its first instruction */
    wait(0);
    procmsg("child now at RIP = %.16p\n", get_child_eip(child_pid));

    debug_breakpoint* bps[64];
    int bps_size = 0;

    while (1) {
        // prompt
        procmsg("(mygdb) ");
        // debug command
        char cmd[64];
        scanf("%s", cmd);
        if (strcmp(cmd, "b") == 0) {
            // set break point
            void* addr;
            scanf("%p", &addr);
            debug_breakpoint* bp = create_breakpoint(child_pid, addr);
            assert(bps_size < 64);
            bps[bps_size++] = bp;
        } else if (strcmp(cmd, "i") == 0) {
            // info
            char option[64];
            scanf("%s", option);
            if (strcmp(option, "r") == 0) {
                // register
                struct user_regs_struct regs;
                ptrace(PTRACE_GETREGS, child_pid, 0, &regs);
                // show rip, rbp and rsp
                procmsg("child process's RIP = %.16p\n", regs.rip);
                procmsg("child process's RBP = %.16p\n", regs.rbp);
                procmsg("child process's RSP = %.16p\n", regs.rsp);
                procmsg("......\n");
            } else if (strcmp(option, "b") == 0) {
                // break point
                for (int i = 0; i < bps_size; i++) {
                    procmsg("%d\taddr: %.16p\toriginal data: %#.16lx\n", i, bps[i]->addr, bps[i]->orig_data);
                }
            }
        } else if (strcmp(cmd, "x") == 0) {
            // examine memory
            void* addr;
            scanf("%p", &addr);
            // dump [addr, addr + 8)
            dump_process_memory(child_pid, addr, addr + 8);
        } else if (strcmp(cmd, "c") == 0) {
            // continue
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, child_pid, 0, &regs);
            // match break point
            int i;
            for (i = 0; i < bps_size; i++) {
                if ((unsigned long)bps[i]->addr == regs.rip - 1) {
                    break;
                }
            }
            if (i < bps_size) {
                // match success, stop at manual break point
                int rc = resume_from_breakpoint(child_pid, bps[i]);
                if (rc == 0) {
                    procmsg("child exited\n");
                    break;
                }
                else if (rc == 1) {
                    ptrace(PTRACE_GETREGS, child_pid, 0, &regs);
                    procmsg("child trap at RIP = %.16p\n", regs.rip - 1);
                    continue;
                }
                else {
                    procmsg("unexpected: %d\n", rc);
                    break;
                }
            } else {
                // match success, stop at temporary break point
                ptrace(PTRACE_CONT, child_pid, 0, 0);
                int wait_status;
                wait(&wait_status);
                if (WIFEXITED(wait_status)) {
                    procmsg("child exited\n");
                    break;
                }
                else if (WIFSTOPPED(wait_status)) {
                    ptrace(PTRACE_GETREGS, child_pid, 0, &regs);
                    procmsg("child trap at RIP = %.16p\n", regs.rip - 1);
                    continue;
                }
                else {
                    procmsg("unexpected\n");
                    break;
                }
            }
        }
    }
    // release resource
    for (int i = 0; i < bps_size; i++) {
        cleanup_breakpoint(bps[i]);
    }
}


int main(int argc, char** argv)
{
    pid_t child_pid;

    if (argc < 2) {
        fprintf(stderr, "Expected <program name> as argument\n");
        return -1;
    }

    child_pid = fork();
    if (child_pid == 0)
        run_target(argv[1]);
    else if (child_pid > 0) {
        run_debugger(child_pid);
    }
    else {
        perror("fork");
        return -1;
    }

    return 0;
}