#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "analyzer.h"
#include "shell.h"

int main(int argc, char *argv[])
{
    char *line = NULL;
    size_t len = 0;

    pcfsh_init();
    pcfsh_prefix(NULL);

    while (getline(&line, &len, stdin) != -1) {
        struct llist *token_list = NULL;
        struct parse *tree = NULL;
        struct parse_error *err_list = NULL;
        const char *after = line;
        struct llist *pipelines = NULL;

        /* parse the current line */
        token_list = tokenize(&after);
        tree = rdparser(token_list, &err_list);

#ifdef PARSETREE_DEBUG
        prstree_debug(tree);
#endif

        if (err_list != NULL) {
            struct parse_error *err = err_list;

            while (err != NULL) {
                fprintf(stderr, "Line %zu, Position %zu, Parse error: %s\n", 
                        err->lineno, err->charno, err->message);
                err = err->next;
            }
        } else {
            /* if parsing went well, analyze it */
            pipelines = analyze_pipelines(tree);

            /* execute all pipelines */
            for (struct link *lnk = pipelines->head; lnk != NULL; lnk = lnk->next)
                job_exec(lnk->data);
        }

        /* cleanup */
        list_destroy(token_list, (void (*)(void *))token_destroy);
        tree_destroy(tree);
        errlist_destroy(err_list);
        list_destroy(pipelines, (void (*)(void *))an_pipeline_destroy);

        /* update statuses and get notifications */
        jobs_notifications();
        /* shell prefix */
        pcfsh_prefix(NULL);
    }

    free(line);

    /**
     * jobs_cleanup() should be called here.
     */
    return 0;
}
