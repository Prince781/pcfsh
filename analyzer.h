#ifndef ANALYZER_H
#define ANALYZER_H

#include "parser.h"
#include "ds/llist.h"
#include <stdbool.h>
#include <stddef.h>

struct an_path {
    /**
     * Path to the program.
     */
    char *fname;
    /**
     * If the program name is a relative path.
     */
    bool is_rel;
};

struct an_process {
    /**
     * The name of the file to execute.
     */
    struct an_path progname;
    /**
     * A list of arguments. NULL-terminated.
     */
    char **args;
    /**
     * The total number of arguments.
     */
    size_t num_args;
};

struct an_pipeline {
    /**
     * The file to read in.
     * If this is NULL, then stdin will be the default input stream. 
     */
    struct an_path *file_in;

    /**
     * The file to write out.
     * If this is NULL, then stdout will be the default output stream.
     */
    struct an_path *file_out;

    /**
     * If this pipeline is to be run in the background.
     */
    bool is_bg;

    /**
     * A list of {struct an_process}es. The contents of this list
     * are managed internally and should not be free()d.
     */
    struct llist *procs;
};

/**
 * Frees all data allocated by a {struct an_pipeline}.
 */
void an_pipeline_destroy(struct an_pipeline *pipeline);

/**
 * Analyzes the syntax tree and returns a list of pipelines to execute.
 */
struct llist *analyze_pipelines(struct parse *tree);

#endif
