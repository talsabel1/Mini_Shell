#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#define PIPE "|"
#define RUN_IN_BACKGROUND "&"
#define INPUT_REDIRECT "<"

/* function declarations ----------------------------------------------------------------------------------------------*/
int prepare(void);
int process_arglist(int count, char** arglist);
int regular_execution(char **arglist);
int my_pipe(char **arglist, int pipe_index);
int background(int count, char **arglist);
int redirect(int count, char **arglist);
int finalize(void);
void error(const char *type);
void restore_default_signals();
/*---------------------------------------------------------------------------------------------------------------------*/

/* Set up function, defines SIGINT and SIGCHLD responses --------------------------------------------------------------
 * Returns 0 on success -----------------------------------------------------------------------------------------------*/
int prepare(void) {
    /*
     * I used this link as a reference to assist in understanding how to write the code for signal handling:
     * https://www.gnu.org/software/libc/manual/html_node/Sigaction-Function-Example.html
     */

    struct sigaction new_action_sigint, new_action_sigchld;

    new_action_sigint.sa_handler = SIG_IGN;
    sigemptyset (&new_action_sigint.sa_mask);
    new_action_sigint.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &new_action_sigint, NULL) == -1) {
        error("signal");
        exit(1);
    }

    new_action_sigchld.sa_handler = SIG_IGN;
    sigemptyset (&new_action_sigchld.sa_mask);
    new_action_sigint.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &new_action_sigchld, NULL) == -1) {
        error("signal");
        exit(1);
    }

    return 0;
}

/* "Main" function that processes commands -------------------------------------------------------------------------------
 *  Receives array of strings that hold the command + args, and the length of the array. Returns 1 on success, 0 on failure */
int process_arglist(int count, char **arglist) {
    int i;
    char* curr;

    for (i = 0; i < count; i++) {
        curr = arglist[i];

        if (strcmp(curr, PIPE) == 0) {
            if (my_pipe(arglist, i) == -1) {
                return 0;
            }
            break;
        } else if (strcmp(curr, INPUT_REDIRECT) == 0) {
            if (redirect(count, arglist) == -1) {
                return 0;
            }
            break;
        } else if (strcmp(curr, RUN_IN_BACKGROUND) == 0) {
            if (background(count, arglist) == -1) {
                return 0;
            }
            break;
        }
    }

    if (i == count) {      //the for loop finished (the command didn't contain any shell symbol)
        if (regular_execution(arglist) == -1) {
            return 0;
        }
    }

    return 1;
}

/* Executing a "regular" shell command --------------------------------------------------------------------------------
 * Receives array of strings that hold the command + args. Returns 1 on success, -1 on failure ------------------------- */
int regular_execution(char **arglist) {
    pid_t pid;

    pid = fork();
    if (pid == 0) {   // child process
        restore_default_signals();
        if (execvp(arglist[0], arglist) == -1){
            error("execvp");
            exit(1);
        }
    } else if (pid > 0) {   // parent process
        if((waitpid(pid, NULL, WUNTRACED) == -1) && !(errno == ECHILD || errno == EINTR)) { // EINTR is handled by SA_RESTART in sigaction so this check is just an extra precaution
            error("wait");
            return -1;
        }
    } else {    // fork failed
        error("fork");
        return -1;
    }
    return 1;
}

/* Executing a PIPE shell command -------------------------------------------------------------------------------------
 * Receives array of strings that hold the command + args, and the index of the '|' char. Returns 1 on success, -1 on failure */
