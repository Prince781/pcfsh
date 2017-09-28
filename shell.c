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
                perror("openat()");
                free(jb);
                close(dirfd);
                return -1;
            }
        } else {
            if ((fin_fd = open(pln->file_in->fname, O_RDONLY)) == -1) {
                perror("open()");
                free(jb);
                close(dirfd);
                return -1;
            }
        }

        jb->stdin_fd = fin_fd;
    } else
        jb->stdin_fd = shell_input_fd;

    /* set standard output */
    if (pln->file_out != NULL) {

        if (pln->file_out->is_rel) {
            if ((fout_fd = openat(dirfd, pln->file_out->fname, O_WRONLY | O_CREAT | O_TRUNC)) == -1) {
                perror("openat()");
                if (fin_fd != -1)
                    close(fin_fd);
                free(jb);
                close(dirfd);
                return -1;
            }
        } else {
            if ((fout_fd = open(pln->file_out->fname, O_WRONLY | O_CREAT | O_TRUNC)) == -1) {
                perror("open()");
                if (fin_fd != -1)
                    close(fin_fd);
                free(jb);
                close(dirfd);
                return -1;
            }
        }

        jb->stdout_fd = fout_fd;
    } else
        jb->stdout_fd = STDOUT_FILENO;

    jb->stderr_fd = STDERR_FILENO;

    jb->is_bg = pln->is_bg;

    /* cleanup */
    close(dirfd);

    struct proc **lastp = &jb->procs;

    /* now, create the processes */
    for (struct link *lnk = pln->procs->head;
            lnk != NULL;
            lnk = lnk->next) {
        struct an_process *anproc;
        struct proc *proc;

        anproc = lnk->data;

        proc = calloc(1, sizeof(*proc));
        proc->argv = calloc(anproc->num_args, sizeof(proc->argv[0]));
        for (size_t i=0; i<anproc->num_args; ++i)
            proc->argv[i] = anproc->args[i] != NULL ? strdup(anproc->args[i]) : NULL;
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
            if (pipe(pipefds) == -1) {
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

        /* now fork */
        child_pid = fork();
        if (child_pid == -1) {
            /* fork failed */
            perror("fork()");
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

    /* now we should wait for our job */
    if (!interactive)
        job_wait(jb);
    else if (jb->is_bg)
        job_background(jb, false);
    else
        job_foreground(jb, false);

    return 0;
}

static int proc_update(pid_t pid, int status, struct job *jb_compare)
{
    struct proc *p = NULL;
    struct job *jb = NULL;

    if (pid > 0) {
        for (jb = jobs; jb != NULL; jb = jb->next)
            for (p = jb->procs; p != NULL; p = p->next) {
                if (p->pid == pid) {
                    p->status = status;
                    if (WIFSTOPPED(status)) {
                        p->stopped = true;
                        fprintf(stderr, "%d: Stopped.\n", (int) pid);
                    } else {
                        p->finished = true;
                        if (WIFSIGNALED(status))
                            fprintf(stderr, "%d: Terminated by signal %d.\n", (int) pid, WTERMSIG(status));
                    }
                    if (jb != jb_compare)
                        jb->changed = true;
                    return 0;
                }
            }
    } else if (pid == 0 || errno == ECHILD) {
        return -1;
    } else {
        perror("waitpid");
    }
    return -1;
}

void job_wait(struct job *jb)
{
    int status;
    pid_t pid;

    do {
        pid = waitpid(WAIT_ANY, &status, WCONTINUED | WUNTRACED);
    } while (proc_update(pid, status, jb) != 0
            && !jb->procs->stopped
            && !jb->procs->finished);
}

void job_background(const struct job *jb, bool to_continue)
{
    if (to_continue) {
        kill(-jb->pgid, SIGCONT);
    }
}

void job_foreground(struct job *jb, bool to_continue)
{
    /* if the job is suspended, it is no longer controlling
     * the terminal */

    if (interactive)
        tcsetpgrp(shell_input_fd, jb->pgid);

    if (to_continue) {
        kill(-jb->pgid, SIGCONT);
    }

    /* wait */
    job_wait(jb);

    if (interactive) {
        /* job is either stopped or finished.
         * set this process as the tty controller */
        tcsetpgrp(shell_input_fd, shell_pgid);

        /* save and restore terminal settings */
        tcgetattr(shell_input_fd, &jb->tmodes);
        tcsetattr(shell_input_fd, TCSADRAIN, &term_attrs);
    }
}

void job_continue(struct job *jb, bool background)
{
    for (struct proc *p = jb->procs; p != NULL; p = p->next)
        p->stopped = false;

    jb->is_bg = background;
    if (background)
        job_background(jb, true);
    else
        job_foreground(jb, true);
}

void jobs_notifications(void)
{
    struct job **jb;

    jb = &jobs;
    while (*jb != NULL) {
        if ((*jb)->changed) {
            int status = (*jb)->procs->status;

            if (WIFSTOPPED(status))
                fprintf(stderr, "%d: Stopped.\n", (int) (*jb)->pgid);
            else if (WIFCONTINUED(status))
                fprintf(stderr, "%d: Continuing.\n", (int) (*jb)->pgid);
            else if (WIFSIGNALED(status))
                fprintf(stderr, "%d: Terminated by signal %d.\n", (int) (*jb)->pgid, WTERMSIG(status));

            (*jb)->changed = false;
        }

        if ((*jb)->procs->finished) {
            struct job *job_temp = *jb;

            /* remove the job from the list */
            *jb = (*jb)->next;

            /* delete the job */
            job_temp->next = NULL;

            /* close file descriptors */
            if (job_temp->stdin_fd != shell_input_fd)
                close(job_temp->stdin_fd);
            if (job_temp->stdout_fd != STDOUT_FILENO)
                close(job_temp->stdout_fd);
            if (job_temp->stderr_fd != STDERR_FILENO)
                close(job_temp->stderr_fd);

            /* destroy process info */
            struct proc *p = job_temp->procs;
            while (p != NULL) {
                struct proc *p_next = p->next;

                for (size_t i = 0; p->argv[i] != NULL; ++i)
                    free(p->argv[i]);
                free(p);

                p = p_next;
            }

            /* finally, destroy the job */
            free(job_temp);
        } else
            jb = &(*jb)->next;
    }
}
