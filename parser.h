#ifndef PARSER_H
#define PARSER_H

/**
 * This is a parser for the shell script language. It is split into 
 * a tokenizer (scanner) and a parser. The "semantic analysis" is 
 * done in the interpreter.
 */

#include <stddef.h>
#include "ds/llist.h"

/*** Tokenizer part ***/

/**
 * This is a token category.
 */
enum tcat {
    /**
     * A string, like "...", with anything between two double-quotes, except for a newline.
     */
    CAT_STRING_DBL,
    /**
     * A single-quoted string.
     */
    CAT_STRING_SNGL,
    /**
     * An absolute path, starting with "/".
     * Examples: /usr/bin/bash, /usr/bin/gedit,
     */
    CAT_PATH_ABS,
    /**
     * A relative path, not starting with "/".
     * Examples: bin/bash, gedit
     */
    CAT_PATH_REL,
    /**
     * Any sequence of non-whitespace characters that is neither "|", "&", "<", ">", nor ";"
     */
    CAT_ARG,
    /**
     * A pipe operator (|).
     */
    CAT_PIPE,
    /**
     * An ampersand operator (&)
     */
    CAT_AMPERSAND,
    /**
     * Left angle bracket (<)
     */
    CAT_LANGLE,
    /**
     * Right angle bracket (>)
     */
    CAT_RANGLE,
    /**
     * Semicolon (;)
     */
    CAT_SEMICOLON,
    /**
     * Newline
     */
    CAT_NEWLINE,
    /**
     * This is an error.
     */
    CAT_ERROR
};

/**
 * A structure representing the token.
 */
struct token {
    /**
     * The type of token.
     */
    enum tcat cat;
    /**
     * The string of the token.
     */
    char *str_data;
    /** The line number. **/
    size_t lineno;
    /** The character number on this line. **/
    size_t charno;
};

/**
 * Current total number of read lines.
 */
extern size_t num_lines;

/**
 * Returns a NULL-terminated array of tokens.
 * Advances *{@input} right after the last token.
 */
struct llist *tokenize(const char **input);

/** end of tokenizer stuff **/

/** start of parser stuff **/
/**
 * Here is our grammar:
 * <name> -> [ARGUMENT] | [STRING] | [PATH] (these are terminals)
 * <arglist> -> <name> <arglist> | e
 * <amp_op> -> [AMPERSAND] | e
 * <stdin_pipe> -> [LANGLE] <name> | e
 * <stdout_pipe> -> [RANGLE] <name> | e
 * <pipeline> -> <name> <arglist> <stdin_pipe> <pipeline_tail> <stdout_pipe> <amp_op>
 * <pipeline_tail> -> [PIPE] <progname> <arglist> <pipeline_tail> | e
 * <pln_list> -> [SEMICOLON] <pipeline> <pln_list> | e
 * <program> -> <pipeline> <pln_list> | e
 */

/* represents a production */
enum prod {
    PROD_NAME,
    PROD_ARGLIST,
    PROD_AMP_OP,
    PROD_STDIN_PIPE,
    PROD_STDOUT_PIPE,
    PROD_PIPELINE,
    PROD_PIPELINE_TAIL,
    PROD_PLN_LIST,
    PROD_PROGRAM,
    /* for when we are at a leaf */
    PROD_TERMINAL
};

struct parse_error {
    size_t lineno;
    size_t charno;
    char *message;
    struct parse_error *next;
};

/**
 * A n-ary parse tree.
 */
struct parse {
    enum prod type;
    /**
     * is non-NULL only when type == PROD_TERMINAL
     */
    struct token *token;
    struct parse *lchild;
    struct parse *rsibling;
};
/** end of parser stuff **/

/**
 * Given a stream of input, returns a parse tree.
 * If parsing failed, 
 */
struct parse *rdparser(const char *input);

#endif
