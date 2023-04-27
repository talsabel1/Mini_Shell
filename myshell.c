#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define PIPE "|"
#define RUN_IN_BG "&"
#define INPUT_REDIRECT "<"


int process_arglist(int count, char** arglist);
int myPipe(int count, char **arglist, int index);
int background(int count, char **arglist);
int redirect(int count, char **arglist);
int prepare(void);
int finalize(void);
void error(const char *type);


int process_arglist(int count, char **arglist) {
    int i;
    int pipe;
    int bg;
    int inputRedirect;
    char* curr;
    pid_t pid;

    for (i = 0; i < count; i++) {
        curr = arglist[i];

        if (strcmp(curr, PIPE) == 0) {
            pipe = myPipe(count, arglist, i);
            if (pipe == -1) {
                return 0;
            }
            break;
        }
        else if (strcmp(curr, INPUT_REDIRECT) == 0) {
            inputRedirect = redirect(count, arglist);
            if (inputRedirect == -1) {
                return 0;
            }
            break;
        }
        else if (strcmp(curr, RUN_IN_BG) == 0) {
            bg = background(count, arglist);
            if (bg == -1) {
                return 0;
            }
            break;
        }
    }

    if (i == count) { //the for loop finished (the command didn't contain a shell symbol)
        pid = fork();
        if (pid == 0) {   // child process
            if (execvp(arglist[0], arglist) == -1){
                error("execvp");
                exit(1);
            }
        } else if (pid > 0) {   // parent process
            if((wait(NULL) == -1) && !(errno == ECHILD || errno == EINTR)) {
                error("wait");
                return 0;
            }
        } else {    // fork failed
            error("fork");
            return 0;
        }

    }

    return 1;
}


int myPipe(int count, char **arglist, int pipeIndex){
    int pipefd[2];
    pid_t pid1;
    pid_t pid2;

    if (-1 == pipe(pipefd)) {
        error("pipe");
        return -1;
    }
    else {
        pid1 = fork();
        if (pid1 == 0) {    // 1st child process (to write to pipe)
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                error("dup2");
                exit(1);
            }
            arglist[pipeIndex] = NULL;
            if(execvp(arglist[0], arglist) == -1){
                error("execvp");
                exit(1);
            }
        }
        else if (pid1 > 0) {      // parent process
            //wait();
            // wait error?
            pid2 = fork();
            if (pid2 == 0) { // 2nd child process (to read from pipe)
                close(pipefd[1]);
                if(dup2(pipefd[0], STDIN_FILENO) == -1){
                    error("dup2");
                    exit(1);
                }
                if (execvp(arglist[pipeIndex+1], &arglist[pipeIndex+1]) == -1){
                    error("execvp");
                    exit(1);
                }
            }
            else if (pid2 > 0) {
                close(pipefd[0]);
                close(pipefd[1]);
                if((wait(NULL) == -1) && !(errno == ECHILD || errno == EINTR)) {
                    error("wait");
                    return -1;
                }
            }
            else { // fork 2 failed
                error("fork");
                perror("(Second fork)");
                close(pipefd[0]);
                close(pipefd[1]);
                // kill fork 1 ?
                return -1;
            }
        }
        else {     // fork 1 failed
            error("fork");
            perror("(First fork)");
            close(pipefd[0]);
            close(pipefd[1]);
            return -1;
        }
    }
    return 1;
    // close opened read/ write pipes in children
}

int redirect(int count, char **arglist){
    pid_t pid;
    int fd;

    if ((fd = open(arglist[count-1], O_WRONLY | O_CREAT, 0644)) == -1) {
        error("file");
        return -1;
    }
    else {
        pid = fork();
        if (pid == 0) {    // child process
            if (dup2(fd, STDOUT_FILENO) == -1) {
                error("dup2");
                exit(1);
            }
            arglist[count-2] = NULL; // so we can send arglist to execvp w/o < and filename
            if (execvp(arglist[0], arglist) == -1){
                error("execvp");
                exit(1);
            }
        } else if (pid > 0) {      // parent process
            if((wait(NULL) == -1) && !(errno == ECHILD || errno == EINTR)) {
                error("wait");
                return -1;
            }
        } else {     // fork failed
            error("fork");
            return -1;
        }
    }
    return 1;
}

int background(int count, char **arglist) {
    pid_t pid;

    pid = fork();
    if (pid == 0) {    // child process
        arglist[count-1] = NULL; // remove & to send to execvp
        if (execvp(arglist[0], arglist) == -1) {
            error("execvp");
            exit(1);
        }
    }
    else if (pid < 0) { // fork failed
        error("fork");
        return -1;
    }
    return 1;
    // parent will just continue, not waiting for child to finish
}

int prepare(void) {
    // treat "no termination of shell on SIGINT"
    // foreground (child) processes should terminate on SIGINT
    // background (child) processes should not terminate on SIGINT
}


int finalize(void) {}

void error(const char *type) {
    if (strcmp(type, "pipe") == 0){
        perror("Pipe creation failed.");
    }
    else if (strcmp(type, "dup2") == 0){
        perror("Call to dup2() failed.");
    }
    else if (strcmp(type, "execvp") == 0) {
        perror("Call to execvp() failed.");
    }
    else if (strcmp(type, "fork") == 0) {
        perror("Fork creation failed.");
    }
    else if (strcmp(type, "file") == 0) {
        perror("Error opening file.");
    }
    else if (strcmp(type, "wait") == 0) {
        perror("Call to wait() failed.");
    }
}
