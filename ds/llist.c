#include "llist.h"
#include <stdlib.h>
#include <assert.h>

struct llist *list_new(void)
{
    return calloc(1, sizeof(struct llist));
}

void list_append(struct llist *list, void *data)
{
    if (list->head == NULL) {
        list->head = calloc(1, sizeof(struct link));
        list->head->data = data;
        list->last = list->head;
    } else {
        struct link *last = list->last;
        list->last = calloc(1, sizeof(struct link));
        list->last->data = data;
        list->last->prev = last;
        last->next = list->last;
    }

    list->size++;
}

void list_prepend(struct llist *list, void *data)
{
    if (list->head == NULL) {
        list->head = calloc(1, sizeof(struct link));
        list->head->data = data;
        list->last = list->head;
    } else {
        struct link *head = list->head;
        list->head = calloc(1, sizeof(struct link));
        list->head->data = data;
        list->head->next = head;
        head->prev = list->head;
    }

    list->size++;
}

void *list_remove_end(struct llist *list)
{
    void *data;

    assert(list->head != NULL);

    if (list->head == list->last) {
        data = list->last->data;
        free(list->last);
        list->head = NULL;
        list->last = NULL;
    } else {
        struct link *last = list->last;
        data = last->data;
        list->last = last->prev;
        list->last->next = NULL;
        free(last);
    }

    list->size--;
    return data;
}

void *list_remove_start(struct llist *list)
{
    void *data;

    assert(list->head != NULL);

    if (list->head == list->last) {
        data = list->head->data;
        free(list->head);
        list->head = NULL;
        list->last = NULL;
    } else {
        struct link *head = list->head;
        data = list->head->data;
        list->head = head->next;
        list->head->prev = NULL;
        free(head);
    }

    list->size--;
    return data;
}

void list_destroy(struct llist *list, void (*dtor_func)(void *))
{
    struct link *link;
    struct link *oldlnk;

    if (list == NULL)
        return;

    link = list->head;
    while (link != NULL) {
        if (dtor_func != NULL)
            (*dtor_func)(link->data);
        oldlnk = link;
        link = oldlnk->next;
        free(oldlnk);
    }

    free(list);
}
