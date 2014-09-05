#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <obstack.h>
#include <assert.h>
#include <error.h>
#include <errno.h>

#include "graph.h"
#include "jenkins_hash.h"

/* <sys/queue.h> on most Linux systems seem to lack this. */
#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
        for ((var) = TAILQ_FIRST((head));                               \
            (var) && ((tvar) = TAILQ_NEXT((var), field), 1);            \
            (var) = (tvar))
#endif


/* Specification */

/* 
 * A node is uniquely identified by a nul-terminated string. (For the
 * command line utilities built on top of the library, we probably
 * also implicitly assume that the strings are sane, meaning that they
 * only consist of 7-bit ascii in the range 33-126 (no
 * whitespace!)). An edge is an ordered pair of (not necessarily
 * distinct) nodes. Two edges are adjacent if they share a node. A
 * graph is a set of nodes and a collection (multiset) of edges, all
 * of whose endpoints belong to the graph's set of nodes. The
 * components of a graph is the transitive closure of the adjacency
 * relation on its edges. (For a directed graph, this is not the same
 * as mutual reachability).
 */

/* Implementation */

/* 
 * A Graph is mainly a tail queue of Components. Each Component is
 * individually malloc'ed and, when two components need to be
 * collapsed due to insertion of an edge connecting the two, free'd. A
 * Graph also contains a hash table consisting of the set of nodes in
 * the graph. A graph can be undirected, which we implement by forcing
 * edges to be oriented canonically (see below). Finally, a Graph
 * contains two obstacks for handling the memory bookkeeping for nodes
 * and edges. This detail has two nice consequences: First, malloc
 * overhead is essentially eliminated, which means that edges only
 * take up half the space they otherwise would (glibc's smallest
 * allocatable chunk is 24 bytes + 8 bytes overhead). Second, we
 * cannot possibly leak any memory; all the memory ever allocated to a
 * graph is easily accessible and freeable from the struct Graph
 * itself.
 *
 * A Component contains the bookkeeping field chaining the components
 * together in the Graph's tail queue. It also heads a singly-linked
 * tail queue consisting of the nodes it contains.
 *
 * A Node obviously needs to store the string which identifies
 * it. Since that string can be of arbitrary length, we make it a
 * flexible array member. Each Node knows which component it belongs
 * to. Additionally, each Node is an element of the singly-linked list
 * constituting the appropriate hash bucket in the Graph's hash table
 * of Nodes, and the singly-linked tail queue of nodes belonging to a
 * particular component. A node also heads a singly-linked list of
 * edges having that node as source, and contains counters for
 * in-degree and out-degree.
 *
 * An Edge is the simplest of the data structures. It simply contains
 * a pointer to its target, and a bookkeeping field for being inserted
 * in the source Node's list of outgoing edges. We don't explicitly
 * remember the source node, as the only way to get a struct Edge* is
 * through the list of outgoing edges from the source. Also, the
 * component can be found from either the target or source node.
 *
 */

/*
 * At some point, I should steal the obstack implementation and make a
 * version which does not require the underlying allocator to succeed
 * or abort. I'd also like to get rid of some of the compatibility
 * crap.
 */


#define obstack_chunk_alloc  xmalloc
#define obstack_chunk_free   free

static void*
xmalloc(size_t s)
{
	void *p = malloc(s);
	if (p != NULL)
		return p;
  
	error(2, errno, "malloc()");
	abort(); 
}

/* Small convenient node methods. */
static int nodes_cmp(const struct Node *n1, const struct Node *n2)
{
	if (n1->hv < n2->hv)
		return -1;
	if (n1->hv > n2->hv)
		return 1;
	return strcmp(n1->ident, n2->ident);
}


static struct Component*
graph_new_component(struct Graph *g)
{
	struct Component *c = malloc(sizeof *c);
	if (c == NULL)
		return NULL;
	STAILQ_INIT(&c->nodes);
	c->node_count = 0;
	c->edge_count = 0;
	TAILQ_INSERT_TAIL(&g->components, c, list);
	return c;
}

static void
graph_remove_component(struct Graph *g, struct Component *c)
{
	/* should add sanity check that the nodes list headed by c is empty and both counters are zero. */
	TAILQ_REMOVE(&g->components, c, list);
	free(c);
}