int my_pipe(char **arglist, int pipe_index){
    int pipefd[2];
    pid_t pid_1;
    pid_t pid_2;

    if (pipe(pipefd) == -1) {
        error("pipe");
        return -1;
    }
    else {
        pid_1 = fork();
        if (pid_1 == 0) {      // 1st child process (to write to pipe)
            restore_default_signals();
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                error("dup2");
                exit(1);
            }
            close(pipefd[1]);
            arglist[pipe_index] = NULL;
            if(execvp(arglist[0], arglist) == -1){
                error("execvp");
                exit(1);
            }
        }
        else if (pid_1 > 0) {     // parent process
            pid_2 = fork();
            if (pid_2 == 0) {     // 2nd child process (to read from pipe)
                restore_default_signals();
                close(pipefd[1]);
                if(dup2(pipefd[0], STDIN_FILENO) == -1){
                    error("dup2");
                    exit(1);
                }
                close(pipefd[0]);
                if (execvp(arglist[pipe_index + 1], &arglist[pipe_index + 1]) == -1){
                    error("execvp");
                    exit(1);
                }
            }
            else if (pid_2 > 0) {      // parent process
                close(pipefd[0]);
                close(pipefd[1]);
                if((waitpid(pid_1, NULL, WUNTRACED) == -1) && !(errno == ECHILD || errno == EINTR)) { // EINTR is handled by SA_RESTART in sigaction so this check is just an extra precaution
                    error("wait");
                    return -1;
                }
                if((waitpid(pid_2, NULL, WUNTRACED) == -1) && !(errno == ECHILD || errno == EINTR)) { // EINTR is handled by SA_RESTART in sigaction so this check is just an extra precaution
                    error("wait");
                    return -1;
                }
            }
            else {     // fork 2 failed
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
}

/* Executing a REDIRECT shell command ---------------------------------------------------------------------------------
*  Receives array of strings that hold the command + args, and the length of the array. Returns 1 on success, -1 on failure */
int redirect(int count, char **arglist){
    pid_t pid;
    int fd;

    pid = fork();
    if (pid == 0) {      // child process
        restore_default_signals();
        if ((fd = open(arglist[count-1], O_RDONLY, 0644)) == -1) {
            error("file");
            exit(1);
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            error("dup2");
            exit(1);
        }
        close(fd);
        arglist[count-2] = NULL;
        if (execvp(arglist[0], arglist) == -1){
            error("execvp");
            exit(1);
        }
    } else if (pid > 0) {      // parent process
        if((waitpid(pid, NULL, WUNTRACED) == -1) && !(errno == ECHILD || errno == EINTR)) { // EINTR is handled by SA_RESTART in sigaction so this check is just an extra precaution
            error("wait");
            return -1;
        }
    } else {      // fork failed
        error("fork");
        return -1;
    }

    return 1;
}

/* Executing a BACKGROUND shell command -------------------------------------------------------------------------------
 * Receives array of strings that hold the command + args, and the length of the array. Returns 1 on success, -1 on failure */
int background(int count, char **arglist) {
    pid_t pid;

    pid = fork();
    if (pid == 0) {    // child process
        if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
            error("signal");
            exit(1);
        }
        arglist[count-1] = NULL;
        if (execvp(arglist[0], arglist) == -1) {
            error("execvp");
            exit(1);
        }
    }
    else if (pid < 0) {      // fork failed
        error("fork");
        return -1;
    }
    return 1;
}

/* Clean up function --------------------------------------------------------------------------------------------------
 * Returns 0 on success -----------------------------------------------------------------------------------------------*/
int finalize(void) {
    return 0;
}

/* Signal Handling - restores SIGINT and SIGCHLD to default response --------------------------------------------------*/
void restore_default_signals() {
    /*
     * I used this link as a reference to assist in understanding how to write the code for signal handling:
     * https://www.gnu.org/software/libc/manual/html_node/Sigaction-Function-Example.html
     */

    struct sigaction new_action_sigint, new_action_sigchld;

    new_action_sigint.sa_handler = SIG_DFL;
    sigemptyset (&new_action_sigint.sa_mask);
    new_action_sigint.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &new_action_sigint, NULL) == -1) {
        error("signal");
        exit(1);
    }

    new_action_sigchld.sa_handler = SIG_DFL;
    sigemptyset (&new_action_sigchld.sa_mask);
    new_action_sigint.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &new_action_sigchld, NULL) == -1) {
        error("signal");
        exit(1);
    }
}

/* Error handling, receives error type to print the relevant message --------------------------------------------------*/
void error(const char *type) {
    if (strcmp(type, "pipe") == 0){
        perror("Error: Pipe creation failed.");
    }
    else if (strcmp(type, "dup2") == 0){
        perror("Error: Call to dup2() failed.");
    }
    else if (strcmp(type, "execvp") == 0) {
        perror("Error: Call to execvp() failed.");
    }
    else if (strcmp(type, "fork") == 0) {
        perror("Error: Fork creation failed.");
    }
    else if (strcmp(type, "file") == 0) {
        perror("Error: Call to open() failed. Couldn't open file.");
    }
    else if (strcmp(type, "wait") == 0) {
        perror("Error: Call to wait() failed.");
    }
    else if (strcmp(type, "signal") == 0) {
        perror("Error: Call to sigaction() failed.");
    }
}
