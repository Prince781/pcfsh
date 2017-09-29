#include "shell.h"
#include "ds/llist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <libgen.h>
#include <assert.h>

/**
 * The process group ID of the shell.
 */
pid_t shell_pgid;
static int interactive = 0;
static int shell_input_fd;

static struct job *jobs = NULL;
static struct termios term_attrs;

/**
 * Defines an internal process handler.
 */
typedef int (*intproc)(char **argv, int infile, int outfile);

/**
 * Defines a built-in command.
 */
struct builtin {
    /**
     * The program name.
     */
    const char *name;

    /**
     * Function pointer.
     */
    intproc func;

    /**
     * A usage example.
     */
    const char *usage;

    /**
     * A short description.
     */
    const char *desc;
};

static int proc_internal_cmd_cd(char **argv, int infile, int outfile);
static int proc_internal_cmd_jobs(char **argv, int infile, int outfile);
static int proc_internal_cmd_fg(char **argv, int infile, int outfile);
static int proc_internal_cmd_bg(char **argv, int infile, int outfile);
static int proc_internal_cmd_exit(char **argv, int infile, int outfile);
static int proc_internal_cmd_help(char **argv, int infile, int outfile);

struct builtin builtins[] = {
    {
        .name = "cd",
        .func = proc_internal_cmd_cd,
        .usage = "cd [path]",
        .desc = "Change directory."
    },
    {
        .name = "jobs",
        .func = proc_internal_cmd_jobs,
        .usage = "jobs [-l|-p] [job_id]",
        .desc = "Show all jobs. See man jobs(1)"
    },
    {
        .name = "fg",
        .func = proc_internal_cmd_fg,
        .usage = "fg [job_id]",
        .desc = "Set recent job, or specified job, into foreground."
    },
    {
        .name = "bg",
        .func = proc_internal_cmd_bg,
        .usage = "bg [job_id]",
        .desc = "Set recent job, or specified job, int background."
    },
    {
        .name = "exit",
        .func = proc_internal_cmd_exit,
        .usage = "exit [status]",
        .desc = "Exit normally or with status."
    },
    {
        .name = "help",
        .func = proc_internal_cmd_help,
        .usage = "help",
        .desc = "Show help."
    },
    { NULL, NULL, NULL }
};

static void sighandler(int signum, siginfo_t *info, void *context)
{
    if (signum == SIGCHLD) {
        /* we want to reset the controller to us */
        if (info->si_code != CLD_CONTINUED) {
            if (tcsetpgrp(shell_input_fd, shell_pgid) < 0)
                perror("tcsetpgrp");
        } else {
            /* fprintf(stderr, "resumed\n"); */
        }
    }
}

/**
 * Note: some of the basic ideas come from this helpful resource:
 * https://www.gnu.org/software/libc/manual/html_node/Initializing-the-Shell.html#Initializing-the-Shell
 */
void pcfsh_init(void)
{
    shell_input_fd = STDIN_FILENO;
    interactive = isatty(shell_input_fd);

    /* determine if shell is running in a tty,
     * in case we want to register signal handlers */
    if (interactive) {
        /* In case we were started from another shell,
         * we should pause this process until it is in the foreground.
         * We get the pgid of the process controlling the terminal
         * and check to see that it is ours.*/
        while (tcgetpgrp(shell_input_fd) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        /* we want to ignore all job control signals,
         * since we are the controlling terminal */
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        /* we don't want to ignore SIGCHLD
         * see the NOTES section of wait(2) */

        /* put this process in its own process group */
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Could not put shell in its own process group.");
            exit(EXIT_FAILURE);
        }

        /* tell the terminal that all processes in 
         * the shell's PGID are the foreground */
        tcsetpgrp(shell_input_fd, shell_pgid);

        /* save terminal attributes */
        tcgetattr(shell_input_fd, &term_attrs);

        /* register signal handler */
        struct sigaction sa;

        sa.sa_sigaction = sighandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_SIGINFO;

        sigaction(SIGCHLD, &sa, NULL);
    }
}

