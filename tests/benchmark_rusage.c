#define _DEFAULT_SOURCE

#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Measure a target forked by a small, freshly exec'd process. Directly using
 * Python's child rusage can include the Python parent's resident-memory floor.
 * Linux reports ru_maxrss in KiB; benchmark_jsoncons.py checks the platform.
 */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: benchmark_rusage EXECUTABLE [ARGUMENTS...]\n");
        return 2;
    }

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 2;
    }
    if (child == 0) {
        execv(argv[1], argv + 1);
        perror("execv");
        _exit(127);
    }

    int status;
    struct rusage usage;
    while (wait4(child, &status, 0, &usage) < 0) {
        if (errno == EINTR) continue;
        perror("wait4");
        return 2;
    }
    double user = (double)usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
    double system = (double)usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    fprintf(stderr,
            "JSONBENCH_RUSAGE {\"peak_rss_kib\":%ld,\"user_seconds\":%.9f,"
            "\"system_seconds\":%.9f}\n",
            usage.ru_maxrss, user, system);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 2;
}
