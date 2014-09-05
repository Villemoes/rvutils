#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#undef NDEBUG
#include <assert.h>
#include <sys/queue.h>

#include "graph.h"
#include "clique.h"

struct NodeSet {
	SLIST_ENTRY(NodeSet) list;
	uint32_t len;
	uint32_t cap;
	bool     sorted;
	const struct Node **nodes;
};

struct nodesetpool {
	SLIST_HEAD(, NodeSet) sets;
	uint32_t count;
};

struct cliqctx {
	int (*user_cb)(const struct Node **, size_t, void *);
	void *user_ctx;
	struct NodeSet     *R;
	struct nodesetpool pool;
};


static struct NodeSet *
nodeset_alloc(uint32_t init)
{
	struct NodeSet *ns = malloc(sizeof(*ns));
	if (!ns)
		return NULL;
	if (init < 7)
		init = 7;
	ns->nodes = malloc(init*sizeof(*ns->nodes));
	if (!ns->nodes) {
		free(ns);
		return NULL;
	}
	ns->len = 0;
	ns->cap = init;
	ns->sorted = true;
	return ns;
}

static void
nodeset_free(struct NodeSet *ns)
{
	if (!ns)
		return;
	free(ns->nodes);
	free(ns);
}

static inline int
cmp_nodeptr(const void *a, const void *b)
{
	uintptr_t x = (uintptr_t) (*(const struct Node **) a);
	uintptr_t y = (uintptr_t) (*(const struct Node **) b);
	if (x < y)
		return -1;
	if (x > y)
		return 1;
	return 0;
}
static void
nodeset_sort(struct NodeSet *ns)
{
	if (ns->sorted)
		return;
	if (ns->len > 1)
		qsort(ns->nodes, ns->len, sizeof(*ns->nodes), &cmp_nodeptr);
	ns->sorted = true;
}

static void
nodeset_clear(struct NodeSet *ns)
{
	ns->len = 0;
	ns->sorted = true;
}

static bool
nodeset_contains(const struct NodeSet *ns, const struct Node *node)
{
	if (ns->sorted && ns->len >= 7) {
		uint32_t lower = 0, upper = ns->len, idx;
		int cmp;
		while (lower < upper) {
			idx = (lower+upper)/2;
			cmp = cmp_nodeptr(&ns->nodes[idx], &node);
			if (cmp > 0)
				upper = idx;
			else if (cmp < 0)
				lower = idx+1;
			else
				return true;
		}
		return false;
	}
	for (uint32_t i = 0; i < ns->len; ++i) {
		if (ns->nodes[i] == node)
			return true;
	}
	return false;  
}

static int
nodeset_increase_capacity(struct NodeSet *ns)
{
	uint32_t newcap = ns->cap + ns->cap/2 + 1;
	const struct Node **new = realloc(ns->nodes, newcap*sizeof(*new));
	if (!new)
		return -1;
	ns->nodes = new;
	ns->cap = newcap;
	return 0;
}

