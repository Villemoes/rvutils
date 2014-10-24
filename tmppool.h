#ifndef TMPPOOL_H
#define TMPPOOL_H

#include <sys/queue.h>
#include <pthread.h>
#include "ass.h"

struct tmppool_obj;

struct tmppool_list {
	pthread_mutex_t            mtx;
	SLIST_HEAD(, tmppool_obj)  head;
};
#define TMPPOOL_LIST_INITIALIZER {.mtx = PTHREAD_MUTEX_INITIALIZER, .head = SLIST_HEAD_INITIALIZER() }

struct tmppool {
	int               (*init_obj)(void *obj, unsigned size);
	void              (*destroy_obj)(void *obj, unsigned size);
	unsigned            mask;
	unsigned            obj_size;
	struct tmppool_list list[];
};

/*
 * I'd like to avoid using the gcc extension allowing static
 * initialization of a flexible array members, but I don't know how to
 * achieve this any other way. I really don't want to force the user
 * to allocate and initialize the tmppool at runtime (this is library
 * code meant for other libraries to build on).
 *
 */

#define TMPPOOL_DECLARE(name, N, init, destroy, objsize)		\
	struct tmppool name = {						\
		.init_obj = (init),					\
		.destroy_obj = (destroy),				\
		.mask = ((N)-1) +					\
		static_assert_zero(((N)&((N)-1)) == 0,			\
				N_must_be_power_of_2),			\
		.obj_size = (objsize),					\
		.list = {						\
			[0 ... ((N)-1)] = TMPPOOL_LIST_INITIALIZER,	\
		},							\
	}


void *tmppool_get(struct tmppool *pool);
void tmppool_put(struct tmppool *pool, void *obj);
void tmppool_release(struct tmppool *pool);


#endif /* TMPPOOL_H */
