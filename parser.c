#include "parser.h"
#include "ds/llist.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

/* TODO: implement environment variable substitution ? */

size_t num_lines = 0;

/**
 * Parse a quoted string, with {@delim} as the delimeter.
 */
static struct token *parse_string(const char **input, char delim)
{
    struct token *tk = calloc(1, sizeof(struct token));
    size_t string_length = 0;
    size_t buf_size = 1;

    tk->cat = delim == '"' ? CAT_STRING_DBL : CAT_STRING_SNGL;
    tk->str_data = malloc(buf_size);

    /* advance past the first quotation mark */
    (*input)++;

    while (**input != delim && **input != '\0') {
        char c = **input;
        char next_c = (*input)[1];

        if (c == '\\' && (next_c == '\\' || next_c == delim)) {
            tk->str_data[string_length] = next_c;

            if (++string_length >= buf_size) {
                buf_size *= 2;
                tk->str_data = realloc(tk->str_data, buf_size);
            }

            (*input) += 2;
        } else {
            tk->str_data[string_length] = c;

            if (++string_length >= buf_size) {
                buf_size *= 2;
                tk->str_data = realloc(tk->str_data, buf_size);
            }

            (*input)++;
        }
    }

    /* put trailing NUL byte */
    tk->str_data = realloc(tk->str_data, string_length + 1);
    tk->str_data[string_length] = '\0';

    if (**input == '\0') {
        /* we did not find a matching quotation mark */
        char msg[48];

        tk->cat = CAT_ERROR;
        free(tk->str_data);
        snprintf(msg, 48, "Expected '%c'", delim);
        tk->str_data = strdup(msg);
        return tk;
    }

    /* advance past the last quotation mark */
    (*input)++;

    return tk;
}

#define isop(c) (c == '|' || c == '&' || c == '<' || c == '>' || c == ';')

/**
 * Parses an argument, which may also be a relative or absolute path.
 */
static struct token *parse_arg(const char **input)
{
    struct token *tk = calloc(1, sizeof(struct token));
    size_t buf_size = 1;
    size_t string_length = 0;

    tk->cat = CAT_ARG;
    tk->str_data = malloc(buf_size);

    while (!isspace(**input) && !isop(**input) && **input != '\0') {
        char c = **input;
        char next_c = (*input)[1];

        if (c == '\\' && next_c != '\0') {
            tk->str_data[string_length] = next_c;

            if (next_c == '/')
                tk->cat = CAT_PATH_REL;

            if (++string_length >= buf_size) {
                buf_size *= 2;
                tk->str_data = realloc(tk->str_data, buf_size);
            }

            (*input) += 2;
        } else {
            tk->str_data[string_length] = c;

            if (c == '/')
                tk->cat = CAT_PATH_REL;

            if (++string_length >= buf_size) {
                buf_size *= 2;
                tk->str_data = realloc(tk->str_data, buf_size);
            }

            (*input)++;
        }
    }

    tk->str_data = realloc(tk->str_data, string_length + 1);
    tk->str_data[string_length] = '\0';

    if (tk->str_data[0] == '/')
        tk->cat = CAT_PATH_ABS;

    return tk;
}

struct llist *tokenize(const char **input)
{
    struct llist *tokens = list_new();
    size_t cur_line = 0;
    const char *in_base = *input;

    while (**input) {
        char c = **input;

        if (c == '|' || c == '&' || c == '<' || c == '>' || c  == ';' || c == '\n') {
            struct token *tk = calloc(1, sizeof(struct token));

            tk->str_data = malloc(2);
            tk->str_data[0] = c;
            tk->str_data[1] = '\0';
            tk->lineno = cur_line;
            tk->charno = *input - in_base;

            switch (c) {
                case '|':
                    tk->cat = CAT_PIPE;
                    break;
                case '&':
                    tk->cat = CAT_AMPERSAND;
                    break;
                case '<':
                    tk->cat = CAT_LANGLE;
                    break;
                case '>':
                    tk->cat = CAT_RANGLE;
                    break;
                case ';':
                    tk->cat = CAT_SEMICOLON;
                    break;
                case '\n':
                    tk->cat = CAT_NEWLINE;
                    cur_line++;
                    break;
                default:
                    /* we shouldn't get here */
                    tk->cat = CAT_ERROR;
                    break;
            }

            list_append(tokens, tk);

            (*input)++;
        } else if (isspace(c)) {
            (*input)++;
        } else {
            struct token *tk;
            size_t charno = *input - in_base;

            /* these parse routines advance the position in the input string */
            if (c == '"' || c == '\'')
                tk = parse_string(input, c);
            else
                tk = parse_arg(input);

            tk->charno = charno;
            tk->lineno = cur_line;
            list_append(tokens, tk);
        }
    }

    return tokens;
}

/**
 * Make a tree with zero children.
 */
