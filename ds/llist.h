#ifndef LLIST_H
#define LLIST_H

#include <stddef.h>

struct link {
    void *data;
    struct link *next;
    struct link *prev;
};

struct llist {
    size_t size;
    struct link *head;
    struct link *last;
};

/**
 * Creates an empty list.
 */
struct llist *list_new(void);

/**
 * Inserts {@data} at the end of the list.
 */
void list_append(struct llist *list, void *data);

/**
 * Inserts {@data} at the beginning of the list.
 */
void list_prepend(struct llist *list, void *data);

/**
 * Remove the last element of the list and return it.
 */
void *list_remove_end(struct llist *list);

/**
 * Remove the first element of the list and return it.
 */
void *list_remove_start(struct llist *list);

/**
 * free every element in this list and call a destructor function,
 * {@dtor_func} on each record if {@dtor_func} != NULL.
 */
void list_destroy(struct llist *list, void (*dtor_func)(void *));

#endif