/* Merge two components, and remove the empty one. The surviving component is returned. */
static struct Component*
graph_merge_components(struct Graph *g, struct Component *c1, struct Component *c2)
{
	struct Node *n;

	assert(c1 != c2);

	/* 
	 * To do as little work as possible, let the largest (in terms of
	 * number of nodes) component survive, and update the nodes in the
	 * smaller to belong to the larger.
	 */
	if (c2->node_count > c1->node_count) {
		struct Component *tmp = c1;
		c1 = c2;
		c2 = tmp;
	}
	assert(c2->node_count <= c1->node_count);

	/* Tell the nodes of c2 their new owner */
	STAILQ_FOREACH(n, &c2->nodes, complink) {
		n->comp = c1;
	}

	/* Concatenate and update counters - this is the reason for STAILQ and not just SLIST. */
	STAILQ_CONCAT(&c1->nodes, &c2->nodes);
	c1->node_count += c2->node_count;
	c1->edge_count += c2->edge_count;
	c2->node_count = c2->edge_count = 0;
	/* Remove the now empty component */
	graph_remove_component(g, c2);
	return c1;
}

/* Add a node to a component, and make sure the node knows where it belongs. */
static void
component_add_node(struct Component *c, struct Node *n)
{
	STAILQ_INSERT_TAIL(&c->nodes, n, complink);
	c->node_count += 1;
	n->comp = c;
}

static void
node_add_out_edge(struct Node *node, struct Edge *e)
{
	struct Component *comp = node->comp;
	assert(e->tgt->comp == comp);

	SLIST_INSERT_HEAD(&node->out_edges, e, nodelink);
	node->out_degree++;
	e->tgt->in_degree++;
	comp->edge_count += 1;
}


static void
graph_attempt_hash_resize(struct Graph *g)
{
	size_t oldsize = g->hashmask + 1;
	size_t newsize = 2*oldsize;
	uint32_t newmask = newsize-1;
	uint32_t i;
	struct Node *n;

	struct NodeBucket *newbucket = malloc(newsize * sizeof(*newbucket));
	if (newbucket == NULL) {
		g->resize_threshold = UINT32_MAX;
		return;
	}
	for (i = 0; i < newsize; ++i)
		SLIST_INIT(&newbucket[i]);

	for (i = 0; i < oldsize; ++i) {
		while ((n = SLIST_FIRST(&g->nodes[i])) != NULL) {
			SLIST_REMOVE_HEAD(&g->nodes[i], hashlink);
			SLIST_INSERT_HEAD(&newbucket[n->hv & g->hashmask], n, hashlink);
		}
	}
	free(g->nodes);
	g->nodes = newbucket;
	g->hashmask = newmask;
	/* For a hash table of size 2^n, allow an average chain length of n. */
	newsize *= __builtin_ctz(newsize);
	g->resize_threshold = newsize > UINT32_MAX ? UINT32_MAX : newsize;
}

static struct Node*
graph_alloc_node(struct Graph *g, size_t len)
{
	struct Node *n = obstack_alloc(&g->node_os, sizeof(*n) + len + 1);
	if (!n)
		return NULL;
	if (++g->node_count > g->resize_threshold)
		graph_attempt_hash_resize(g);
	return n;
}

/*
 * If some allocation fails, we need to undo adding nodes. This means
 * removing it from the graph's hash table, and deallocating it from
 * the top of the obstack. This can only be done for the most recently
 * added node. The node must have been added to the graph's hash
 * table, but must not have been assigned to any component.
 */
static void
graph_remove_last_node(struct Graph *g, struct Node *n)
{
	assert(n->comp == NULL);
	/*
	 * Normally, n sits at the front of its hash bucket. But if
	 * we've added two nodes and then need to remove them again,
	 * and the second addition caused a rehash, the first will now
	 * be at the end of its hash bucket. Since this is a
	 * double-rare occurence (an allocation must have failed AND
	 * we must have triggered a rehash), we can certainly afford
	 * an O(n) operation. SLIST_REMOVE checks for n being at the
	 * front, and defers to SLIST_REMOVE_HEAD in the common case.
	 */
	SLIST_REMOVE(&g->nodes[g->hashmask & n->hv], n, Node, hashlink);
	obstack_free(&g->node_os, n);
	g->node_count--;
}

/* 
 * Internal function for adding a node. 
 * 
 * If the node already exists, it is simply returned. If not, it is
 * created and inserted into the graph's hash table. The create_comp
 * parameter determines whether a new (singleton) component is
 * created. If not, it is the caller's responsibility to add the
 * returned node to some component. (This is useful in
 * graph_add_edge(), when we are creating a new 2-node,1-edge
 * component, to avoid creating two singleton components and then
 * immediately merging them, or when we are adding an edge with one
 * endpoint in an existing component, so that we know that the new
 * node will belong to that component).
 */
