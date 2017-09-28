#include "parser.h"
#include "ds/llist.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h> /* fork(), exec() */

/* TODO: implement environment variable substitution ? */

size_t num_lines = 0;

void token_destroy(struct token *tk)
{
    free(tk->str_data);
    free(tk);
}

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
                    num_lines++;
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

void tree_destroy(struct parse *tree)
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
        size_t lineno, size_t charno, const char *message)
{
    struct parse_error *perr = calloc(1, sizeof(struct parse_error));

    perr->charno = charno;
    perr->lineno = num_lines + lineno;
    perr->message = strdup(message);
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
                "Expected an argument, a string, or a path.");
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
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

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
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

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
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

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
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

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
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

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

static struct parse *rdparse_LINE(const struct link **list,
        struct parse_error **err_listp);

static struct parse *rdparse_PLN_LIST(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_semicolon = NULL;
    struct parse *ch_line = NULL;

    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_SEMICOLON) {
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

        /* epsilon */
        return make_tree0(PROD_PLN_LIST, NULL);
    }

    ch_semicolon = make_tree0(PROD_TERMINAL, cur_tk);
    (*list) = (*list)->next;

    if ((ch_line = rdparse_LINE(list, err_listp)) == NULL) {
        tree_destroy(ch_semicolon);
        tree_destroy(ch_line);
        return NULL;
    }

    return make_treeN(PROD_PLN_LIST, NULL, ch_semicolon, ch_line, NULL);
}

static struct parse *rdparse_LINE(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_pipeline = NULL;
    struct parse *ch_pln_list = NULL;
    struct token *cur_tk;

    if (*list == NULL || !match_NAME(cur_tk = (*list)->data)) {
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

        /* epsilon */
        return make_tree0(PROD_LINE, NULL);
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

    return make_treeN(PROD_LINE, NULL, ch_pipeline, ch_pln_list, NULL);
}

static struct parse *rdparse_PROGRAM(const struct link **list,
        struct parse_error **err_listp);

static struct parse *rdparse_LINES_LIST(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_newline = NULL;
    struct parse *ch_program = NULL;
    struct token *cur_tk;

    if (*list == NULL || (cur_tk = (*list)->data)->cat != CAT_NEWLINE) {
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }

        /* epsilon */
        return make_tree0(PROD_LINES_LIST, NULL);
    }

    ch_newline = make_tree0(PROD_TERMINAL, cur_tk);
    *list = (*list)->next;

    if ((ch_program = rdparse_PROGRAM(list, err_listp)) == NULL) {
        tree_destroy(ch_newline);
        tree_destroy(ch_program);
        /* TODO: error */
        return NULL;
    }

    return make_treeN(PROD_LINES_LIST, NULL, ch_newline, ch_program, NULL);
}

static struct parse *rdparse_PROGRAM(const struct link **list,
        struct parse_error **err_listp)
{
    struct parse *ch_line = NULL;
    struct parse *ch_lines_list = NULL;
    struct token *cur_tk;

    if (*list == NULL || !match_NAME(cur_tk = (*list)->data)) {
        if (*list != NULL && cur_tk->cat == CAT_ERROR) {
            errlist_ppnd(err_listp, cur_tk->lineno, 
                    cur_tk->charno, cur_tk->str_data);
            /* error */
            return NULL;
        }
        /* epsilon */
        return make_tree0(PROD_PROGRAM, NULL);
    }

    if ((ch_line = rdparse_LINE(list, err_listp)) == NULL
     || (ch_lines_list = rdparse_LINES_LIST(list, err_listp)) == NULL) {
        tree_destroy(ch_line);
        tree_destroy(ch_lines_list);
        return NULL;
    }

    return make_treeN(PROD_PROGRAM, NULL, ch_line, ch_lines_list, NULL);
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

struct parse *rdparser(const struct llist *tokens, struct parse_error **err_listp)
{
    struct parse *tree;
    const struct link *first_link;

    first_link = tokens->head;

    tree = rdparse_PROGRAM(&first_link, err_listp);

    return tree;
}

const char *category_names[] = {
    [CAT_STRING_DBL] = "[string_dbl]",
    [CAT_STRING_SNGL] = "[string_sngl]",
    [CAT_PATH_ABS] = "[path_abs]",
    [CAT_PATH_REL] = "[path_rel]",
    [CAT_ARG] = "[argument]",
    [CAT_PIPE] = "|",
    [CAT_AMPERSAND] = "&",
    [CAT_LANGLE] = "<",
    [CAT_RANGLE] = ">",
    [CAT_SEMICOLON] = ";",
    [CAT_NEWLINE] = "[newline]",
    [CAT_ERROR] = "(parse error)"
};

const char *production_names[] = {
    [PROD_NAME] = "<name>",
    [PROD_ARGLIST] = "<arglist>",
    [PROD_AMP_OP] = "<amp_op>",
    [PROD_STDIN_PIPE] = "<stdin_pipe>",
    [PROD_STDOUT_PIPE] = "<stdout_pipe>",
    [PROD_PIPELINE] = "<pipeline>",
    [PROD_PIPELINE_TAIL] = "<pipeline_tail>",
    [PROD_PLN_LIST] = "<pln_list>",
    [PROD_LINE] = "<line>",
    [PROD_LINES_LIST] = "<lines_list>",
    [PROD_PROGRAM] = "<program>",
    [PROD_TERMINAL] = "(terminal)"
};

int prstree_empty(struct parse *tree)
{
    return tree == NULL || (tree->type != PROD_TERMINAL && tree->lchild == NULL);
}

static void prstree_debug2(struct parse *parent, 
        struct parse *tree, FILE *stream)
{
    if (tree->token != NULL) {
        fprintf(stream, "node%p [label=\"%s\"];\n", tree,
                category_names[tree->token->cat]);
        if (match_NAME(tree->token)) {
            fprintf(stream, "node%p_str_data [label=\"%s\"];\n",
                    tree, tree->token->str_data);
            fprintf(stream, "node%p -> node%p_str_data;\n", tree, tree);
        }
    } else {
        fprintf(stream, "node%p [label=\"%s\"];\n", tree, 
                    production_names[tree->type]);
    }
    if (parent != NULL)
        fprintf(stream, "node%p -> node%p;\n", parent, tree);

    if (!prstree_empty(tree)) {
        struct parse *child = tree->lchild;

        while (child != NULL) {
            prstree_debug2(tree, child, stream);
            child = child->rsibling;
        }
    }
}

void prstree_debug(struct parse *tree)
{
    /* set up file */
    char fname[1024], fname2[1024];
    FILE *stream;

    if (tree == NULL)
        return;

    tmpnam(fname);
    stream = fopen(fname, "a");
    tmpnam(fname2);
    fprintf(stream, "digraph G {\n");
    prstree_debug2(NULL, tree, stream);
    fprintf(stream, "}");

    fclose(stream);
}
