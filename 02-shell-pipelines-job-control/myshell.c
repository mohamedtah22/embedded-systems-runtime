#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include "LineParser.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/limits.h>

#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20

int debug = 0;

typedef struct process {
    cmdLine* cmd;                         
    pid_t pid;                            
    int status;                          
    struct process* next;                 
} process;

process* processList = NULL;

char history[HISTLEN][2048];

void addProcess(process** process_list, cmdLine* cmd, pid_t pid) {
    process* newProcess = malloc(sizeof(process));
    newProcess->cmd = cmd;
    newProcess->pid = pid;
    newProcess->next = process_list[0];
    process_list[0] = newProcess;

    if (newProcess->cmd->next || newProcess->cmd->blocking)
        newProcess->status = TERMINATED;
    else
        newProcess->status = RUNNING;
}

void freeProcessList(process* process_list) {
    if (process_list) {
        freeCmdLines(process_list->cmd);
        free(process_list);
    }
}

void removeTerminatedProcesses() {
    process* someProcess = processList;
    process* prev = NULL;
    while (someProcess != NULL) {
        if (someProcess->status == TERMINATED) {
            if (prev == NULL) {
                processList = processList->next;
            } else {
                prev->next = someProcess->next;
            }
        }
        prev = someProcess;
        someProcess = someProcess->next;
    }
}

void updateProcessStatus(process* process_list, int pid, int status) {
    process* process_list1 = process_list;
    while (process_list1 != NULL) {
        if (process_list1->pid == pid) {
            process_list1->status = status;
            return;
        }
        process_list1 = process_list1->next;
    }
}

void updateProcessList(process **process_list) {
    int status;
    pid_t pid;

#ifdef WCONTINUED
    while ((pid = waitpid(-1, &status, WCONTINUED | WUNTRACED | WNOHANG)) > 0) {
#else
    while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {
#endif
        while (*process_list) {
            if ((*process_list)->pid == pid) {
                if (WIFSIGNALED(status) || WIFEXITED(status)) {
                    updateProcessStatus((*process_list), (*process_list)->pid, TERMINATED);
                } else if (WIFSTOPPED(status)) {
                    updateProcessStatus((*process_list), (*process_list)->pid, SUSPENDED);
                } else {
                    updateProcessStatus((*process_list), (*process_list)->pid, RUNNING);
                }
            }
            process_list = &((*process_list)->next);
        }
    }
}

void printProcessList(process** process_list) {
    updateProcessList(process_list);
    int index = 0;
    process* process = (*process_list);
    if (process != NULL) {
        printf("index          PID          Command         STATUS\n");
    }
    while (process != NULL) {
        if (process->status == RUNNING) {
            printf("%d              %d         %s        RUNNING\n", index, process->pid, process->cmd->arguments[0]);
        } else if (process->status == TERMINATED) {
            printf("%d              %d         %s        TERMINATED\n", index, process->pid, process->cmd->arguments[0]);
        } else if (process->status == SUSPENDED) {
            printf("%d              %d         %s        SUSPENDED\n", index, process->pid, process->cmd->arguments[0]);
        }
        process = process->next;
        index++;
    }
    removeTerminatedProcesses();
}

void pipeline_process(cmdLine* pCmdLine1, cmdLine* pCmdLine2) {
    if (pCmdLine1->outputRedirect) {
        fprintf(stderr, "Invalid output redirect in first command\n");
        return;
    }
    if (pCmdLine2->inputRedirect) {
        fprintf(stderr, "Invalid input redirect in second command\n");
        return;
    }
    int p[2];
    if (pipe(p) < 0) {
        exit(1);
    }
    if (debug) {
        fprintf(stderr, "(parent_process>forking…)\n");
    }
    int child1 = fork();
    if (child1 == 0) {
        if (debug) {
            fprintf(stderr, "(child1>redirecting stdout to the write end of the pipe…)\n");
        }
        close(1);
        dup(p[1]);
        close(p[1]);
        if (debug) {
            fprintf(stderr, "(child1>going to execute cmd: %s)\n", pCmdLine1->arguments[0]);
        }
        if (execvp(pCmdLine1->arguments[0], pCmdLine1->arguments) == -1) {
            perror("Error");
            _exit(1);
        }
    } else {
        if (debug) {
            fprintf(stderr, "(parent_process>created process with id: %d)\n", child1);
            fprintf(stderr, "(parent_process>closing the write end of the pipe…)\n");
        }
        close(p[1]);
        int child2 = fork();
        if (child2 == 0) {
            if (debug) {
                fprintf(stderr, "(child2>redirecting stdin to the read end of the pipe…)\n");
            }
            close(0);
            dup(p[0]);
            close(p[0]);
            if (debug) {
                fprintf(stderr, "(child2>going to execute cmd: %s)\n", pCmdLine2->arguments[0]);
            }
            if (execvp(pCmdLine2->arguments[0], pCmdLine2->arguments) == -1) {
                perror("Error");
                _exit(1);
            }
        } else {
            if (debug) {
                fprintf(stderr, "(parent_process>created process with id: %d)\n", child2);
                fprintf(stderr, "(parent_process>closing the read end of the pipe…)\n");
            }
            close(p[0]);
        }
        if (debug) {
            fprintf(stderr, "(parent_process>waiting for child processes to terminate…)\n");
        }
        int status;
        waitpid(child1, &status, 0);
        waitpid(child2, &status, 0);
        addProcess(&processList, pCmdLine1, child1);
        freeCmdLines(pCmdLine2);
    }
}

void execute(cmdLine* pCmdLine) {
    if (pCmdLine->next) {
        pipeline_process(pCmdLine, pCmdLine->next);
    } else {
        int PID = fork();
        if (debug == 1) {
            fprintf(stderr, "PID: %d\n", PID);
            fprintf(stderr, "Command: %s\n", pCmdLine->arguments[0]);
        }
        if (PID == 0) {
            if (strcmp(pCmdLine->arguments[0], "cat") == 0) {
                if (pCmdLine->inputRedirect != NULL) {
                    int inputRed = open(pCmdLine->inputRedirect, 0);
                    close(0);
                    dup(inputRed);
                    close(inputRed);
                }
                if (pCmdLine->outputRedirect != NULL) {
                    int outputRed = open(pCmdLine->outputRedirect, 1);
                    close(1);
                    dup(outputRed);
                    close(outputRed);
                }
            }
            if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
                perror("Error");
                _exit(1);
            }
        }
        if (pCmdLine->blocking == 1) {
            int status;
            waitpid(PID, &status, 0);
        }
        addProcess(&processList, pCmdLine, PID);
    }
}