static struct Node*
graph_add_node_internal(struct Graph *g, const char *nstr, int create_comp)
{
	struct Node *n;
	struct Component *c;
	size_t idlen;
	uint32_t hv;

	idlen = strlen(nstr);
#define HASH_INIT   0xC0FFEE
	hv = jenkins_hash(nstr, idlen, HASH_INIT);
	SLIST_FOREACH(n, &g->nodes[g->hashmask & hv], hashlink) {
		if (hv == n->hv && strcmp(nstr, n->ident) == 0)
			return n;
	}

	/* No node with that name exists. Create one. */
	n = graph_alloc_node(g, idlen);
	if (n == NULL)
		return NULL;

	n->comp = NULL;
	SLIST_INIT(&n->out_edges);
	n->out_degree = 0;
	n->in_degree = 0;
	n->hv = hv;
	memcpy(n->ident, nstr, idlen+1);
	SLIST_INSERT_HEAD(&g->nodes[g->hashmask & hv], n, hashlink);

	if (create_comp) {
		c = graph_new_component(g);
		if (c == NULL) {
			graph_remove_last_node(g, n);
			return NULL;
		}
		component_add_node(c, n);
	}

	return n;
}

/**
 * Public interfaces.
 */

/* Iterate over the components of the graph. */
int
graph_iterate_components(const struct Graph *g, int (*callback)(const struct Component *comp, void *ctx), void *ctx)
{
	const struct Component *comp;
	TAILQ_FOREACH(comp, &g->components, list) {
		int r = callback(comp, ctx);
		if (r)
			return r;
	}
	return 0;
}

/* Iterate over the nodes of the graph. */
int
graph_iterate_nodes(const struct Graph *g, int (*cb)(const struct Node *node, void *ctx), void *ctx)
{
	const struct Component *comp;
	TAILQ_FOREACH(comp, &g->components, list) {
		int r = component_iterate_nodes(comp, cb, ctx);
		if (r)
			return r;
	}
	return 0;
}

/* Iterate over the edges of the graph. The edges are supplied as a pair of source and target node. */
int
graph_iterate_edges(const struct Graph *g, int (*cb)(const struct Node *src, const struct Node *tgt, void *ctx), void *ctx)
{
	const struct Component *comp;
	TAILQ_FOREACH(comp, &g->components, list) {
		int r = component_iterate_edges(comp, cb, ctx);
		if (r)
			return r;
	}
	return 0;  
}

/* Iterate over the nodes of the component. */
int
component_iterate_nodes(const struct Component *comp, int (*cb)(const struct Node *node, void *ctx), void *ctx)
{
	const struct Node *node;
	STAILQ_FOREACH(node, &comp->nodes, complink) {
		int r = cb(node, ctx);
		if (r)
			return r;
	}
	return 0;
}
/* Iterate over the edges of the component. The edges are supplied as a pair of source and target node. */
int 
component_iterate_edges(const struct Component *comp, int (*cb)(const struct Node *src, const struct Node *tgt, void *ctx), void *ctx)
{
	const struct Node *src;
	const struct Edge *edge;
	STAILQ_FOREACH(src, &comp->nodes, complink) {
		SLIST_FOREACH(edge, &src->out_edges, nodelink) {
			int r = cb(src, edge->tgt, ctx);
			if (r)
				return r;
		}
	}
	return 0;
}


bool
graph_edge_exists(const struct Node *src, const struct Node *tgt)
{
	const struct Edge *e;
	SLIST_FOREACH(e, &src->out_edges, nodelink) {
		if (e->tgt == tgt)
			return true;
	}
	return false;
}



