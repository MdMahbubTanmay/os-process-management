
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define GRACE_SECONDS 2.0   /* time between SIGTERM and SIGKILL */
#define POLL_NS 50000000L   /* 50 ms polling interval */

static FILE *g_log = NULL;


static void logmsg(const char *fmt, ...)
{
    struct timespec t;
    struct tm tm;
    char ts[64], msg[1024];
    va_list ap;

    clock_gettime(CLOCK_REALTIME, &t);
    localtime_r(&t.tv_sec, &tm);
    size_t n = strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(ts + n, sizeof ts - n, ".%03ld", t.tv_nsec / 1000000);

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    printf("[%s] %s\n", ts, msg);
    fflush(stdout);
    if (g_log) {
        fprintf(g_log, "[%s] %s\n", ts, msg);
        fflush(g_log);
    }
}

static double elapsed(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) + (now.tv_nsec - start->tv_nsec) / 1e9;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-t timeout_sec] [-l logfile] <command> [args...]\n"
            "  -t  kill the child if it runs longer than this many seconds\n"
            "  -l  append log lines to this file (default: logs/process_log.txt)\n",
            prog);
}

int main(int argc, char *argv[])
{
    double timeout = 0;                 /* 0 = no timeout */
    const char *logpath = "logs/process_log.txt";
    int opt;

    while ((opt = getopt(argc, argv, "+t:l:h")) != -1) {
        char *end;
        switch (opt) {
        case 't':
            timeout = strtod(optarg, &end);
            if (*end != '\0' || timeout < 0) {
                fprintf(stderr, "Invalid timeout: %s\n", optarg);
                return 2;
            }
            break;
        case 'l': logpath = optarg; break;
        default: usage(argv[0]); return 2;
        }
    }
    if (optind >= argc) {
        usage(argv[0]);
        return 2;
    }
    char **cmd = &argv[optind];

    g_log = fopen(logpath, "a");
    if (!g_log)
        perror("warning: cannot open log file (continuing without it)");

    logmsg("PARENT  pid=%d ppid=%d : starting, command='%s', timeout=%.1fs",
           getpid(), getppid(), cmd[0], timeout);


    int ep[2];
    if (pipe(ep) < 0) { perror("pipe"); return 1; }
    if (fcntl(ep[1], F_SETFD, FD_CLOEXEC) < 0) { perror("fcntl"); return 1; }

    fflush(NULL);                       /* avoid duplicated buffered output */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {                     /* ---------- CHILD ---------- */
        close(ep[0]);
        setpgid(0, 0);                  
        logmsg("CHILD   pid=%d ppid=%d : calling execvp('%s')",
               getpid(), getppid(), cmd[0]);
        execvp(cmd[0], cmd);
      
        int e = errno;
        if (write(ep[1], &e, sizeof e) < 0) {  }
        _exit(127);
    }

    /* ------------- PARENT ------------- */
    close(ep[1]);
    setpgid(pid, pid);                  

    int child_errno = 0;
    ssize_t n;
    do {
        n = read(ep[0], &child_errno, sizeof child_errno);
    } while (n < 0 && errno == EINTR);
    close(ep[0]);

    int status;
    if (n == (ssize_t)sizeof child_errno) {
        /* exec failed in child */
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        logmsg("PARENT  pid=%d : exec of '%s' FAILED in child %d: %s",
               getpid(), cmd[0], pid, strerror(child_errno));
        logmsg("PARENT  child %d exited with status 127 (exec failure)", pid);
        if (g_log) fclose(g_log);
        return 127;
    }

    logmsg("PARENT  pid=%d : child %d is running, waiting with waitpid()",
           getpid(), pid);

    int sent_term = 0, sent_kill = 0, timed_out = 0;
    double term_at = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("waitpid");
            return 1;
        }
        double el = elapsed(&start);
        if (timeout > 0 && !sent_term && el >= timeout) {
            logmsg("PARENT  TIMEOUT after %.2fs: sending SIGTERM to process group %d",
                   el, pid);
            kill(-pid, SIGTERM);
            sent_term = timed_out = 1;
            term_at = el;
        } else if (sent_term && !sent_kill && el - term_at >= GRACE_SECONDS) {
            logmsg("PARENT  child ignored SIGTERM: sending SIGKILL to group %d", pid);
            kill(-pid, SIGKILL);
            sent_kill = 1;
        }
        struct timespec ts = {0, POLL_NS};
        nanosleep(&ts, NULL);
    }

    double total = elapsed(&start);
    int rc;
    if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
        logmsg("PARENT  child %d terminated NORMALLY, exit status=%d, runtime=%.3fs",
               pid, rc, total);
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        rc = 128 + sig;
        logmsg("PARENT  child %d KILLED BY SIGNAL %d (%s)%s, runtime=%.3fs%s",
               pid, sig, strsignal(sig),
#ifdef WCOREDUMP
               WCOREDUMP(status) ? " [core dumped]" : "",
#else
               "",
#endif
               total, timed_out ? " [caused by timeout mechanism]" : "");
    } else {
        rc = 1;
        logmsg("PARENT  child %d ended in unexpected state (status=0x%x)", pid, status);
    }


    if (timed_out)
        kill(-pid, SIGKILL);

    if (g_log) fclose(g_log);
    return rc;
}