void pcfsh_prefix(const char *str)
{
    char cwd[1024];
    char buf[1024];

    if (!interactive)
        return;

    if (str == NULL)
        str = "$";

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        char *bname = basename(cwd);
        snprintf(buf, sizeof(buf), "\x1b[38;5;32;1m%s\x1b[0m %s ", bname, str);
    } else
        snprintf(buf, sizeof(buf), "%s ", str);

    write(shell_input_fd, buf, strlen(buf));
}

/* internal processes */

static int proc_internal_cmd_cd(char **argv, int infile, int outfile)
{
    if (argv[1] != NULL) {
        if (chdir(argv[1]) < 0) {
            perror(argv[1]);
            return -1;
        }
    }
    return 0;
}

static void job_display(const struct job *jb, 
        bool more_info, 
        bool display_only_pids,
        long curjob,
        int outfile)
{
    char buf[1024];
    /* show all information about this job
     * job number, current job, process group ID, state, and command that formed the job*/
    if (more_info || display_only_pids) {
        size_t padding;
        char padbuf[48];

        snprintf(buf, sizeof(buf), "[%ld] ", curjob);
        padding = strlen(buf);
        write(outfile, buf, padding);
        snprintf(padbuf, sizeof(padbuf), "%*s", (int) padding, " ");

        for (struct proc *p = jb->procs; p != NULL; p = p->next) {
            if (p != jb->procs)
                write(outfile, padbuf, padding);
            if (p->pid == jb->pgid)
                write(outfile, "+ ", 2);
            /* write PID */
            if (!display_only_pids || p->pid == jb->pgid) {
                snprintf(buf, sizeof(buf), "%6d ", p->pid);
                write(outfile, buf, strlen(buf));
            } else {
                write(outfile, "       ", 7);
            }

            /* write state */
            if (job_stopped(jb))
                snprintf(buf, sizeof(buf), "stopped ");
            else if (job_finished(jb))
                snprintf(buf, sizeof(buf), "done ");
            else
                snprintf(buf, sizeof(buf), "running ");
            write(outfile, buf, strlen(buf));
            /* display command */
            write(outfile, p->name, strlen(p->name));
            write(outfile, "\n", 1);
        }
    } else {
        snprintf(buf, sizeof(buf), "[%ld] + ", curjob);
        write(outfile, buf, strlen(buf));
        if (job_stopped(jb))
            snprintf(buf, sizeof(buf), "stopped ");
        else if (job_finished(jb))
            snprintf(buf, sizeof(buf), "done ");
        else
            snprintf(buf, sizeof(buf), "running ");
        write(outfile, buf, strlen(buf));
        /* display command */
        write(outfile, jb->cmdline, strlen(jb->cmdline));
        write(outfile, "\n", 1);
    }
}

static int proc_internal_cmd_jobs(char **argv, int infile, int outfile)
{
    char **argp;
    /**
     * this is option is mutually exclusive with
     * the other */
    bool more_info = false;         /* -l option */
    bool display_only_pids = false; /* -p option */
    long job_id = 0;

    argp = argv + 1;
    while (*argp != NULL) {
        if (strcmp(*argp, "-l") == 0)
            more_info = true;
        else if (strcmp(*argp, "-p") == 0)
            display_only_pids = true;
        else {
            job_id = strtol(*argp, NULL, 0);
            if (job_id <= 0 || errno == ERANGE) {
                fprintf(stderr, "jobs: invalid job_id %s\n", *argp);
                return -1;
            }
        }
        ++argp;
    }

    long curjob = 1;
    struct job *jb = jobs;

    while (jb != NULL) {
        if (job_id > 0 && curjob != job_id) {
            ++curjob;
            jb = jb->next;
            continue;
        }

        job_display(jb, more_info, display_only_pids, curjob, outfile);

        ++curjob;
        jb = jb->next;
    }

    if (job_id > curjob) {
        fprintf(stderr, "jobs: invalid job_id %ld\n", job_id);
        return -1;
    }

    return 0;
}