int newCommandLine = -1;
int oldCommandLine = 0;
int hSize = 0;

void addCommandToHistory(char* input) {
    newCommandLine = (newCommandLine + 1) % HISTLEN;
    strcpy(history[newCommandLine], input);
    if (hSize == HISTLEN) {
        oldCommandLine = (oldCommandLine + 1) % HISTLEN;
    } else {
        hSize++;
    }
}

void sleepProcess(int pid) {
    if (kill(pid, SIGTSTP) == -1) {
        perror("Error");
    } else {
        updateProcessStatus(processList, pid, SUSPENDED);
        printf("Looper handling SIGTSTP\n");
    }
}

void blastProcess(int pid) {
    if (kill(pid, SIGINT) == -1) {
        perror("Error");
    } else {
        updateProcessStatus(processList, pid, TERMINATED);
        printf("Looper handling SIGINT\n");
    }
}

void alarmProcess(int pid) {
    if (kill(pid, SIGCONT) == -1) {
        perror("Error");
    } else {
        updateProcessStatus(processList, pid, RUNNING);
        printf("Looper handling SIGCONT\n");
    }
}

int main(int argc, char** argv) {
    char buffer[PATH_MAX];
    char input[2048];
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug = 1;
        }
    }
    while (1) {
        getcwd(buffer, PATH_MAX);
        printf("%s>", buffer);
        fgets(input, 2048, stdin);
        if (input[0] == '\n') {
            continue;
        }

        if (strcmp(input, "quit\n") == 0) {
            exit(0);
        }
        cmdLine* line = parseCmdLines(input);
        if (strcmp(line->arguments[0], "!!") == 0) {
            if (hSize == 0) {
                fprintf(stderr, "There's no history of command lines\n");
                return 1;
            } else {
                int historyCommand = newCommandLine;
                while (strcmp(history[historyCommand], "history\n") == 0) {
                    historyCommand--;
                }
                line = parseCmdLines(history[historyCommand]);
            }
        } else if (strncmp(line->arguments[0], "!", 1) == 0) {
            int index = atoi((line->arguments[0]) + 1) - 1;
            if (index < 0 || index >= hSize) {
                fprintf(stderr, "Error, index was out of bound!\n");
                return 1;
            }
            line = parseCmdLines(history[(oldCommandLine + index) % HISTLEN]);
        }
        if (strcmp(line->arguments[0], "history") == 0) {
            if (hSize == HISTLEN) {
                for (int i = 0; i < HISTLEN; i++) {
                    if (history[i]) {
                        printf("%d  %s", i + 1, history[(oldCommandLine + i) % HISTLEN]);
                    }
                }
            } else {
                for (int i = 0; i <= newCommandLine; i++) {
                    if (history[i]) {
                        printf("%d  %s", i + 1, history[i]);
                    }
                }
            }
        } else if (strcmp(line->arguments[0], "cd") == 0) {
            if (chdir(line->arguments[1]) == -1) {
                fprintf(stderr, "No such path exists\n");
            } else {
                getcwd(buffer, PATH_MAX);
                printf("The working directory changed to %s\n", buffer);
            }
            freeCmdLines(line);
        } else if (strcmp(line->arguments[0], "wakeup") == 0) {
            int num = atoi(line->arguments[1]);
            if (num <= 0) {
                fprintf(stderr, "Invalid process id\n");
            } else {
                alarmProcess(num);
            }
            freeCmdLines(line);
        } else if (strcmp(line->arguments[0], "blast") == 0) {
            int num = atoi(line->arguments[1]);
            if (num <= 0) {
                fprintf(stderr, "Invalid process id\n");
            } else {
                blastProcess(num);
            }
            freeCmdLines(line);
        } else if (strcmp(line->arguments[0], "sleep") == 0) {
            int num = atoi(line->arguments[1]);
            if (num <= 0) {
                fprintf(stderr, "Invalid process id\n");
            } else {
                sleepProcess(num);
            }
            freeCmdLines(line);
        } else if (strcmp(line->arguments[0], "procs") == 0) {
            printProcessList(&processList);
            freeCmdLines(line);
        } else {
            execute(line);
        }
        updateProcessList(&processList);
        if (strcmp(history[newCommandLine], "history\n") != 0) {
            addCommandToHistory(input);
        }
    }
    freeProcessList(processList);
    return 0;
}
