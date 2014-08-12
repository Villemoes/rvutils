#ifndef __TAILQ_SORT_H__
#define __TAILQ_SORT_H__

/* size_t and offsetof */
#include <stddef.h>

/**
 * tailq_sort - perform stable sort of a TAILQ
 *
 * @head: The TAILQ_HEAD of the list.
 * @offset: The offset within the structure of the TAILQ_ENTRY bookkeeping member.
 * @cmp_func: Pointer to the comparison function.
 * @ctx: Passed through to @cmp_func.
 *
 * Typical use:
 *
 * struct s { ... TAILQ_ENTRY(s) list; ... };
 * TAILQ_HEAD(shead, s) myhead;
 *
 * tailq_sort(&myhead, offsetof(struct s, list), &compare_s, ctx);
 *
 * or, if you prefer macro magic,
 *
 * TAILQ_SORT(&myhead, s, list, &compare_s, ctx);
 */

void tailq_sort(void *head, size_t offset, 
		int (*cmp_func)(const void *a, const void *b, void *ctx),
		void *ctx);
#define TAILQ_SORT(head, type, member, cmp, ctx)	\
	tailq_sort(head, offsetof(struct type, member), cmp, ctx)

/**
 * tailq_shuffle - perform random shuffle of a TAILQ
 *
 * @head: The TAILQ_HEAD of the list.
 * @offset: The offset within the structure of the TAILQ_ENTRY bookkeeping member.
 * @rand_func: Pointer to function providing random numbers.
 * @ctx: Passed through to @rand_func.
 *
 * rand_func(x, ctx) should return a random integer in the interval
 * [0, x]. If the numbers are uniformly distributed in this interval,
 * the shuffle will be a perfect shuffle (all N! orderings equally
 * likely). rand_func may be NULL, in which case a default function is
 * used.
 *
 * Typical use:
 *
 * struct s { ... TAILQ_ENTRY(s) list; ... };
 * TAILQ_HEAD(shead, s) myhead;
 *
 * tailq_shuffle(&myhead, offsetof(struct s, list), NULL, NULL);
 */
void tailq_shuffle(void *head, size_t offset, 
		   unsigned (*rand_func)(unsigned max, void *ctx),
		   void *ctx);
#define TAILQ_SHUFFLE(head, type, member, rand, ctx)	\
	tailq_shuffle(head, offsetof(struct type, member), rand, ctx)



#endif /* __TAILQ_SORT_H__ */
