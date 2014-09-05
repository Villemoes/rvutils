#ifndef GRAPH_H_INCLUDED
#define GRAPH_H_INCLUDED

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <obstack.h>
#include <sys/queue.h>

/*
 * A rather straight-forward "append-only" graph implementation. It is
 * somewhat memory-efficient; an edge only uses 16+epsilon bytes, and
 * a node uses 44+(length of identifier)+epsilon.
 */


#define GRAPH_UNDIRECTED 0x01 /* orient all edges canonically */
#define GRAPH_NOPARALLEL 0x02 /* disallow adding the same edge twice */
#define GRAPH_NOLOOP     0x04 /* disallow self-edges */
#define GRAPH_DUAL       0x08 /* add two copies of each edge (incompatible with GRAPH_UNDIRECTED) */

/*
 * GRAPH_UNDIRECTED is mostly useful together with GRAPH_NOPARALLEL,
 * e.g. to make sure that in 'graph_add_edge("foo", "bar");
 * graph_add_edge("bar", "foo");', the second call is a no-op.
 */

struct Graph;
struct Component;
struct Edge;
struct Node;
struct Clique;

TAILQ_HEAD(ComponentHead, Component);
SLIST_HEAD(NodeBucket, Node);
STAILQ_HEAD(NodeHead, Node);
SLIST_HEAD(EdgeHead, Edge);

/*
 * The structs below are exposed to facilitate the use of callback
 * functions (without having to provide accessors for the "public"
 * fields). This is C. You're supposed to know what you're doing.
 */


struct Graph {
	struct ComponentHead   components;
	struct NodeBucket      *nodes;     /* array of buckets */
	uint32_t               hashmask;

	unsigned               flags;

	uint32_t               node_count;
	uint32_t               resize_threshold;

	struct obstack         node_os;
	struct obstack         edge_os;
};

struct Component {
	TAILQ_ENTRY(Component)  list;
	struct NodeHead         nodes;

	uint32_t    node_count;
	uint32_t    edge_count;
};

struct Edge {
	SLIST_ENTRY(Edge)  nodelink;
	/* struct Node        *src; */
	struct Node        *tgt;
};

struct Node {
	SLIST_ENTRY(Node)  hashlink;  /* used by the hash table in struct Graph */
	STAILQ_ENTRY(Node) complink;  /* used by the STAILQ in struct Component */
	struct Component   *comp;     /* the component this node belongs to */
	struct EdgeHead    out_edges; /* head of list of outgoing edges */
	uint32_t           out_degree;
	uint32_t           in_degree;
	uint32_t           hv;        /* hash value of ident */
	char               ident[];   /* identifying string */
};


/**
 * graph_init - initialize a struct Graph
 *
 * @g: The graph to initialize
 * @flags: Bitwise OR of GRAPH_* macros defined above.
 *
 * Returns: 0 on success, -1 on failure.
 */

int graph_init(struct Graph *g, unsigned flags);

/**
 * graph_destroy - destroy a graph
 *
 * This frees all the memory allocated to the graph @g.
 */
void graph_destroy(struct Graph *g);

/**
 * graph_add_edge - add an edge to the graph
 *
 * @s1: string identifying the source
 * @s2: string identifying the target
 *
 * Returns:
 *   -1: Error
 *    0: GRAPH_NOPARALLEL is set and the edge is already present
 *    0: GRAPH_NOLOOP is set and s1 and s2 refer to the same node
 *    1: otherwise
 */
int graph_add_edge(struct Graph *g, const char *s1, const char *s2);

/**
 * graph_add_node - add a node to the graph
 *
 * @nstr: string identifying the node
 *
 * Returns: -1 on error, 0 if the node was already present, 1
 * otherwise (in which case the node necessarily starts out as an
 * isolated component).
 */
int graph_add_node(struct Graph *g, const char *nstr);

/**
 * graph_add_file - read a graph from a file
 *
 * Read fp until EOF. Each line should consist of (whitespace
 * separated) fields. A line containing one field contributes a node
 * identified by that string; a line containing two fields contributes
 * an edge connecting the two nodes. Fields beyond the first two are
 * ignored.
 *
 * Returns: 0 on success, -1 on any failure.
 */
int graph_add_file(struct Graph *g, FILE *fp);


/* Return true if there is an edge from src to tgt. */
bool graph_edge_exists(const struct Node *src, const struct Node *tgt);

/*
 * Various routines implemented using used-supplied callbacks.
 *
 * All callback functions are supposed to adhere to the convention
 * that if they return non-zero, the iteration is stopped and the
 * iterator returns that non-zero value immediately. The iterators may
 * also return early for internal reasons, in which case -1 is
 * returned. On normal completion, 0 is returned.
 */

/* Iterate over the components of a graph. */
int graph_iterate_components(const struct Graph *g, int (*cb)(const struct Component *comp, void *ctx), void *ctx);

/* Iterate over the nodes of a graph. */
int graph_iterate_nodes(const struct Graph *g, int (*cb)(const struct Node *node, void *ctx), void *ctx);
/* Iterate over the edges of a graph. The edges are supplied as a pair of source and target node. */
int graph_iterate_edges(const struct Graph *g, int (*cb)(const struct Node *src, const struct Node *tgt, void *ctx), void *ctx);

/* Iterate over the nodes of a component. */
int component_iterate_nodes(const struct Component *comp, int (*cb)(const struct Node *node, void *ctx), void *ctx);
/* Iterate over the edges of a component. The edges are supplied as a pair of source and target node. */
int component_iterate_edges(const struct Component *comp, int (*cb)(const struct Node *src, const struct Node *tgt, void *ctx), void *ctx);


#endif /* !GRAPH_H_INCLUDED */