static int proc_internal_cmd_fg(char **argv, int infile, int outfile)
{
    char **argp;
    long job_id = 0;

    argp = argv + 1;
    while (*argp != NULL) {
        job_id = strtol(*argp, NULL, 0);
        if (job_id <= 0 || errno == ERANGE) {
            fprintf(stderr, "fg: invalid job_id %s\n", *argp);
            return -1;
        }
        ++argp;
    }

    /* take a specific job and move it to the foreground */
    if (job_id > 0) {
        struct job *jb = jobs;
        long curjob = 1;

        while (jb != NULL) {
            if (job_id == curjob) {
                job_continue(jb, false);
                return 0;
            }
            jb = jb->next;
            ++curjob;
        }

        fprintf(stderr, "fg: invalid job_id %ld\n", job_id);
        return -1;
    }

    /* take the most recent job and move it to the foreground */
    if (jobs != NULL)
        job_continue(jobs, false);
    return 0;
}

static int proc_internal_cmd_bg(char **argv, int infile, int outfile)
{
    char **argp;
    long job_id = 0;

    argp = argv + 1;
    while (*argp != NULL) {
        job_id = strtol(*argp, NULL, 0);
        if (job_id <= 0 || errno == ERANGE) {
            fprintf(stderr, "bg: invalid job_id %s\n", *argp);
            return -1;
        }
        ++argp;
    }

    /* take a specific job and move it to the background */
    if (job_id > 0) {
        struct job *jb = jobs;
        long curjob = 1;

        while (jb != NULL) {
            if (job_id == curjob) {
                job_continue(jb, true);
                return 0;
            }
            jb = jb->next;
            ++curjob;
        }

        fprintf(stderr, "bg: invalid job_id %ld\n", job_id);
        return -1;
    }

    /* take the most recent job and move it to the background */
    if (jobs != NULL)
        job_continue(jobs, true);
    return 0;
}

static int proc_internal_cmd_exit(char **argv, int infile, int outfile)
{
    char **argp;
    long exit_status = 0;

    argp = argv + 1;

    /* try parsing the parameter */
    if (*argp != NULL) {
        exit_status = strtol(*argp, NULL, 0);
        if (errno == ERANGE)
            exit_status = 0;
    }

    exit(exit_status);
}

static int proc_internal_cmd_help(char **argv, int infile, int outfile)
{
    char buf[256];

    write(outfile, "PCF Shell Help\n", 15);
    write(outfile, "==============\n", 15);
    for (struct builtin *b = &builtins[0]; b->name != NULL; ++b) {
        snprintf(buf, sizeof(buf), " %s\n%4s%s\n", b->usage, " ", b->desc);
        write(outfile, buf, strlen(buf));
    }

    return 0;
}

static intproc proc_internal_get(const char *cmdname)
{
    for (struct builtin *b = &builtins[0]; b->name != NULL; ++b) {
        if (strcmp(cmdname, b->name) == 0)
            return b->func;
    }
    return NULL;
}

/* end of internal processes */

