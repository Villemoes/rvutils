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


/*
  options

  -s: print summary
  -n: print nodes
  -e: print edges

  -u: consider the graph undirected (actually directs all edges 'lexicographically')
  -p: disallow parallel edges (affects performance, sort -u is your friend)
  -l: ignore loops

*/

struct optionvalues {
	const char    *sumfile;
	const char    *nodefile;
	const char    *edgefile;
	int           summary;
	int           nodes;
	int           edges;
	unsigned      graphflags;
};

struct optionvalues opt_val = {
	.sumfile    = NULL,
	.nodefile   = NULL,
	.edgefile   = NULL,
	.summary    = 0,
	.nodes      = 0,
	.edges      = 0,
	.graphflags = 0,
};

struct context {
	FILE *dest;
	unsigned long cidx;
};

static int print_component_data(const struct Component *comp, void *ctx)
{
	struct context *sctx = ctx;
	sctx->cidx++;
	fprintf(sctx->dest, "%lu\t%u\t%u\n", sctx->cidx, comp->node_count, comp->edge_count);
	return 0;
}
static int print_node_data(const struct Node *node, void *ctx)
{
	struct context *nctx = ctx;
	fprintf(nctx->dest, "%lu\t%s\t%u\t%u\n", nctx->cidx, node->ident, node->in_degree, node->out_degree);
	return 0;
}
static int print_nodes_per_component(const struct Component *comp, void *ctx)
{
	struct context *nctx = ctx;
	nctx->cidx++;
	component_iterate_nodes(comp, &print_node_data, ctx);
	return 0;
}
static int print_edge_data(const struct Node *src, const struct Node *tgt, void *ctx)
{
	struct context *ectx = ctx;
	fprintf(ectx->dest, "%lu\t%s\t%s\n", ectx->cidx, src->ident, tgt->ident);
	return 0;
}
static int print_edges_per_component(const struct Component *comp, void *ctx)
{
	struct context *ectx = ctx;
	ectx->cidx++;
	component_iterate_edges(comp, &print_edge_data, ctx);
	return 0;
}



static void __attribute__((__noreturn__))
help_exit(int status)
{
	FILE *fp = status ? stderr : stdout;
	fprintf(fp, 
		"graphcomponents [-s[file]] [-n[file]] [-e[file]] [-u] [-p] [-l]\n"
		"graphcomponents -h\n"
		"\n"
		"Reads a description of a graph from STDIN, computes its components, and prints\n"
		"some information on those.\n"
		"\n"
		"Each line of input should consist of one or two whitespace separated fields.\n"
		"Each string identifies a node in the graph; a new node is created whenever\n"
		"a new string is encountered. If a line contains two fields, that defines an edge\n"
		"which is added to the graph.\n"
		"\n"
		"What information to print, and where, is controlled by the given options:\n"
		"\n"
		"-s,--summary   print a summary of the components (number of nodes and edges)\n"
		"               to file, or stdout if no filename is given\n"
		"-n,--nodes     print the nodes of the graph by component\n"
		"               to file, or stdout if no filename is given\n"
		"-e,--edges     print the edges of the graph by component\n"
		"               to file, or stdout if no filename is given\n"
		"\n"
		"Please note: When using the short option, no space is allowed before\n"
		"the filename. When using the long option, an equal sign is required before\n"
		"the filename (with no space on either side). Thus\n"
		"\n"
		"             graphcomponents -nnodefile.txt\n"
		"    or\n"
		"             graphcomponents --nodes=nodefile.txt\n"
		"\n"
		"If none of -s,-n,-e are given, -s is assumed.\n"
		"\n"
		"-u,--undirected  consider the graph undirected (actually simply directs\n"
		"                 each edge in some internal canonical order)\n"
		"-p,--noparallel  disallow parallel edges (affects performance, sort -u is\n"
		"                 your friend)\n"
		"-l,--noloop      disallow (ignore) loops (edges connecting a node to itself)\n"
		"\n"
		"-h,--help        print help and exit\n"

		);
	exit(status);
}

static void
parse_options(int argc, char *argv[])
{
	while (1) {
		static struct option Options[] = {
			{"summary",    optional_argument, 0, 's'},
			{"nodes",      optional_argument, 0, 'n'},
			{"edges",      optional_argument, 0, 'e'},
			{"undirected", no_argument, 0, 'u'},
			{"noparallel", no_argument, 0, 'p'},
			{"noloop",     no_argument, 0, 'l'},
			{"help",       no_argument, 0, 'h'},
			{0, 0, 0, 0}
		};
		int option_index = 0;
		int c;

		c = getopt_long(argc, argv, "s::n::e::uplh", Options, &option_index);
		if (c == -1)
			break;
		switch(c) {
		case 'h':
			help_exit(0);
			break;
		case 's': opt_val.summary = 1; opt_val.sumfile = optarg; break;
		case 'n': opt_val.nodes = 1; opt_val.nodefile = optarg; break;
		case 'e': opt_val.edges = 1; opt_val.edgefile = optarg; break;
		case 'u': opt_val.graphflags |= GRAPH_UNDIRECTED; break;
		case 'p': opt_val.graphflags |= GRAPH_NOPARALLEL; break;
		case 'l': opt_val.graphflags |= GRAPH_NOLOOP; break;

		case '?':
			help_exit(1);
			break;
		default: /* should never happen */
			assert(0);
		}
	}
	if (!opt_val.summary && !opt_val.nodes && !opt_val.edges)
		opt_val.summary = 1;
}

static void do_output(const char *filename, int (*cb)(const struct Component *, void *), const struct Graph *gph)
{
	struct context ctx;
	ctx.cidx = 0;
	ctx.dest = (filename == NULL) ? stdout : fopen(filename, "w");
	if (ctx.dest == NULL) {
		error(2, errno, "could not open '%s' for writing", filename);    
	}
	graph_iterate_components(gph, cb, &ctx);
	if (filename != NULL)
		fclose(ctx.dest);
}

int main(int argc, char *argv[]) {
	struct Graph gph;

	parse_options(argc, argv);

	if (graph_init(&gph, opt_val.graphflags))
		error(2, errno, "initialization failed");

	if (graph_add_file(&gph, stdin))
		error(2, errno, "reading graph failed");

	if (opt_val.summary)
		do_output(opt_val.sumfile, &print_component_data, &gph);
	if (opt_val.nodes)
		do_output(opt_val.nodefile, &print_nodes_per_component, &gph);
	if (opt_val.edges)
		do_output(opt_val.edgefile, &print_edges_per_component, &gph);

	if (RUNNING_ON_VALGRIND)
		graph_destroy(&gph);

	return 0;
}

