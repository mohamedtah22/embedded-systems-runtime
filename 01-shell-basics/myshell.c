#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>
#include "LineParser.h"
#include <linux/limits.h>

#define MAX_INPUT_SIZE 2048

void displayPrompt() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s$ ", cwd);
    } else {
        perror("getcwd");
        exit(EXIT_FAILURE);
    }
}

void execute(cmdLine *pCmdLine) {
    int inputFd, outputFd;
    
    // Check for input redirection
    if (pCmdLine->inputRedirect) {
        inputFd = open(pCmdLine->inputRedirect, O_RDONLY);
        if (inputFd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        dup2(inputFd, STDIN_FILENO);
        close(inputFd);
    }
    
    // Check for output redirection
    if (pCmdLine->outputRedirect) {
        outputFd = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (outputFd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }
        dup2(outputFd, STDOUT_FILENO);
        close(outputFd);
    }
    
    // Execute command
    execvp(pCmdLine->arguments[0], pCmdLine->arguments);
    perror("execvp");
    exit(EXIT_FAILURE);
}

int main() {
    char input[MAX_INPUT_SIZE];
    cmdLine *parsedCmdLine;

    while (1) {
        displayPrompt();
        fgets(input, MAX_INPUT_SIZE, stdin);

        // Check if the command is "quit"
        if (strcmp(input, "quit\n") == 0) {
            printf("Exiting shell.\n");
            break;
        }

        // Parse input
        parsedCmdLine = parseCmdLines(input);
        if (!parsedCmdLine) {
            perror("parseCmdLines");
            exit(EXIT_FAILURE);
        }

        // Execute command
        execute(parsedCmdLine);

        // Free allocated memory
        freeCmdLines(parsedCmdLine);
    }

    return 0;
}