static void proc_exec(struct proc *proc, int pgid, int fdin, int fdout, int fderr, bool is_bg)
{
    /* we only care about job control if we're on a tty */
    if (interactive) {
        pid_t pid;

        pid = getpid();
        /* Set this to belong to a new process group.
         * note that if pgid == 0, then this will set
         * this process to be the group leader (pgid = pid). */
        if (pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);

        /* set as foreground */
        if (!is_bg)
            tcsetpgrp(shell_input_fd, pgid);

        /* re-enable signals */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
    }

    /* We're in a child process before exec()ing.
     * We have to dup() to associate stdin and stdout
     * with our pipe ends. */

    if (fdin != STDIN_FILENO) {
        dup2(fdin, STDIN_FILENO);
        /* we don't want to leak this file descriptor */
        close(fdin);
    }

    if (fdout != STDOUT_FILENO) {
        dup2(fdout, STDOUT_FILENO);
        close(fdout);
    }

    if (fderr != STDERR_FILENO) {
        dup2(fderr, STDERR_FILENO);
        close(fderr);
    }

#ifdef DEBUG_PROC
    for (size_t i=0; proc->argv[i] != NULL; ++i)
        fprintf(stderr, "%s ", proc->argv[i]);
    fprintf(stderr, "\n");
#endif

    execvp(proc->name, proc->argv);
    perror(proc->name);
    /* child exits if exec failed */
    exit(EXIT_FAILURE);
}

