# Mini Shell
A mini project that can handle and execute shell commands including:
- Regular Commands
- Processes running in background - '&'
- Single pipes - '|'
- Single input redirect - '<'

The shell also handles the desired response for SIGINT and SIGCHLD signals for each type of process, cleans up zombies and handles various errors (errors in a child process will not stop the execution of the parent shell).

Compiles cleanly with the following flags:

       gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 shell.c myshell.c