static int
nodeset_append(struct NodeSet *ns, const struct Node *node)
{
	if (ns->len == ns->cap && nodeset_increase_capacity(ns))
		return -1;
	/* 
	 * The new array is properly sorted if it was either empty, or
	 * already sorted and the node-to-be-appended compares greater than
	 * the last element.
	 */
	ns->sorted = (ns->len == 0) || (ns->sorted && cmp_nodeptr(&ns->nodes[ns->len-1], &node) < 0);
	ns->nodes[ns->len++] = node;
	return 0;
}
static int
nodeset_insert_sorted(struct NodeSet *ns, const struct Node *node)
{
	if (ns->len == ns->cap && nodeset_increase_capacity(ns))
		return -1;
	/* If the set is empty, or is not sorted, just put it at the end and then sort the whole thing. */
	if (!ns->sorted || ns->len == 0) {
		ns->nodes[ns->len++] = node;
		nodeset_sort(ns);
		return 0;
	}
	/* 
	 * Here ns->len > 0. If the set is already sorted and the new
	 * element actually belongs after the last element, we don't need to
	 * do a binary search.
	 */
	if (ns->sorted && cmp_nodeptr(&ns->nodes[ns->len-1], &node) < 0) {
		ns->nodes[ns->len++] = node;
		return 0;
	}

	uint32_t lower = 0, upper = ns->len, idx;
	int cmp;
	while (lower < upper) {
		idx = (lower+upper)/2;
		cmp = cmp_nodeptr(&ns->nodes[idx], &node);
		if (cmp > 0)
			upper = idx;
		else if (cmp < 0)
			lower = idx + 1;
		else /* Shouldn't happen! Maybe just print a warning and return 0. */
			abort();
	}
	/* 
	 * If cmp < 0 at this point, this means that node should be inserted
	 * after ns->nodes[idx]. In that case, we need to move all elements
	 * from idx+1 and upwards one element to the right. If cmp > 0, this
	 * means that node should be inserted before ns->nodes[idx] (that
	 * is, at index idx), in which case all elements from idx and up
	 * need to be moved.
	 */
	if (cmp < 0)
		idx++;
	memmove(ns->nodes+idx+1, ns->nodes+idx, (ns->len-idx)*sizeof(*ns->nodes));
	ns->nodes[idx] = node;
	ns->len++;
	return 0;
}
static void
nodeset_intersect(struct NodeSet *A, const struct NodeSet *B)
{
	uint32_t target = 0;
	for (uint32_t source = 0; source < A->len; ++source) {
		if (nodeset_contains(B, A->nodes[source]))
			A->nodes[target++] = A->nodes[source];
	}
	A->len = target;
}


static void
nsp_init(struct nodesetpool *pool)
{
	pool->count = 0;
	SLIST_INIT(&pool->sets);
}
static void
nsp_destroy(struct nodesetpool *pool)
{
	struct NodeSet *ns;

	while ((ns = SLIST_FIRST(&pool->sets)) != NULL) {
		SLIST_REMOVE_HEAD(&pool->sets, list);
		nodeset_free(ns);
	}
	pool->count = 0;
}
static struct NodeSet*
nsp_get(struct nodesetpool *pool, uint32_t cap)
{
	struct NodeSet *ns;

	ns = SLIST_FIRST(&pool->sets);
	if (ns) {
		SLIST_REMOVE_HEAD(&pool->sets, list);
		pool->count--;
		return ns;
	}
	return nodeset_alloc(cap);
}

static void
nsp_put(struct nodesetpool *pool, struct NodeSet *ns)
{
	if (!ns)
		return;
	SLIST_INSERT_HEAD(&pool->sets, ns, list);
	pool->count++;
}


/*
 * Wikipedia shows these two versions in pseudo-code:
 
 BronKerbosch1(R,P,X):
 if P and X are both empty:
 report R as a maximal clique
 for each vertex v in P:
 BronKerbosch1(R ⋃ {v}, P ⋂ N(v), X ⋂ N(v))
 P := P \ {v}
 X := X ⋃ {v}

 BronKerbosch2(R,P,X):
 if P and X are both empty:
 report R as a maximal clique
 choose a pivot vertex u in P ⋃ X
 for each vertex v in P \ N(u):
 BronKerbosch2(R ⋃ {v}, P ⋂ N(v), X ⋂ N(v))
 P := P \ {v}
 X := X ⋃ {v}

 * The version with pivoting is much faster than the one
 * without. Wikipedia's "case of graphs with many non-maximal cliques"
 * can for example mean "any graph with a large clique". If there is a
 * clique with 30 nodes, each of the >10^9 subsets are (non-maximal)
 * cliques... Clearly, the first version can't handle stuff such as the
 * complete graph on 50 nodes.
 *
 * Since this is C, we have to handle the memory management and set
 * manipulation ourselves. First note that the set R can be shared
 * between all the recursive calls to BK - it is simply a stack. For
 * simplicity, we choose to represent a set of nodes using a standard
 * managed array (keep track of the current length and capacity). Since
 * we will need to do some set manipulations, we add the option to sort
 * the array canonically (we simpy sort the array by pointer value),
 * facilitating fast lookup. Each recursive call will need to pass a
 * new copy of P and X. Instead of allocating and freeing in each step,
 * we get and put from a pool of nodesets.
 *
 * We will ensure that P and X are sorted on every call to BK. This
 * makes the set manipulations faster. Appending to X in the loop is
 * easy. Removing from P is a lot harder. In the non-pivoting version,
 * we can simply solve this by cheating: When v = P[i] and we need to
 * form P ⋂ N(v), we just iterate from j=i+1. However, this implicit
 * removal breaks down in the pivoting case: If we just do a naive "if
 * (v and the pivot are neighbours) continue;", we would implicitly
 * have removed v from P on subsequent iterations, even though the
 * algorithm would never have reached the "P := P \ {v}" line. We
 * repair this by adding v to a list of exceptions, which we remember
 * to include in newP.
 *
 * Compiling with USE_PIVOT = 0 or 1 should produce identical results.
 */

