#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/queue.h>
#include <sys/types.h>
#include "tailq_sort.h"

/* 
 * Straight-forward implementation of mergesort as described at
 * http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html,
 * applied to the generic TAILQ from sys/queue.h (or bsd/sys/queue.h).
 */


void
tailq_sort(void *__head, size_t offset, 
	   int (*cmp_func)(const void *a, const void *b, void *ctx),
	   void *ctx)
{
	/*
	 * We define a custom type with the same initial layout as the
	 * user's struct. This may be one of the few legitimate uses
	 * of the gcc extension allowing VLAs in struct definitions.
	 */
	struct type {
		char              pad[offset];
		TAILQ_ENTRY(type) list;
	};
	TAILQ_HEAD(headtype, type);

	struct headtype *head = __head;
	struct type *x, *y;
	size_t pwr2, merges, xsize, ysize;

	assert(offsetof(struct type, list) == offset);

	if (!TAILQ_FIRST(head))
		return;

	pwr2 = 1;
	while (1) {
		x = TAILQ_FIRST(head);
		merges = 0;

		while (x) {
			/* [1] */
			merges++;
			y = x;
			for (xsize = 0; y && xsize < pwr2; ++xsize) {
				y = TAILQ_NEXT(y, list);
			}
			/* [2] */
			ysize = pwr2;
			while (xsize > 0 || (ysize > 0 && y)) {
				/* [3] */
				if (ysize == 0 || !y) {
					/* [4] */
					break;
				}
				if (xsize == 0) {
					/* [5] */
					do {
						y = TAILQ_NEXT(y, list);
						ysize--;
					} while (ysize > 0 && y);
					break;
				}
				/* [6] */
				if (cmp_func(x, y, ctx) <= 0) {
					/* [7] */
					xsize--;
					x = TAILQ_NEXT(x, list);
				}
				else {
					/* [8] */
					struct type *tmp = TAILQ_NEXT(y, list);
					TAILQ_REMOVE(head, y, list);
					TAILQ_INSERT_BEFORE(x, y, list);
					ysize--;
					y = tmp;
				}
			}
			x = y;
		}
		if (merges <= 1)
			break;
		pwr2 *= 2;
	}
}
/*
 * Notes:
 *
 * [1] At this point, x points to L[k*2*pwr2] for some k. The
 *     preceding k sublists of length 2*pwr2 are individually
 *     sorted. The sublist L[k], L[k+1], ..., L[k+pwr2-1] is sorted
 *     (up to how many actually exist), as is the sublist L[k+pwr2],
 *     ..., L[k+2*pwr2-1].
 *
 * [2] We don't know that y has pwr2 elements after it, but we must
 *     not step y more than pwr2 along.
 * 
 * [3] The elements from L[k*2*pwr2] up to but excluding x are
 *     sorted. They are all smaller than both x and y. The sublist
 *     starting at x of length xsize is sorted. The sublist starting
 *     at y of length ysize (or till the end of the list) is
 *     sorted.
 *
 * [4] y has reached the end of its sublist, so the rest of the merge
 *     consists only of elements from the x sublist, which is already
 *     sorted. So we shortcircuit.
 * 
 * [5] x has reached the end of its sublist (so now we should actually
 *     have x == y). We need to step y to the end of its sublist, then
 *     break. Note that here we must have ysize > 0 and y != NULL, so
 *     "do {} while" is slightly better than "while {}".
 *
 * [6] Both the x and y sublists are non-empty. We need to figure out
 *     what the next element is.
 *
 * [7] Easy. x precedes y, so is already in the right position.
 *
 * [8] Hard. We need to remove y from the list and insert it before x,
 *     but we also need to make y point to its current successor.
 */




/* 
 * If we have a source which produces truly random bits, this is one
 * way to use it to generate numbers uniformly at random in a given
 * range: Pick out just enough of the low-order bits as needed, and
 * check if the resulting number is acceptable. If not, rinse,
 * repeat.
 */
static unsigned
default_rand(unsigned max, void *ctx __attribute__((__unused__)))
{
	unsigned mask;
	unsigned r;

	mask = max;
	mask |= mask >> 1;
	mask |= mask >> 2;
	mask |= mask >> 4;
	mask |= mask >> 8;
	mask |= mask >> 16;

	/* 
	 * Mask is now one less than the smallest power of 2 greater
	 * than max. (This is also true in corner cases, e.g. when max
	 * is 0, or a power of 2, or greater than 2^31).
	 * 
	 * Note that mrand48 returns a long int, containing a number
	 * uniformly from [-2^31, 2^31-1]. Casting this to an unsigned
	 * produces a number uniformly in [0, 2^32-1] (assuming
	 * sizeof(unsigned) == 4 and 2s complement). The bitwise and
	 * with mask then produces a number uniformly in [0,
	 * 2^whatever-1]. On average, the loop should repeat less than
	 * two times.
	 */
	do {
		r = mrand48();
		r &= mask;
	} while (r > max);
	
	return r;
}

void
tailq_shuffle(void *__head, size_t offset,
	      unsigned (*rand_func)(unsigned, void*), void *ctx)
{
	struct type {
		char              pad[offset];
		TAILQ_ENTRY(type) list;
	};
	TAILQ_HEAD(headtype, type);

	struct headtype *head = __head;
	struct headtype tmp[2];
	size_t len[2];
	size_t count = 0;
	struct type *e;

	if (rand_func == NULL)
		rand_func = default_rand;

	/* Lists of size 0 or 1 are already perfectly shuffled. */
	if (TAILQ_EMPTY(head) || TAILQ_FIRST(head) == TAILQ_LAST(head, headtype))
		return;
	/* Also special-case length 2. */
	if (TAILQ_NEXT(TAILQ_FIRST(head), list) == TAILQ_LAST(head, headtype)) {
		/* The % 2 should be unnecessary, but also shouldn't hurt. */
		if (rand_func(1, ctx) % 2) {
			e = TAILQ_FIRST(head);
			TAILQ_REMOVE(head, e, list);
			TAILQ_INSERT_TAIL(head, e, list);
		}
		return;
	}

	TAILQ_INIT(&tmp[0]);
	TAILQ_INIT(&tmp[1]);

	while ((e = TAILQ_FIRST(head))) {
		TAILQ_REMOVE(head, e, list);
		TAILQ_INSERT_TAIL(&tmp[count % 2], e, list);
		count++;
	}
	len[1] = count/2;
	len[0] = count - len[1];

	tailq_shuffle(&tmp[0], offset, rand_func, ctx);
	tailq_shuffle(&tmp[1], offset, rand_func, ctx);

	while (len[0] > 0 && len[1] > 0) {
		if (rand_func(len[0] + len[1] - 1, ctx) < len[0]) {
			e = TAILQ_FIRST(&tmp[0]);
			TAILQ_REMOVE(&tmp[0], e, list);
			len[0]--;
		}
		else {
			e = TAILQ_FIRST(&tmp[1]);
			TAILQ_REMOVE(&tmp[1], e, list);
			len[1]--;
		}
		TAILQ_INSERT_TAIL(head, e, list);
	}
	/* One of the lists is empty, take the rest of the other and append it to head */
	TAILQ_CONCAT(head, &tmp[0], list);
	TAILQ_CONCAT(head, &tmp[1], list);
}
