/*
 * Runs a command and reports, on stderr, how long it took and how much
 * memory it needed:
 *
 *     measure <command> [args...]
 *     -> "<wall clock in ms> <peak RSS in KB>"
 *
 * wait4 gives the child's own rusage, so the peak RSS is the child's and
 * not inflated by the measuring process. The child keeps our stdout and
 * stderr, which is how run.py compares program output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: measure <command> [args...]\n");
        return 2;
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 2;
    }
    if (child == 0) {
        execvp(argv[1], &argv[1]);
        perror(argv[1]);
        _exit(127);
    }

    int status;
    struct rusage usage;
    if (wait4(child, &status, 0, &usage) < 0) {
        perror("wait4");
        return 2;
    }
    gettimeofday(&end, NULL);

    double ms = (end.tv_sec - start.tv_sec) * 1000.0
              + (end.tv_usec - start.tv_usec) / 1000.0;
    fprintf(stderr, "%.3f %ld\n", ms, usage.ru_maxrss);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 2;
}
