#include "analyzer.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void an_process_destroy(struct an_process *process)
{
    if (process == NULL)
        return;

    for (int i=0; process->args[i] != NULL; ++i)
        free(process->args[i]);

    free(process->args);
    free(process->progname.fname);
    free(process);
}

void an_pipeline_destroy(struct an_pipeline *pipeline)
{
    if (pipeline == NULL)
        return;

    if (pipeline->file_in != NULL)
        free(pipeline->file_in->fname);

    if (pipeline->file_out != NULL)
        free(pipeline->file_out->fname);

    list_destroy(pipeline->procs, (void (*)(void *))an_process_destroy);
    free(pipeline);
}

/**
 * We parse a <name> <arglist>
 */
static struct an_process *get_process(struct parse *tree)
{
    struct an_process *proc;
    struct parse *child;
    struct parse *sibling;
    struct llist *proc_args;
    char *progname;
    bool pname_rel;

    assert(tree->type == PROD_NAME);
    child = tree->lchild;
    assert(child->type == PROD_TERMINAL);

    /* get the command name */
    progname = child->token->str_data;
    pname_rel = child->token->cat == CAT_PATH_REL || progname[0] == '/';

    /* allocate space */
    proc = calloc(1, sizeof(*proc));
    proc_args = list_new();

    proc->progname.fname = strdup(progname);
    proc->progname.is_rel = pname_rel;

    /* build a list of all arguments */
    sibling = tree->rsibling;
    while (!prstree_empty(sibling)) {
        child = sibling->lchild;
        if (sibling->type == PROD_ARGLIST) {
            sibling = child;
            continue;
        }
        assert(sibling->type == PROD_NAME);
        assert(child->type == PROD_TERMINAL);
        list_append(proc_args, strdup(child->token->str_data));
        sibling = sibling->rsibling;
    }

    proc->num_args = proc_args->size + 2;

    /* convert list of strings to array of strings */
    char **arg_arr = calloc(proc->num_args, sizeof(*arg_arr));

    arg_arr[0] = strdup(proc->progname.fname);
    for (size_t i=1; i<proc->num_args-1; ++i)
        arg_arr[i] = list_remove_start(proc_args);
    
    /**
     * Since we used calloc, the argument list is NULL-terminated.
     * arg_arr[proc->num_args - 1] == NULL
     */

    proc->args = arg_arr;

    /* cleanup */
    list_destroy(proc_args, NULL);

    return proc;
}

static struct an_pipeline *get_pipeline(struct parse *tree)
{
    struct an_pipeline *pipeline;
    struct an_process *proc;
    struct llist *pathnodes;
    struct parse *child;

    assert(tree->type == PROD_PIPELINE);
    pipeline = calloc(1, sizeof(*pipeline));
    pipeline->procs = list_new();

    /* get arguments to first process */
    child = tree->lchild; /* at <name> */
    proc = get_process(child);
    list_append(pipeline->procs, proc);

    child = child->rsibling; /* at <arglist> */

    child = child->rsibling; /* at <stdin_pipe> */
    if (!prstree_empty(child)) {
        struct parse *child2;

        /* this takes us to the <name> */
        child2 = child->lchild->rsibling;
        assert(child2->type == PROD_NAME);
        /* this takes us to the terminal inside <name> */
        child2 = child2->lchild;

        pipeline->file_in = calloc(1, sizeof(*pipeline->file_in));
        pipeline->file_in->fname = strdup(child2->token->str_data);
        pipeline->file_in->is_rel = 
            child2->token->cat == CAT_PATH_REL || pipeline->file_in->fname[0] == '/';
    }

    /* advance to <pipeline_tail> */
    child = child->rsibling;

    /* get all subsequent processes */
    pathnodes = list_new();
    list_append(pathnodes, child);

    while (pathnodes->size != 0) {
        struct parse *node = list_remove_end(pathnodes);

        switch (node->type) {
            case PROD_PIPELINE_TAIL:
                {
                    struct parse *node_child;

                    node_child = node->lchild;
                    while (node_child != NULL) {
                        list_append(pathnodes, node_child);
                        node_child = node_child->rsibling;
                    }
                }
                break;
            case PROD_TERMINAL:
            case PROD_ARGLIST:
                /* do nothing */
                break;
            case PROD_NAME:
                {
                    proc = get_process(node);
                    list_append(pipeline->procs, proc);
                }
                break;
            default:
                /* TODO: error */
                assert(0);
                break;
        }
    }

    /* advance past <pipeline_tail> to <stdout_pipe> */
    child = child->rsibling;

    if (!prstree_empty(child)) {
        struct parse *child2;

        /* this takes us to the <name> */
        child2 = child->lchild->rsibling;
        assert(child2->type == PROD_NAME);
        /* this takes us to the terminal inside <name> */
        child2 = child2->lchild;

        pipeline->file_out = calloc(1, sizeof(*pipeline->file_out));
        pipeline->file_out->fname = strdup(child2->token->str_data);
        pipeline->file_out->is_rel = 
            child2->token->cat == CAT_PATH_REL || pipeline->file_out->fname[0] == '/';
    }

    /* advance to <amp_op> */
    child = child->rsibling;

    pipeline->is_bg = !prstree_empty(child);

    /* cleanup */
    list_destroy(pathnodes, NULL);

    return pipeline;
}

struct llist *analyze_pipelines(struct parse *tree)
{
    struct llist *pipelines;
    struct llist *pathnodes;

    pipelines = list_new();
    pathnodes = list_new();

    list_append(pathnodes, tree);

    while (pathnodes->size != 0) {
        struct parse *node = list_remove_end(pathnodes);

        switch (node->type) {
            case PROD_PROGRAM:
            case PROD_LINE:
            case PROD_LINES_LIST:
            case PROD_PLN_LIST:
                {
                    struct parse *child;

                    child = node->lchild;
                    while (child != NULL) {
                        list_append(pathnodes, child);
                        child = child->rsibling;
                    }
                }
                break;
            case PROD_PIPELINE:
                {
                    struct an_pipeline *pln;

                    pln = get_pipeline(node);
                    list_append(pipelines, pln);
                }
                break;
            case PROD_TERMINAL:
                /* do nothing */
                break;
            default:
                /* TODO: error */
                assert(0);
                break;
        }
    }

    /* cleanup */
    list_destroy(pathnodes, NULL);
    
    return pipelines;
}