static struct parse *make_tree0(enum prod type, struct token *token)
{
    struct parse *tree = calloc(1, sizeof(struct parse));
    tree->type = type;
    tree->token = token;

    return tree;
}

/**
 * Make a tree with one child.
 */
static struct parse *make_tree1(enum prod type, struct token *token, struct parse *child)
{
    struct parse *tree = make_tree0(type, token);
    tree->lchild = child;
    return tree;
}

/**
 * Make a tree with N children. This list must be NULL-terminated.
 */
static struct parse *make_treeN(enum prod type, struct token *token, struct parse *lchild, ...)
{
    struct parse *tree = make_tree1(type, token, lchild);
    va_list args;
    struct parse *arg;

    va_start(args, lchild);

    while ((arg = va_arg(args, struct parse *)) != NULL) {
        lchild->rsibling = arg;
        lchild = arg;
    }

    va_end(args);

    return tree;
}

static void tree_destroy(struct parse *tree)
{
    if (tree == NULL)
        return;

    tree_destroy(tree->rsibling);
    tree_destroy(tree->lchild);

    free(tree);
}

/**
 * Prepend a new error message onto the error list.
 */
static void errlist_ppnd(struct parse_error **err_listp,
        size_t lineno, size_t charno, char *message)
{
    struct parse_error *perr = calloc(1, sizeof(struct parse_error));

    perr->charno = charno;
    perr->lineno = lineno;
    perr->message = message;
    perr->next = *err_listp;

    *err_listp = perr;
}

static inline int match_NAME(const struct token *token) {
    return token->cat == CAT_ARG
        || token->cat == CAT_STRING_DBL
        || token->cat == CAT_STRING_SNGL
        || token->cat == CAT_PATH_ABS
        || token->cat == CAT_PATH_REL;
}

static struct parse *rdparse_NAME(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *child = NULL;
    struct token *cur_tk;

    if (*list == NULL || !match_NAME(cur_tk = (*list)->data)) {
        /* TODO */
        errlist_ppnd(err_listp, cur_tk->lineno, cur_tk->charno, 
                strdup("Expected an argument, a string, or a path."));
        return NULL;
    }

    child = make_tree0(PROD_TERMINAL, cur_tk);
    *list = (*list)->next;

    return make_tree1(PROD_NAME, NULL, child);
}

static struct parse *rdparse_ARGLIST(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_name = NULL;
    struct parse *ch_arglist = NULL;
    struct token *cur_tk = NULL;

    if (*list == NULL || !match_NAME(cur_tk = (*list)->data)) {
        /* epsilon */
        return make_tree0(PROD_ARGLIST, NULL);
    }

    if ((ch_name = rdparse_NAME(list, err_listp)) == NULL
     || (ch_arglist = rdparse_ARGLIST(list, err_listp)) == NULL) {
        tree_destroy(ch_name);
        tree_destroy(ch_arglist);
        /* TODO: error */
        return NULL;
    }

    return make_treeN(PROD_ARGLIST, NULL, ch_name, ch_arglist, NULL);
}

static struct parse *rdparse_AMP_OP(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_ampersand = NULL;
    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_AMPERSAND) {
        /* epsilon */
        return make_tree0(PROD_AMP_OP, NULL);
    }

    ch_ampersand = make_tree0(PROD_TERMINAL, cur_tk);
    (*list) = (*list)->next;

    return make_tree1(PROD_AMP_OP, NULL, ch_ampersand);
}

static struct parse *rdparse_STDIN_PIPE(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_langle = NULL;
    struct parse *ch_name = NULL;
    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_LANGLE) {
        /* epsilon */
        return make_tree0(PROD_STDIN_PIPE, NULL);
    }

    ch_langle = make_tree0(PROD_TERMINAL, cur_tk);
    (*list) = (*list)->next;

    if ((ch_name = rdparse_NAME(list, err_listp)) == NULL) {
        tree_destroy(ch_langle);
        tree_destroy(ch_name);
        /* TODO: error */
        return NULL;
    }

    return make_treeN(PROD_STDIN_PIPE, NULL, ch_langle, ch_name, NULL);
}

static struct parse *rdparse_STDOUT_PIPE(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_rangle = NULL;
    struct parse *ch_name = NULL;
    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_RANGLE) {
        /* epsilon */
        return make_tree0(PROD_STDOUT_PIPE, NULL);
    }

    ch_rangle = make_tree0(PROD_TERMINAL, cur_tk);
    (*list) = (*list)->next;

    if ((ch_name = rdparse_NAME(list, err_listp)) == NULL) {
        tree_destroy(ch_rangle);
        tree_destroy(ch_name);
        /* TODO: error */
        return NULL;
    }

    return make_treeN(PROD_STDOUT_PIPE, NULL, ch_rangle, ch_name, NULL);
}