int
graph_init(struct Graph *g, unsigned flags)
{
	uint32_t buckets;
	uint32_t i;
	int bshift = 4;

	if (flags & ~(GRAPH_UNDIRECTED | GRAPH_NOPARALLEL | GRAPH_NOLOOP | GRAPH_DUAL)) {
		errno = EINVAL;
		return -1;
	}

	if ((flags & GRAPH_UNDIRECTED) && (flags & GRAPH_DUAL)) {
		errno = EINVAL;
		return -1;
	}

	TAILQ_INIT(&g->components);

	buckets = 1 << bshift;
	g->nodes = malloc(buckets * sizeof(*g->nodes));
	if (g->nodes == NULL) {
		return -1;
	}

	g->node_count = 0;
	g->resize_threshold = bshift * buckets;
	g->hashmask = buckets - 1;
	assert((buckets & g->hashmask) == 0);
	assert(g->hashmask >> bshift == 0);

	for (i = 0; i < buckets; ++i) {
		SLIST_INIT(&g->nodes[i]);
	}

	obstack_begin(&g->node_os, 1 << 11);
	obstack_begin(&g->edge_os, 1 << 11);
  
	g->flags = flags;

	return 0;
}
void
graph_destroy(struct Graph *g)
{
	struct Component *c, *c2;
	TAILQ_FOREACH_SAFE(c, &g->components, list, c2) {
		free(c);
	}
	free(g->nodes);
	obstack_free(&g->node_os, NULL);
	obstack_free(&g->edge_os, NULL);
	memset(g, 0, sizeof(*g));
}

int
graph_add_node(struct Graph *g, const char *nstr)
{
	struct Component *last = TAILQ_LAST(&g->components, ComponentHead);
	struct Node *node = graph_add_node_internal(g, nstr, 1);
	if (node == NULL)
		return -1;
	assert(node->comp != NULL);
	/*
	 * If a new component was created, it has been inserted at the
	 * end. The only way we can know if this has happened, without
	 * modifying _internal to return more information, is to cache
	 * the last component before the call and then compare it to
	 * the last component afterwards.
	 */
	return last != TAILQ_LAST(&g->components, ComponentHead);
}

/* Do add an edge from src to tgt, and return 1 on success, -1 on failure. */
static int
do_add_edge(struct Graph *g, struct Node *src, struct Node *tgt)
{
	/* 
	 * If we reach this point, we must add a new edge. It is directed
	 * from src to tgt. However, we can't call node_add_out_edge() before
	 * we know which component src and tgt belong to (since
	 * node_add_out_edge is going to increment n->comp->edge_count).
	 */
	struct Edge *e;

	e = obstack_alloc(&g->edge_os, sizeof(*e));
	if (!e)
		return -1;
	/* e->src = src; */
	e->tgt = tgt;

	/* 
	   We have a total of five cases: 
	   (a) neither src or tgt existed before
	   (b) tgt existed, but src did not
	   (c) src existed, but tgt did not
	   (d) both existed, and belonged to the same component
	   (e) both existed, and belonged to different components
	*/
  
	if (src->comp == NULL && tgt->comp == NULL) {
		/* (a) create a new component consisting of src, tgt and e. Since src
		   and tgt were created without knowing their components, remember
		   to update their ->comp fields (component_add_node() does this). */
		struct Component *c = graph_new_component(g);
		if (c == NULL) {
			obstack_free(&g->edge_os, e);
			return -1;
		}

		component_add_node(c, src);
		if (src != tgt)
			component_add_node(c, tgt);

		node_add_out_edge(src, e);
		return 1;
	}
  
	if (src->comp == NULL) {
		/* (b) src and the new edge to tgt is simply added to the component tgt belongs to */
		assert(tgt->comp != NULL);
		component_add_node(tgt->comp, src);
		node_add_out_edge(src, e);
		return 1;
	}
  
	if (tgt->comp == NULL) {
		/* (c) completely symmetrical to (b) */
		assert(src->comp != NULL);
		component_add_node(src->comp, tgt);
		node_add_out_edge(src, e);
		return 1;
	}

	assert(src->comp != NULL);
	assert(tgt->comp != NULL);
	if (src->comp == tgt->comp) {
		/* (d) the simple case, just add the edge to the list of outgoing edges */
		node_add_out_edge(src, e);
		return 1;
	}
  
	assert(src->comp != tgt->comp);
	/* (e) the complicated case: the two components need to be merged */
	graph_merge_components(g, src->comp, tgt->comp);

	/* insert the new edge in src's list of outgoing edges */
	node_add_out_edge(src, e);
	return 1;
 
}

