#include <stdio.h>
#include <stdlib.h>
#include "parser.h"

int main(int argc, char *argv[])
{
    char *line = NULL;
    size_t len = 0;

    while (getline(&line, &len, stdin) != -1) {
        struct llist *token_list = NULL;
        struct parse *tree = NULL;
        struct parse_error *err_list = NULL;

        tree = rdparser(line, &err_list, &token_list);

        prstree_debug(tree);

        if (err_list != NULL) {
            struct parse_error *err = err_list;

            while (err != NULL) {
                fprintf(stderr, "Line %zu, Position %zu, Parse error: %s\n", 
                        err->lineno, err->charno, err->message);
                err = err->next;
            }
        }

        list_destroy(token_list, (void (*)(void *))token_destroy);
        tree_destroy(tree);
        errlist_destroy(err_list);
    }

    free(line);

    return 0;
}