#define USE_PIVOT 1
static int
BronKerbosch(struct cliqctx *cliqctx, struct NodeSet *P, struct NodeSet *X)
{
	struct NodeSet *newP = NULL, *newX = NULL;
	const struct Node *v;
#if USE_PIVOT
	const struct Node *pivot;
	struct NodeSet *still_in_P = NULL;
#endif
	struct Edge *edge;
	int ret = -1;

	if (P->len == 0) {
		if (X->len == 0)
			return (*cliqctx->user_cb)(cliqctx->R->nodes, cliqctx->R->len, cliqctx->user_ctx);
		return 0;
	}

	/* 
	 * The R we pass into the recursive call will have cardinality
	 * precisely |R|+1. The other two are bounded by |P| and |X|, but
	 * probably in practice a lot smaller.
	 */
	if (cliqctx->R->len == cliqctx->R->cap && nodeset_increase_capacity(cliqctx->R))
		return -1;

	newP = nsp_get(&cliqctx->pool, 0);
	newX = nsp_get(&cliqctx->pool, 0);
	if (newP == NULL || newX == NULL)
		goto out;
  
#if USE_PIVOT
	/* Choose the node among P \union X with the largest number of neighbours. */
	pivot = P->nodes[0];
	for (uint32_t i = 1; i < P->len; ++i) {
		if (P->nodes[i]->out_degree > pivot->out_degree)
			pivot = P->nodes[i];
	}
	for (uint32_t i = 0; i < X->len; ++i) {
		if (X->nodes[i]->out_degree > pivot->out_degree)
			pivot = X->nodes[i];
	}
	/* We will add at most all elements of P, and at most all neighbours of pivot, to still_in_P. */
	still_in_P = nsp_get(&cliqctx->pool, pivot->out_degree < P->len ? pivot->out_degree : P->len);
	if (still_in_P == NULL)
		goto out;
	nodeset_clear(still_in_P);
#endif  

	for (uint32_t i = 0; i < P->len; ++i) {
		v = P->nodes[i];
#if USE_PIVOT
		if (graph_edge_exists(pivot, v)) {
			/* Remember that v actually belongs to P. */
			if (nodeset_insert_sorted(still_in_P, v)) {
				ret = -1;
				goto out;
			}
			continue;
		}
#endif

		/* 
		 * Prepare R, newP and newX. There must be room in R. When
		 * creating newP = P\intersect N(v), we should think of P as
		 * consisting of the nodes from index i and onwards together with
		 * still_in_P.
		 */
		assert(nodeset_append(cliqctx->R, v) == 0);
		nodeset_clear(newP);
		nodeset_clear(newX);
		/* We use newX temporarily to contain N(v) */
		SLIST_FOREACH(edge, &v->out_edges, nodelink) {
			if (nodeset_insert_sorted(newX, edge->tgt)) {
				ret = -1;
				goto out;
			}
		}

		/* 
		 * Create P\intersect N(v) by first iterating over still_in_P,
		 * then iterating over P from j=i+1, and for each one asking if
		 * the node is an element of N(v).
		 *
		 * Note that still_in_P is a subsequence of P, so doing it this
		 * way should ensure that we create newP in a sorted manner, so
		 * that nodeset_insert_sorted() calls are O(1).
		 */
#if USE_PIVOT
		for (uint32_t j = 0; j < still_in_P->len; ++j) {
			if (nodeset_contains(newX, still_in_P->nodes[j])) {
				if (nodeset_insert_sorted(newP, still_in_P->nodes[j])) {
					ret = -1;
					goto out;
				}
			}
		}
#endif
		for (uint32_t j = i+1; j < P->len; ++j) {
			if (nodeset_contains(newX, P->nodes[j])) {
				if (nodeset_insert_sorted(newP, P->nodes[j])) {
					ret = -1;
					goto out;
				}
			}
		}
		/* Now restrict newX to its intersection with X. */
		nodeset_intersect(newX, X);

		/* Now newP := P \intersect N(v) and newX := X \intersect N(v). */
		ret = BronKerbosch(cliqctx, newP, newX);
		if (ret)
			goto out;

		/* Remove v from R again. */
		cliqctx->R->len--;

		/* Removing v from P is implicit. */

		/* 
		 * Add v to X. If P is sorted, we will be adding elements to X in
		 * order, so this should actually be O(1).
		 */
		if (nodeset_insert_sorted(X, v)) {
			ret = -1;
			goto out;
		}
	}
	ret = 0;

out:
	/* 
	 * Put in the opposite order of get, so that we use the pool as a
	 * stack. On subsequent recursive calls, we will then get a newP
	 * which also previously played the role of newP (similarly for newX
	 * and still_in_P), which should greatly increase the chance that
	 * they already have appropriate capacity.
	 */
#if USE_PIVOT
	nsp_put(&cliqctx->pool, still_in_P);
#endif
	nsp_put(&cliqctx->pool, newX);
	nsp_put(&cliqctx->pool, newP);

	return ret;
}
#undef USE_PIVOT


