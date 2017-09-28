#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>  /* for pid_t */
#include <termios.h>
#include "analyzer.h"

struct proc {
    pid_t pid;

    /* Simply refers to argv[0]. Should not be
     * freed().
     * This is not guaranteed to be unique. */
    char *name;
    char **argv;

    bool stopped;
    bool finished;

    int status; /* the status value */

    struct proc *next;
};

struct job {
    /* The process group ID */
    pid_t pgid;

    int stdin_fd, stdout_fd, stderr_fd;

    bool is_bg;

    struct termios tmodes;

    /* List of processes in this pipeline */
    struct proc *procs;

    /* the next job */
    struct job *next;
};

void pcfsh_init(void);

void pcfsh_prefix(const char *str);

int job_exec(struct an_pipeline *pln);

/**
 * Waits for a job to be completed. Returns 0 on success.
 */
int job_wait(const struct job *jb);

void job_background(const struct job *jb, bool to_continue);

void job_foreground(struct job *jb, bool to_continue);

void job_continue(struct job *jb, bool background);

bool jobs_remaining(void);

#endif