int
graph_add_edge(struct Graph *g, const char *s1, const char *s2)
{
	struct Node *n1, *n2;
	int swapped = 0;

	n1 = graph_add_node_internal(g, s1, 0);
	if (n1 == NULL)
		return -1;
	n2 = graph_add_node_internal(g, s2, 0);
	if (n2 == NULL) {
		if (n1->comp == NULL)
			graph_remove_last_node(g, n1);
		return -1;
	}

	if ((g->flags & GRAPH_UNDIRECTED) && nodes_cmp(n1, n2) > 0) {
		/* Orient the edge canonically. */
		struct Node *tmp = n1;
		n1 = n2;
		n2 = tmp;
		swapped = 1;
	}

	if (n1 == n2 && (g->flags & GRAPH_NOLOOP)) {
		/* 
		 * We are not allowed to add loops. But if the given node has not
		 * been seen before, we still need to add a singleton
		 * component.
		 */
		if (n1->comp == NULL) {
			struct Component *c = graph_new_component(g);
			if (c == NULL) {
				graph_remove_last_node(g, n1);
				return -1;
			}

			component_add_node(c, n1);
		}
		return 0;
	}

	/* 
	 * If we must not add parallel edges, we need to check if an
	 * edge parallel to the new edge already exists. If so, there
	 * is nothing for us to do. This can obviously only happen if
	 * n1->comp and n2->comp are equal and non-null.
	 */
	if ((g->flags & GRAPH_NOPARALLEL) && 
	    (n1->comp != NULL) && (n1->comp == n2->comp)) {
		if (graph_edge_exists(n1, n2))
			return 0;
	}

	/* Now do add an edge from n1 to n2. */
	if (do_add_edge(g, n1, n2) < 0) {
		/*
		 * If either n1 or n2 were not known before the call
		 * of graph_add_edge, it needs to be removed now. If
		 * they were swapped, we need to swap back, since we
		 * need to remove them in proper order. Also, be
		 * careful if n1 and n2 are the same node.
		 */
		if (swapped) {
			struct Node *tmp = n1;
			n1 = n2;
			n2 = tmp;
		}
		if (n2->comp == NULL)
			graph_remove_last_node(g, n2);
		if (n1 != n2 && n1->comp == NULL)
			graph_remove_last_node(g, n1);
		return -1;
	}

	/* Check that do_add_edge actually created or merged components, if needed. */
	assert(n1->comp != NULL);
	assert(n1->comp == n2->comp);

	if ((g->flags & GRAPH_DUAL) && (n1 != n2)) {
		/*
		 * If GRAPH_DUAL is set, and we've previously added an
		 * edge between n1 and n2, we've also added an edge in
		 * the other direction. Hence if GRAPH_NOPARALLEL is
		 * also set, we wouldn't reach this point if there
		 * already is an edge from n2 to n1 (because then
		 * there would also be an edge from n1 to n2, which
		 * would have been caught above). So we shouldn't need
		 * the somewhat expensive graph_edge_exists(n2, n1)
		 * call here.
		 *
		 * The exception, of course, is the case where the
		 * previous add_edge(n2, n1) call failed halfway
		 * through, leaving the graph in a state inconsistent
		 * with its flags (see below). But that means that
		 * omitting the graph_edge_exists() check at worst
		 * trades one inconsistency (not satisfying
		 * GRAPH_DUAL) for another (potentially adding an edge
		 * parallel to an existing edge, despite
		 * GRAPH_NOPARALLEL). This is acceptable. Also note
		 * that this is idempotent: Once edges exist in both
		 * directions, we will not add any more edges between
		 * these two vertices, so at worst there will be one
		 * extra edge in one direction.
		 */
#if 0
		if ((g->flags & GRAPH_NOPARALLEL) && graph_edge_exists(n2, n1))
			return 1;
#endif
		/*
		 * At this point, we cannot undo adding the edge from
		 * n1 to n2 (among other things, it might have caused
		 * two components to be merged). So we can only tell
		 * the caller that an error occured. This will leave
		 * the graph in a state which is inconsistent with its
		 * flags, but we let the caller deal with that.
		 */
		if (do_add_edge(g, n2, n1) < 0)
			return -1;
		return 2; /* the number of edges added */
	}
	return 1;
}

int
graph_add_file(struct Graph *gph, FILE *fp)
{
	char *line  = NULL;
	size_t lcap = 0;
	ssize_t linelen;
	int rv = 0;

	while ((linelen = getline(&line, &lcap, fp)) > 0) {
		char *nstr1;
		char *nstr2;
		nstr1 = strtok_r(line, " \t\n", &nstr2);
		if (nstr1 == NULL) /* blank line */
			continue;
		nstr2 = strtok_r(NULL, " \t\n", &nstr2);
		if (nstr2 == NULL) {
			if (graph_add_node(gph, nstr1) < 0) {
				rv = -1;
				break;
			}
		}
		else {
			if (graph_add_edge(gph, nstr1, nstr2) < 0) {
				rv = -1;
				break;
			}
		}
	}
	if (ferror(fp))
		rv = -1;
	free(line);
	return rv;
}
