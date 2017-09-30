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

    /**
     * If we have notified the user of
     * a state change already.
     */
    bool notified;

    /**
     * Saved terminal modes.
     */
    struct termios tmodes;

    /**
     * If the job has a saved terminal mode
     * associated with it.
     */
    bool tmodes_saved;

    /**
     * For displaying messages.
     */
    char *cmdline;

    /* List of processes in this pipeline */
    struct proc *procs;

    /* the next job */
    struct job *next;
};

void pcfsh_init(void);

void pcfsh_prefix(const char *str);

int job_exec(struct an_pipeline *pln);

/* Returns true if all processes
 * in the job have stopped. */
bool job_stopped(const struct job *jb);

bool job_finished(const struct job *jb);

/**
 * Determines if all of the processes in the job
 * are internal processes.
 */
bool job_is_internal(const struct job *jb);

/**
 * Waits for a job to be completed.
 */
void job_wait(struct job *jb);

/**
 * Place the job in the foreground and wait for it.
 * If {@to_continue} is true, this will send SIGCONT to the job,
 * and attempt to restore the terminal state.
 */
void job_background(const struct job *jb, bool to_continue);

/**
 * Place a job in the background. If {@to_continue} is true, this
 * will send SIGCONT to the job.
 */
void job_foreground(struct job *jb, bool to_continue);

/**
 * Continue a job. If {@background} is true, places the job
 * in the background.
 */
void job_continue(struct job *jb, bool background);

/**
 * If there are any finished jobs, reaps them. Then displays
 * information about any jobs that have changed state
 * since the last time this was called.
 */
void jobs_notifications(void);

/**
 * Should only be called on program termination.
 * This sends a SIGKILL to all running child processes
 * before termination.
 */
void jobs_cleanup(void);

/**
 * Frees all information associated with a job.
 */
void job_destroy(struct job *jb);

#endif
