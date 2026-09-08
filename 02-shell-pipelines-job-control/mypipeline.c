#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>

#define c1 "c1" // child 1
#define c2 "c2" // child 2
#define pp "pp" // parent process
int debugMode = 0;
int createcp(char *args[], int toClose, int pipefds[], int rf);
void printFunc(char *name, char *msg, int id);
void printFunc1(char *name, char *msg);

int main(int argc, char **argv)
{
   char *args2[4] = {"tail", "-n", "2", NULL};
   char *args1[3] = {"ls", "-l", NULL};
   pid_t pid;
   int pipefds[2];
   for (int i = 0; i < argc; i++)
   {
      if (strncmp(argv[i], "-d", 2) == 0)
         debugMode = 1;
   }

   printf("before creating pipe..\n");
   if (pipe(pipefds) == -1)
   {
      perror("Failed to create pipe.\n");
      return 1;
   }
   int read = pipefds[0];  
   int write = pipefds[1]; 

   printFunc1(pp, "forking...");
   pid_t pid1 = createcp(args1, STDOUT_FILENO, pipefds, 1);
   printFunc1(pp, "closing the write end of the pipe...)");
   close(write);
   printFunc(pp, "Created process with id:", pid1);

   printFunc1(pp, "forking...");
   pid_t pid2 = createcp(args2, STDIN_FILENO, pipefds, 0);
   printFunc1(pp, "closing the read end of the pipe...)");
   close(read);
   printFunc(pp, "Created process with id:", pid2);
   printFunc1(pp, "waiting for child processes to terminate...)");
   waitpid(pid1, NULL, 0);
   waitpid(pid2, NULL, 0);

   printFunc1(pp, "exiting...");
   return 0;
}
int createcp(char *args[], int toClose, int pipefds[], int rf)
{
   pid_t pid;
   pid = fork();

   if (pid == 0) // child process
   {
      if (rf)
      {
         printFunc1(c1, "redirecting stdout to the write end of the pipe...)");
      }
      else
      {
         printFunc1(c2, "redirecting stdin to the write end of the pipe...)");
      }
      close(toClose);
      dup(pipefds[rf]);
      close(pipefds[rf]);
      if (rf)
      {
         printFunc1(c1, "going to excute cmd...)");
      }
      else
      {
         printFunc1(c2, "going to excute cmd...)");
      }
      execvp(args[0], args);
   }
   return pid;
}
void printFunc(char *name, char *msg, int id)
{
   if (debugMode == 1)
   {
      char *output = "(";
      fprintf(stderr, "%s%s%s%s%d%s\n", "(", name, ">", msg, id, ")");
   }
}
void printFunc1(char *name, char *msg)
{
   if (debugMode == 1)
   {
      fprintf(stderr, "%s%s%s%s\n", "(", name, ">", msg);
   }
}
