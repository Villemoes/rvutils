#ifndef CLIQUE_H_INCLUDED
#define CLIQUE_H_INCLUDED

#include <stdbool.h>

#include "graph.h"


/* 
 * Generate all maximal cliques, calling the given callback for each
 * one with an additional user-provided context argument. If callback
 * ever returns non-zero, returns immediately with that return
 * value. May also return -1 on some internal allocation
 * error. Otherwise, returns 0 on succesful completion.
 *
 * The callback function should not store copies of the nodes argument;
 * the array should be copied if necessary.
 *
 * The struct Graph must have been created with all of the flags
 * GRAPH_NOLOOP|GRAPH_DUAL|GRAPH_NOPARALLEL.
 *
 */


int
component_iterate_maximal_cliques(const struct Component *comp, int (*cb)(const struct Node **nodes, size_t count, void *ctx), void *ctx);

int
graph_iterate_maximal_cliques(const struct Graph *gra, int (*cb)(const struct Node **nodes, size_t count, void *ctx), void *ctx);


#endif /* !CLIQUE_H_INCLUDED */
