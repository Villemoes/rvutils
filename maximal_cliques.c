#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <error.h>
#include <errno.h>

#include <getopt.h>

#include <valgrind/valgrind.h>

#include "graph.h"
#include "clique.h"

struct optionvalues {
	long      hashshift;
	bool      exclude_singletons;    
};

struct optionvalues opt_val = {
	.exclude_singletons = false,
};

static void
usage(FILE *fp)
{
	fputs("maximal_cliques [-x]\n"
	      "maximal_cliques -h\n",
	      fp);
}

static void
help(FILE *fp)
{
	usage(fp);
	fputs("\n"
	      "Reads a description of a graph from STDIN, and prints all maximal cliques to STDOUT.\n"
	      "\n"
	      "Each line of input should consist of one or two whitespace separated fields.\n"
	      "Each string identifies a node in the graph; a new node is created whenever\n"
	      "a new string is encountered. If a line contains two fields, that defines an edge\n"
	      "which is added to the graph.\n"
	      "\n"
	      "-x               Do not report singleton cliques (aka isolated nodes)\n"
	      "-h,--help        print help and exit\n",
	      fp);
}


static void
parse_options(int argc, char *argv[])
{
	while (1) {
		static struct option Options[] = {
			{"help",       no_argument, 0, 'h'},
			{"exclude-singletons", no_argument, 0, 'x'},
			{0, 0, 0, 0},
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "xh", Options, &option_index);
		if (c == -1)
			break;
		switch(c) {
		case 'h':
			help(stdout);
			exit(0);
			break;
		case 'x':
			opt_val.exclude_singletons = true;
			break;
		case '?':
			usage(stderr);
			exit(1);
			break;
		default: /* should never happen */
			assert(0);
		}
	}
}

struct context {
	unsigned long index;
};


static int
print_clique_cb(const struct Node **nodes, size_t count, void *ctx)
{
	struct context *context = ctx;
	size_t i;

	if (count == 0) /* Shouldn't happen, but just in case. */
		return 0;
	if (opt_val.exclude_singletons && count == 1)
		return 0;

	context->index++;
	for (i = 0; i < count; ++i) {
		fprintf(stdout, "%lu\t%s\n", context->index, nodes[i]->ident);
	}
	return 0;
}

int main(int argc, char *argv[]) {
	struct Graph gph;
	unsigned flags = GRAPH_NOLOOP | GRAPH_NOPARALLEL | GRAPH_DUAL;
	struct context ctx = { .index = 0 };

	parse_options(argc, argv);

	if (graph_init(&gph, flags))
		error(2, errno, "initialization failed");

	if (graph_add_file(&gph, stdin))
		error(2, errno, "reading graph failed");

	graph_iterate_maximal_cliques(&gph, print_clique_cb, &ctx);

	if (RUNNING_ON_VALGRIND)
		graph_destroy(&gph);

	return 0;
}
