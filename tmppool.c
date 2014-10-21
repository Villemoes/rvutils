#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <sched.h>

#include "tmppool.h"


#ifndef container_of
#define container_of(ptr, type, member) ({                      \
      const typeof( ((type *)0)->member ) *__mptr = (ptr);      \
      (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar)                      \
        for ((var) = SLIST_FIRST((head));                               \
	     (var) && ((tvar) = SLIST_NEXT((var), field), 1);		\
	     (var) = (tvar))
#endif

#ifndef SLIST_SWAP
#define SLIST_SWAP(head1, head2, type) do {				\
		struct type *swap_first = SLIST_FIRST(head1);		\
		SLIST_FIRST(head1) = SLIST_FIRST(head2);		\
		SLIST_FIRST(head2) = swap_first;			\
	} while (0)
#endif

/*
 * The allocated and initialized tmp objects are kept in an array of
 * linked lists; each list is protected by its own mutex. When a tmp
 * object is requested, we look for an available object in all lists,
 * starting with the one corresponding to the current cpu number
 * (modulo the number of lists). When an object is found, or if a new
 * one needs to be malloc'ed and initialized, the cpu number is
 * recorded in the tmp obj header.
 *
 * When a tmp obj is returned to the pool, we use that cpu number to
 * decide the list to return it to.
 *
 * The theory is that, provided threads are not moved between cpus
 * (too much), a thread which frequently uses tmp objects is likely to
 * find one available in the first list it looks in, even if the
 * objects are put back by another thread (e.g., a producer-consumer
 * model). Also, multiple threads should be able to concurrently get
 * and put tmp objects, provided they don't run on cpus "hashing" to
 * the same index. Of course, there isn't really any such thing as
 * "the current cpu number", since we can be preempted at any time,
 * but the kernel only rarely moves threads betwen cpus, and the
 * critical sections are extremely short.
 *
 * Objects are stored in a singly-linked list. This has the least
 * memory overhead, and each get/put requires only a few pointer
 * operations. Also, LIFO should have the most cache/TLB-friendly
 * behaviour.
 */

/*
 * This is our wrapper for a temporary object. The actual object is
 * located immediately following the struct. Since the list
 * bookkeeping field is only needed when the tmp obj is free and the
 * cpu field is only needed when the obj is in use, we can use a
 * union. Thus the memory overhead for a tmp obj is only sizeof(void*)
 * (+ malloc overhead).
 */

struct tmppool_obj {
	union {
		SLIST_ENTRY(tmppool_obj) list;
		unsigned                 cpu;
	};
	long                 obj[];
};

static void *
tmppool_alloc(const struct tmppool *pool, unsigned cpu)
{
	struct tmppool_obj *t = malloc(sizeof(*t) + pool->obj_size);
	if (!t)
		return NULL;
	if (pool->init_obj && pool->init_obj(t->obj, pool->obj_size)) {
		free(t);
		return NULL;
	}
	t->cpu = cpu;
	return t->obj;
}

static void
tmppool_free(const struct tmppool *pool, struct tmppool_obj *t)
{
	if (pool->destroy_obj)
		pool->destroy_obj(t->obj, pool->obj_size);
	free(t);
}

static inline unsigned
get_current_cpu(void)
{
	return sched_getcpu();
	/* unsigned cpu; */
	/* asm ("rdtscp\n" : "=c"(cpu) : : "eax", "edx"); */
	/* return cpu; */
}


void *
tmppool_get(struct tmppool *pool)
{
	unsigned i, cpu;
	struct tmppool_obj *t;

	cpu = get_current_cpu();
	i = cpu & pool->mask;

	do {
		pthread_mutex_lock(&pool->list[i].mtx);
		t = SLIST_FIRST(&pool->list[i].head);
		if (t) {
			SLIST_REMOVE_HEAD(&pool->list[i].head, list);
			pthread_mutex_unlock(&pool->list[i].mtx);
			t->cpu = cpu;
			return t->obj;
		}
		pthread_mutex_unlock(&pool->list[i].mtx);
		++i;
		i &= pool->mask;
	} while (i != (cpu & pool->mask));

	return tmppool_alloc(pool, cpu);
}

void
tmppool_put(struct tmppool *pool, void *obj)
{
	unsigned idx;
	struct tmppool_obj *t;

	t = container_of(obj, struct tmppool_obj, obj);
	idx = t->cpu & pool->mask;

	pthread_mutex_lock(&pool->list[idx].mtx);
	SLIST_INSERT_HEAD(&pool->list[idx].head, t, list);
	pthread_mutex_unlock(&pool->list[idx].mtx);
}

void
tmppool_release(struct tmppool *pool)
{
	unsigned idx;
	struct tmppool_obj *t, *t2;
	SLIST_HEAD(, tmppool_obj) head;

	for (idx = 0; idx <= pool->mask; ++idx) {
		SLIST_INIT(&head);

		pthread_mutex_lock(&pool->list[idx].mtx);
		SLIST_SWAP(&head, &pool->list[idx].head, tmppool_obj);
		pthread_mutex_unlock(&pool->list[idx].mtx);

		SLIST_FOREACH_SAFE(t, &head, list, t2) {
			tmppool_free(pool, t);
		}
	}
}