int job_exec(struct an_pipeline *pln)
{
    struct job *jb;
    char cwd[1024];
    int dirfd = -1;
    int fin_fd = -1;
    int fout_fd = -1;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd()");
        return -1;
    }

    if ((dirfd = open(cwd, O_RDONLY | O_DIRECTORY)) == -1) {
        perror("open()");
        return -1;
    }

    jb = calloc(1, sizeof(*jb));

    /* set standard input */
    if (pln->file_in != NULL) {
        if (pln->file_in->is_rel) {
            if ((fin_fd = openat(dirfd, pln->file_in->fname, O_RDONLY)) == -1) {
                perror(pln->file_in->fname);
                free(jb);
                close(dirfd);
                return -1;
            }
        } else {
            if ((fin_fd = open(pln->file_in->fname, O_RDONLY)) == -1) {
                perror(pln->file_in->fname);
                free(jb);
                close(dirfd);
                return -1;
            }
        }

        jb->stdin_fd = fin_fd;
    } else {
        jb->stdin_fd = shell_input_fd;
        fin_fd = jb->stdin_fd;
    }

    /* set standard output */
    if (pln->file_out != NULL) {

        if (pln->file_out->is_rel) {
            if ((fout_fd = openat(dirfd, pln->file_out->fname, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
                perror(pln->file_out->fname);
                if (fin_fd != -1 && fin_fd != STDIN_FILENO)
                    close(fin_fd);
                free(jb);
                close(dirfd);
                return -1;
            }
        } else {
            if ((fout_fd = open(pln->file_out->fname, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
                perror(pln->file_out->fname);
                if (fin_fd != -1 && fin_fd != STDIN_FILENO)
                    close(fin_fd);
                free(jb);
                close(dirfd);
                return -1;
            }
        }

        jb->stdout_fd = fout_fd;
    } else {
        jb->stdout_fd = STDOUT_FILENO;
        fout_fd = jb->stdout_fd;
    }

    jb->stderr_fd = STDERR_FILENO;

    jb->is_bg = pln->is_bg;

    /* cleanup */
    close(dirfd);

    struct proc **lastp = &jb->procs;

    size_t cmdline_size = 1;
    jb->cmdline = calloc(1, 1);

    /* now, create the processes */
    for (struct link *lnk = pln->procs->head;
            lnk != NULL;
            lnk = lnk->next) {
        struct an_process *anproc;
        struct proc *proc;

        anproc = lnk->data;

        proc = calloc(1, sizeof(*proc));
        proc->argv = calloc(anproc->num_args, sizeof(proc->argv[0]));
        for (size_t i=0; i<anproc->num_args; ++i) {
            proc->argv[i] = anproc->args[i] != NULL ? strdup(anproc->args[i]) : NULL;
            if (proc->argv[i] != NULL) {
                size_t arglen = strlen(proc->argv[i]) + 1;

                jb->cmdline = realloc(jb->cmdline, cmdline_size + arglen);
                strncat(&jb->cmdline[cmdline_size-1], proc->argv[i], arglen-1);
                cmdline_size += arglen;
            }
        }
        proc->name = proc->argv[0];

        *lastp = proc;
        lastp = &(*lastp)->next;
    }

    /* now create the actual processes */
    for (struct proc *p = jb->procs; p != NULL; p = p->next) {
        pid_t child_pid;
        int pipefds[2];

        /* we want to set up pipes first */
        if (p->next != NULL) {
            if (pipe(pipefds) < 0) {
                perror("pipe()");
                /* TODO: don't terminate:
                 * 1. kill all running processes
                 * 2. free all data
                 */
                exit(EXIT_FAILURE);
            }
            fout_fd = pipefds[1];
        } else
            fout_fd = jb->stdout_fd;

        intproc internal_proc = proc_internal_get(p->name);

        if (internal_proc != NULL) {
            (*internal_proc)(p->argv, fin_fd, fout_fd);
            p->finished = true;
        } else {
            /* now fork */
            child_pid = fork();
            if (child_pid < 0) {
                /* fork failed */
                perror("fork()");
                /* TODO: don't terminate */
                exit(EXIT_FAILURE);
            } else if (child_pid == 0) {
                /* child */
                proc_exec(p, jb->pgid, fin_fd, fout_fd, jb->stderr_fd, jb->is_bg);
            } else {
                /* parent */
                p->pid = child_pid;

                /* we only care about job control if we're
                 * on a tty */
                if (interactive) {
                    if (jb->pgid == 0)
                        jb->pgid = child_pid;
                    /* set child to belong to the job group */
                    setpgid(child_pid, jb->pgid);
                }
            }
        }

        /* close any streams we opened in this process that were
         * only meant for the subprocesses in our pipeline */
        if (fin_fd != jb->stdin_fd)
            close(fin_fd);
        if (fout_fd != jb->stdout_fd)
            close(fout_fd);

        /* set up input to be the input end of the last pipe */
        fin_fd = pipefds[0];
    }

    /* add to the list of jobs */
    jb->next = jobs;
    jobs = jb;

    /**
     * Don't wait for an internal job.
     */
    if (job_is_internal(jb))
        return 0;

    /* now we should wait for our job */
    if (!interactive)
        job_wait(jb);
    else if (jb->is_bg)
        job_background(jb, false);
    else
        job_foreground(jb, false);

    return 0;
}

bool job_stopped(const struct job *jb)
{
    for (struct proc *p = jb->procs; p != NULL; p = p->next)
        if (!p->stopped)
            return false;
    return true;
}

bool job_finished(const struct job *jb)
{
    for (struct proc *p = jb->procs; p != NULL; p = p->next)
        if (!p->finished)
            return false;
    return true;
}

static int proc_update(pid_t pid, int status) 
{
    struct proc *p = NULL;
    struct job *jb = NULL;
    int job_id = 1;

    if (pid > 0) {
        for (jb = jobs; jb != NULL; jb = jb->next) {
            for (p = jb->procs; p != NULL; p = p->next) {
                if (p->pid == pid) {
                    p->status = status;
                    if (WIFSTOPPED(status)) {
                        p->stopped = true;
                    } else if (WIFCONTINUED(status)) {
                        p->stopped = false;
                    } else {
                        p->finished = true;
                        if (WIFSIGNALED(status))
                            fprintf(stderr, "[%d] %d Terminated by signal %d.\n", job_id, (int) pid, WTERMSIG(status));
                    }
                    jb->notified = false;
                    return 0;
                }
            }
            ++job_id;
        }
        /* we did not find the process */
        fprintf(stderr, "[%d] %d not found.\n", job_id, (int) pid);
        return -1;
    } else if (pid == 0 || errno == ECHILD) {
        return -1;
    } else {
        perror("waitpid");
    }
    return -1;
}

bool job_is_internal(const struct job *jb)
{
    for (struct proc *p = jb->procs; p != NULL; p = p->next)
        if (p->pid != 0)
            return false;

    return true;
}

void job_wait(struct job *jb)
{
    int status;
    pid_t pid;

    /**
     * wait for all processes in the job to finish or stop
     */
    do {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);
    } while (proc_update(pid, status) == 0
            && !job_stopped(jb)
            && !job_finished(jb));
}

void job_background(const struct job *jb, bool to_continue)
{
    if (to_continue) {
        if (kill(-jb->pgid, SIGCONT) < 0)
            perror("kill");
    }
}

void job_foreground(struct job *jb, bool to_continue)
{
    /* If the job was suspended or in the background, 
     * it is no longer controlling the terminal.
     * Therefore we have to call this again. */
    if (tcsetpgrp(shell_input_fd, jb->pgid) < 0)
        fprintf(stderr, "Cannot set %d as the controller: %s\n", jb->pgid, strerror(errno));

    if (to_continue) {
        if (tcsetattr(shell_input_fd, TCSADRAIN, &jb->tmodes) < 0)
            perror("tcsetattr");
        if (kill(-jb->pgid, SIGCONT) < 0)
            perror("kill");
    }

    /* wait */
    job_wait(jb);

    /* job is either stopped or finished.
     * set this process as the tty controller */
    if (tcsetpgrp(shell_input_fd, shell_pgid) < 0)
        fprintf(stderr, "Cannot set %d as the controller: %s\n", shell_pgid, strerror(errno));

    /* save and restore terminal settings */
    tcgetattr(shell_input_fd, &jb->tmodes);
    tcsetattr(shell_input_fd, TCSADRAIN, &term_attrs);
}

void job_continue(struct job *jb, bool background)
{
    for (struct proc *p = jb->procs; p != NULL; p = p->next)
        p->stopped = false;

    /* we need to notify the user that the job's state has changed */
    jb->notified = false;

    jb->is_bg = background;
    if (background)
        job_background(jb, true);
    else
        job_foreground(jb, true);
}

void jobs_notifications(void)
{
    struct job **jb;
    int job_id;
    pid_t pid;
    int status;

    /* update job statuses */
    do {
        pid = waitpid(WAIT_ANY, &status, WCONTINUED | WUNTRACED | WNOHANG);
    } while (proc_update(pid, status) == 0);

    jb = &jobs;
    job_id = 1;
    while (*jb != NULL) {
        if (job_finished(*jb)) {
            struct job *job_temp = *jb;

            /* remove the job from the list */
            *jb = (*jb)->next;

            /* do this just for better debugging */
            job_temp->next = NULL;

            if (job_temp->is_bg)
                job_display(job_temp, true, false, job_id, STDOUT_FILENO);

            /* destroy the job */
            job_destroy(job_temp);
            continue;   /* don't iterate to next job */
        }
        
        if (!(*jb)->notified) {
            struct job *job_temp = *jb;

            /* notify the user about a recently stopped job */
            job_temp->notified = true;

            /* print information about each process in the job */
            job_display(job_temp, true, false, job_id, STDOUT_FILENO);
        }

        jb = &(*jb)->next;
        ++job_id;
    }
}

void job_destroy(struct job *jb)
{
    /* close file descriptors */
    if (jb->stdin_fd != shell_input_fd)
        close(jb->stdin_fd);
    if (jb->stdout_fd != STDOUT_FILENO)
        close(jb->stdout_fd);
    if (jb->stderr_fd != STDERR_FILENO)
        close(jb->stderr_fd);

    /* destroy process info */
    struct proc *p = jb->procs;
    while (p != NULL) {
        struct proc *p_next = p->next;

        for (size_t i = 0; p->argv[i] != NULL; ++i)
            free(p->argv[i]);
        free(p->argv);
        free(p);

        p = p_next;
    }

    free(jb->cmdline);

    /* finally, destroy the job */
    free(jb);
}