static struct parse *rdparse_PIPELINE_TAIL(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_pipe = NULL;
    struct parse *ch_progname = NULL;
    struct parse *ch_arglist = NULL;
    struct parse *ch_pipeline_tail = NULL;
    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_PIPE) {
        /* epsilon */
        return make_tree0(PROD_PIPELINE_TAIL, NULL);
    }

    ch_pipe = make_tree0(PROD_TERMINAL, cur_tk);
    (*list) = (*list)->next;

    if ((ch_progname = rdparse_NAME(list, err_listp)) == NULL
     || (ch_arglist = rdparse_ARGLIST(list, err_listp)) == NULL
     || (ch_pipeline_tail = rdparse_PIPELINE_TAIL(list, err_listp)) == NULL) {
        tree_destroy(ch_pipe);
        tree_destroy(ch_progname);
        tree_destroy(ch_arglist);
        tree_destroy(ch_pipeline_tail);
        return NULL;
    }

    return make_treeN(PROD_PIPELINE_TAIL, NULL,
            ch_pipe, ch_progname, ch_arglist, ch_pipeline_tail, NULL);
}

static struct parse *rdparse_PIPELINE(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_progname = NULL;
    struct parse *ch_arglist = NULL;
    struct parse *ch_stdin_pipe = NULL;
    struct parse *ch_pipeline_tail = NULL;
    struct parse *ch_stdout_pipe = NULL;
    struct parse *ch_amp_op = NULL;

    if ((ch_progname = rdparse_NAME(list, err_listp)) == NULL
     || (ch_arglist = rdparse_ARGLIST(list, err_listp)) == NULL
     || (ch_stdin_pipe = rdparse_STDIN_PIPE(list, err_listp)) == NULL
     || (ch_pipeline_tail = rdparse_PIPELINE_TAIL(list, err_listp)) == NULL
     || (ch_stdout_pipe = rdparse_STDOUT_PIPE(list, err_listp)) == NULL
     || (ch_amp_op = rdparse_AMP_OP(list, err_listp)) == NULL) {
        tree_destroy(ch_progname);
        tree_destroy(ch_arglist);
        tree_destroy(ch_stdin_pipe);
        tree_destroy(ch_pipeline_tail);
        tree_destroy(ch_stdout_pipe);
        tree_destroy(ch_amp_op);

        return NULL;
    }

    return make_treeN(PROD_PIPELINE, NULL, 
            ch_progname, ch_arglist, ch_stdin_pipe, ch_pipeline_tail, ch_stdout_pipe, ch_amp_op, NULL);
}

static struct parse *rdparse_PLN_LIST(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_semicolon = NULL;
    struct parse *ch_pipeline = NULL;
    struct parse *ch_pln_list = NULL;

    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_SEMICOLON) {
        /* epsilon */
        return make_tree0(PROD_PLN_LIST, NULL);
    }

    ch_semicolon = make_tree0(PROD_TERMINAL, cur_tk);
    (*list) = (*list)->next;

    if ((ch_pipeline = rdparse_PIPELINE(list, err_listp)) == NULL
     || (ch_pln_list = rdparse_PLN_LIST(list, err_listp)) == NULL) {
        tree_destroy(ch_semicolon);
        tree_destroy(ch_pipeline);
        tree_destroy(ch_pln_list);
        return NULL;
    }

    return make_treeN(PROD_PLN_LIST, NULL, ch_semicolon, ch_pipeline, ch_pln_list, NULL);
}

static struct parse *rdparse_PROGRAM(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_pipeline = NULL;
    struct parse *ch_pln_list = NULL;
    struct token *cur_tk;

    if (*list == NULL || !match_NAME(cur_tk = (*list)->data)) {
        /* epsilon */
        return make_tree0(PROD_PROGRAM, NULL);
    }

    if ((ch_pipeline = rdparse_PIPELINE(list, err_listp)) == NULL
     || (ch_pln_list = rdparse_PLN_LIST(list, err_listp)) == NULL) {
        tree_destroy(ch_pipeline);
        /* I know this last tree_destroy statement is 
         * technically unnecessary, but I'm putting it here
         * in case I change the above code later on. */
        tree_destroy(ch_pln_list);
        /* TODO: error */
        return NULL;
    }

    return make_treeN(PROD_PROGRAM, NULL, ch_pipeline, ch_pln_list, NULL);
}

void errlist_destroy(struct parse_error *err_list)
{
    while (err_list != NULL) {
        struct parse_error *next = err_list->next;

        free(err_list->message);
        free(err_list);
        err_list = next;
    }
}

struct parse *rdparser(const char *input, struct parse_error **err_listp)
{
    struct llist *token_list;
    const char *end = input;
    struct parse *tree;
    const struct link *first_link;

    token_list = tokenize(&end);
    first_link = token_list->head;

    tree = rdparse_PROGRAM(&first_link, err_listp);

    /* cleanup */
    list_destroy(token_list, NULL);

    return tree;
}