extern int
component_iterate_maximal_cliques(const struct Component *comp, int (*callback)(const struct Node **set, size_t count, void *ctx), void *ctx)
{
	struct cliqctx cliqctx;
	struct NodeSet *P, *X;
	const struct Node *node;
	int ret = -1;

	cliqctx.user_cb = callback;
	cliqctx.user_ctx = ctx;

	nsp_init(&cliqctx.pool);
	cliqctx.R = nodeset_alloc(0);
	P = nodeset_alloc(comp->node_count);
	X = nodeset_alloc(comp->node_count);
  
	if (cliqctx.R == NULL || P == NULL || X == NULL)
		goto out;

	STAILQ_FOREACH(node, &comp->nodes, complink) {
		assert(nodeset_append(P, node) == 0);
	}
	nodeset_sort(P);
	assert(P->len == comp->node_count);
	assert(X->len == 0);
	assert(cliqctx.R->len == 0);

	ret = BronKerbosch(&cliqctx, P, X);
  
out:
	nodeset_free(cliqctx.R);
	nodeset_free(P);
	nodeset_free(X);
	nsp_destroy(&cliqctx.pool);
	return ret;
}

extern int
graph_iterate_maximal_cliques(const struct Graph *gra, int (*callback)(const struct Node **nodes, size_t count, void *ctx), void *ctx)
{
	const struct Component *comp;
	unsigned required_flags = GRAPH_NOPARALLEL | GRAPH_NOLOOP | GRAPH_DUAL;
	if ((gra->flags & required_flags) != required_flags) {
		errno = EINVAL;
		return -1;
	}

	TAILQ_FOREACH(comp, &gra->components, list) {
		int r = component_iterate_maximal_cliques(comp, callback, ctx);
		if (r)
			return r;
	}
	return 0;
}
