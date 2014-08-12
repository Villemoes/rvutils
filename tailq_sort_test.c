#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>

#include <sys/queue.h>

#include "tailq_sort.h"

/* Download from http://www.mcs.anl.gov/~kazutomo/rdtsc.html or write your own. */
#include "rdtsc.h"

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define __unused __attribute__((__unused__))


struct president {
	unsigned magic_a;
#define PRESIDENT_MAGIC_A 0xd1b1e22d
	char   first[32];
	char   last[32];
	int    start;
	int    stop;
	TAILQ_ENTRY(president)  list;
	unsigned magic_b;
#define PRESIDENT_MAGIC_B 0x170bb0dd
};
struct movie {
	unsigned magic_a;
#define MOVIE_MAGIC_A 0x7928c2be
	int year;
	char title[60];
	TAILQ_ENTRY(movie)      list;
	unsigned magic_b;
#define MOVIE_MAGIC_B 0xc857ca62
};

struct test {
	unsigned magic_a;
#define TEST_MAGIC_A 0xb596e435
	TAILQ_ENTRY(test)       list;
	unsigned orig;
	int      value;
	unsigned magic_b;
#define TEST_MAGIC_B 0x83b4aa3c
};

#define CHECK_MAGIC(p, val_a, val_b) do {		\
		assert((p)->magic_a == (val_a));	\
		assert((p)->magic_b == (val_b));	\
	} while(0)

#define CHECK_MAGIC_PRESIDENT(p) CHECK_MAGIC(p, PRESIDENT_MAGIC_A, PRESIDENT_MAGIC_B)
#define CHECK_MAGIC_MOVIE(p) CHECK_MAGIC(p, MOVIE_MAGIC_A, MOVIE_MAGIC_B)
#define CHECK_MAGIC_TEST(p) CHECK_MAGIC(p, TEST_MAGIC_A, TEST_MAGIC_B)


static int
cmp_test(const void *a, const void *b, void *ctx __unused)
{
	const struct test *ta = a;
	const struct test *tb = b;
	/* CHECK_MAGIC_TEST(ta); */
	/* CHECK_MAGIC_TEST(tb); */
	return ta->value - tb->value;
	/* return ta->value < tb->value ? -1 : */
	/* 	ta->value > tb->value ? 1 : 0; */
}

static int
cmp_by_first(const void *a, const void *b, void *ctx __unused)
{
	const struct president *pa = a;
	const struct president *pb = b;
	CHECK_MAGIC_PRESIDENT(pa);
	CHECK_MAGIC_PRESIDENT(pb);
	return strcmp(pa->first, pb->first);
}
static int
cmp_by_last(const void *a, const void *b, void *ctx __unused)
{
	const struct president *pa = a;
	const struct president *pb = b;
	CHECK_MAGIC_PRESIDENT(pa);
	CHECK_MAGIC_PRESIDENT(pb);
	return strcmp(pa->last, pb->last);
}
static int
cmp_by_inaug(const void *a, const void *b, void *ctx __unused)
{
	const struct president *pa = a;
	const struct president *pb = b;
	CHECK_MAGIC_PRESIDENT(pa);
	CHECK_MAGIC_PRESIDENT(pb);
	return pa->start - pb->start;
}
static int
cmp_by_length(const void *a, const void *b, void *ctx __unused)
{
	const struct president *pa = a;
	const struct president *pb = b;
	CHECK_MAGIC_PRESIDENT(pa);
	CHECK_MAGIC_PRESIDENT(pb);
	return (pa->stop - pa->start) - (pb->stop - pb->start);
}

static int
cmp_by_year(const void *a, const void *b, void *ctx __unused)
{
	const struct movie *ma = a;
	const struct movie *mb = b;
	CHECK_MAGIC_MOVIE(ma);
	CHECK_MAGIC_MOVIE(mb);
	return ma->year - mb->year;
}
static int
cmp_by_title(const void *a, const void *b, void *ctx __unused)
{
	const struct movie *ma = a;
	const struct movie *mb = b;
	CHECK_MAGIC_MOVIE(ma);
	CHECK_MAGIC_MOVIE(mb);
	return strcmp(ma->title, mb->title);
}

TAILQ_HEAD(pres_head, president);
TAILQ_HEAD(movie_head, movie);
TAILQ_HEAD(test_head, test);

static void
sort_tests(struct test_head *head, int (*cmp)(const void*, const void*, void*))
{
	tailq_sort(head, offsetof(struct test, list), cmp, NULL);
}
static void
sort_movies(struct movie_head *head, int (*cmp)(const void*, const void*, void*))
{
	tailq_sort(head, offsetof(struct movie, list), cmp, NULL);
}
static void
shuffle_movies(struct movie_head *head)
{
	tailq_shuffle(head, offsetof(struct movie, list), NULL, NULL);
}
static void
sort_presidents(struct pres_head *head, int (*cmp)(const void*, const void*, void*))
{
	tailq_sort(head, offsetof(struct president, list), cmp, NULL);
}
static void
shuffle_presidents(struct pres_head *head)
{
	tailq_shuffle(head, offsetof(struct president, list), NULL, NULL);
}




struct president parray[] = {
	{.first = "George", .last = "Washington", .start = 1789, .stop = 1797},
	{.first = "John", .last = "Adams", .start = 1797, .stop = 1801},
	{.first = "Thomas", .last = "Jefferson", .start = 1801, .stop = 1809},
	{.first = "James", .last = "Madison", .start = 1809, .stop = 1817},
	{.first = "James", .last = "Monroe", .start = 1817, .stop = 1825},
	{.first = "John Quincy", .last = "Adams", .start = 1825, .stop = 1829},
	{.first = "Andrew", .last = "Jackson", .start = 1829, .stop = 1837},
	{.first = "Martin Van", .last = "Buren", .start = 1837, .stop = 1841},
	{.first = "William Henry", .last = "Harrison", .start = 1841, .stop = 1841},
	{.first = "John", .last = "Tyler", .start = 1841, .stop = 1845},
	{.first = "James K.", .last = "Polk", .start = 1845, .stop = 1849},
	{.first = "Zachary", .last = "Taylor", .start = 1849, .stop = 1850},
	{.first = "Millard", .last = "Fillmore", .start = 1850, .stop = 1853},
	{.first = "Franklin", .last = "Pierce", .start = 1853, .stop = 1857},
	{.first = "James", .last = "Buchanan", .start = 1857, .stop = 1861},
	{.first = "Abraham", .last = "Lincoln", .start = 1861, .stop = 1865},
	{.first = "Andrew", .last = "Johnson", .start = 1865, .stop = 1869},
	{.first = "Ulysses S.", .last = "Grant", .start = 1869, .stop = 1877},
	{.first = "Rutherford B.", .last = "Hayes", .start = 1877, .stop = 1881},
	{.first = "James A.", .last = "Garfield", .start = 1881, .stop = 1881},
	{.first = "Chester", .last = "Arthur", .start = 1881, .stop = 1885},
	{.first = "Grover", .last = "Cleveland", .start = 1885, .stop = 1889},
	{.first = "Benjamin", .last = "Harrison", .start = 1889, .stop = 1893},
	{.first = "Grover", .last = "Cleveland", .start = 1893, .stop = 1897},
	{.first = "William", .last = "McKinley", .start = 1897, .stop = 1901},
	{.first = "Theodore", .last = "Roosevelt", .start = 1901, .stop = 1909},
	{.first = "William Howard", .last = "Taft", .start = 1909, .stop = 1913},
	{.first = "Woodrow", .last = "Wilson", .start = 1913, .stop = 1921},
	{.first = "Warren G.", .last = "Harding", .start = 1921, .stop = 1923},
	{.first = "Calvin", .last = "Coolidge", .start = 1923, .stop = 1929},
	{.first = "Herbert", .last = "Hoover", .start = 1929, .stop = 1933},
	{.first = "Franklin D.", .last = "Roosevelt", .start = 1933, .stop = 1945},
	{.first = "Harry S", .last = "Truman", .start = 1945, .stop = 1953},
	{.first = "Dwight D.", .last = "Eisenhower", .start = 1953, .stop = 1961},
	{.first = "John F.", .last = "Kennedy", .start = 1961, .stop = 1963},
	{.first = "Lyndon B.", .last = "Johnson", .start = 1963, .stop = 1969},
	{.first = "Richard", .last = "Nixon", .start = 1969, .stop = 1974},
	{.first = "Gerald", .last = "Ford", .start = 1974, .stop = 1977},
	{.first = "Jimmy", .last = "Carter", .start = 1977, .stop = 1981},
	{.first = "Ronald", .last = "Reagan", .start = 1981, .stop = 1989},
	{.first = "George", .last = "Bush", .start = 1989, .stop = 1993},
	{.first = "Bill", .last = "Clinton", .start = 1993, .stop = 2001},
	{.first = "George W.", .last = "Bush", .start = 2001, .stop = 2009},
	{.first = "Barack", .last = "Obama", .start = 2009, .stop = 2017},
};

struct movie marray[] = {
	{.year = 1955, .title = "Revenge of the Creature"},
	{.year = 1955, .title = "Francis in the Navy"},
	{.year = 1955, .title = "Lady Godiva of Coventry"},
	{.year = 1955, .title = "Tarantula"},
	{.year = 1956, .title = "Never Say Goodbye"},
	{.year = 1956, .title = "Star in the Dust"},
	{.year = 1956, .title = "Away All Boats"},
	{.year = 1956, .title = "The First Traveling Saleslady"},
	{.year = 1957, .title = "Escapade in Japan"},
	{.year = 1957, .title = "The Enemy Below"},
	{.year = 1958, .title = "Lafayette Escadrille"},
	{.year = 1958, .title = "Ambush at Cimarron Pass"},
	{.year = 1964, .title = "A Fistful of Dollars"},
	{.year = 1965, .title = "For a Few Dollars More"},
	{.year = 1966, .title = "The Good, the Bad and the Ugly"},
	{.year = 1967, .title = "Le streghe"},
	{.year = 1968, .title = "Hang 'Em High"},
	{.year = 1968, .title = "Coogan's Bluff"},
	{.year = 1968, .title = "Where Eagles Dare"},
	{.year = 1969, .title = "Paint Your Wagon"},
	{.year = 1970, .title = "Two Mules for Sister Sara"},
	{.year = 1970, .title = "Kelly's Heroes"},
	{.year = 1971, .title = "The Beguiled"},
	{.year = 1971, .title = "Play Misty for Me"},
	{.year = 1971, .title = "Dirty Harry"},
	{.year = 1972, .title = "Joe Kidd"},
	{.year = 1973, .title = "High Plains Drifter"},
	{.year = 1973, .title = "Breezy"},
	{.year = 1973, .title = "Magnum Force"},
	{.year = 1974, .title = "Thunderbolt and Lightfoot"},
	{.year = 1975, .title = "The Eiger Sanction"},
	{.year = 1976, .title = "The Outlaw Josey Wales"},
	{.year = 1976, .title = "The Enforcer"},
	{.year = 1977, .title = "The Gauntlet"},
	{.year = 1978, .title = "Every Which Way but Loose"},
	{.year = 1979, .title = "Escape from Alcatraz"},
	{.year = 1980, .title = "Bronco Billy"},
	{.year = 1980, .title = "Any Which Way You Can"},
	{.year = 1982, .title = "Firefox"},
	{.year = 1982, .title = "Honkytonk Man"},
	{.year = 1983, .title = "Sudden Impact"},
	{.year = 1984, .title = "Tightrope"},
	{.year = 1984, .title = "City Heat"},
	{.year = 1985, .title = "Pale Rider"},
	{.year = 1986, .title = "Heartbreak Ridge"},
	{.year = 1988, .title = "The Dead Pool"},
	{.year = 1988, .title = "Bird"},
	{.year = 1989, .title = "Thelonious Monk: Straight, No Chaser"},
	{.year = 1989, .title = "Pink Cadillac"},
	{.year = 1990, .title = "White Hunter Black Heart"},
	{.year = 1990, .title = "The Rookie"},
	{.year = 1992, .title = "Unforgiven"},
	{.year = 1993, .title = "In the Line of Fire"},
	{.year = 1993, .title = "A Perfect World"},
	{.year = 1995, .title = "The Bridges of Madison County"},
	{.year = 1995, .title = "The Stars Fell on Henrietta"},
	{.year = 1995, .title = "Casper"},
	{.year = 1997, .title = "Absolute Power"},
	{.year = 1997, .title = "Midnight in the Garden of Good and Evil"},
	{.year = 1999, .title = "True Crime"},
	{.year = 2000, .title = "Space Cowboys"},
	{.year = 2002, .title = "Blood Work"},
	{.year = 2003, .title = "Mystic River"},
	{.year = 2004, .title = "Million Dollar Baby"},
	{.year = 2006, .title = "Flags of Our Fathers"},
	{.year = 2006, .title = "Letters from Iwo Jima"},
	{.year = 2007, .title = "Grace Is Gone"},
	{.year = 2008, .title = "Changeling"},
	{.year = 2008, .title = "Gran Torino"},
	{.year = 2009, .title = "Invictus"},
	{.year = 2010, .title = "Hereafter"},
	{.year = 2010, .title = "Dave Brubeck: In His Own Sweet Way"},
	{.year = 2011, .title = "J. Edgar"},
	{.year = 2012, .title = "Trouble with the Curve"},
};

static void print_president(const struct president *p)
{
	printf("%d\t%d\t%-20s %s\n", p->start, p->stop - p->start, p->first, p->last);
}
static void print_movie(const struct movie *m)
{
	printf("%d\t%s\n", m->year, m->title);
}

#define check_sorted(h, elem, list, cmpfunc, printfunc)                 \
	do {								\
		for (elem = TAILQ_FIRST(h);				\
		     elem && TAILQ_NEXT(elem, list);			\
		     elem = TAILQ_NEXT(elem, list)) {			\
			if (cmpfunc(elem, TAILQ_NEXT(elem, list), NULL) > 0) { \
				printf("list %s not sorted according to %s:\n", #h, #cmpfunc); \
				printfunc(elem);			\
				printf("\tappears before\n");		\
				printfunc(TAILQ_NEXT(elem, list));	\
				exit(1);				\
			}						\
		}							\
	} while (0)

static int
try_device(const char *dev, void *buf, size_t length)
{
	int fd;
	ssize_t sz;

	fd = open(dev, O_RDONLY);
	if (fd < 0)
		return -1;
	sz = read(fd, buf, length);
	(void) close(fd);
	if (sz < 0 || (size_t)sz != length)
		return -1;
	return 0;
}

static void
fill_values(const struct test_head *head, unsigned short *rstate)
{
	struct test *t;
	unsigned orig = 0;
	TAILQ_FOREACH(t, head, list) {
		/*
		 * We force the values to a smaller range than what is
		 * returned by jrand48(). This allows us to use a
		 * simpler comparison function (just return the
		 * difference between the two values; this is wrong if
		 * that difference may overflow an int), and increases
		 * the chance that there are elements with the same
		 * value, so we also get to test the stability of the
		 * sort.
		 */
		t->value = jrand48(rstate) % (1<<16);
		t->orig = orig++;
	}
}

static int
repeat(struct test_head *head, unsigned short *rstate, int reps, double *aver_cycles)
{
	uint64_t total = 0;
	uint64_t start, stop;
	struct test *t, *t2;
	int ret = 0;

	for (int i = 0; i < reps && !ret; ++i) {
		fill_values(head, rstate);
		start = rdtsc();
		sort_tests(head, &cmp_test);
		stop = rdtsc();
		total += stop-start;

		for (t = TAILQ_FIRST(head);
		     t && (t2 = TAILQ_NEXT(t, list));
		     t = t2) {
			int c = cmp_test(t, t2, NULL);
			if (c > 0 || (c == 0 && t->orig > t2->orig)) {
				fprintf(stderr, "ERROR: Test elements not sorted correctly:\n");
				fprintf(stderr, "\t{.value = %d, .orig = %u}\n", t->value, t->orig);
				fprintf(stderr, "\tappears before\n");
				fprintf(stderr, "\t{.value = %d, .orig = %u}\n", t2->value, t2->orig);
				ret = 1;
			}
		}
	}
	*aver_cycles = (double)total/reps;
	return ret;
}

static int
stress_test(int interactive)
{
	struct test_head nodes;
	unsigned n = 0;
	unsigned short rstate[3];
	struct test *t;
	int reps = interactive ? 100 : 10;
	int ret = 0;

	if (try_device("/dev/urandom", rstate, sizeof(rstate)) && 
	    try_device("/dev/random", rstate, sizeof(rstate))) {
		rstate[0] = getpid();
		rstate[1] = time(NULL);
		rstate[2] = 4; /* http://xkcd.com/221/ */
	}
	

	TAILQ_INIT(&nodes);
  
	if (interactive)
		printf("%-7s\t%-15s\t%-15s\n", "n", "cycles", "cycles/(n log n)");
	while (n < 50000) {
		unsigned newnodes;
		double aver_cycles;
		/*
		 * Check lots of small sizes, to catch weird behaviour on sizes
		 * with certain properties (e.g. near or at powers of two).
		 */
		if (n < 70)
			newnodes = 1;
		else
			do {
				newnodes = (((unsigned) nrand48(rstate)) % (n+1)) + 1;
			} while (newnodes > 10000 || newnodes < 1);
		for (unsigned i = 0; i < newnodes; ++i) {
			t = malloc(sizeof(*t));
			assert(t);
			t->magic_a = TEST_MAGIC_A;
			t->magic_b = TEST_MAGIC_B;
			TAILQ_INSERT_TAIL(&nodes, t, list);
		}
		n += newnodes;

		ret = repeat(&nodes, rstate, reps, &aver_cycles);
		if (ret)
			break;
		if (interactive)
			printf("%7u\t%15g\t%g\n", n, aver_cycles, aver_cycles/(n * log2(n)));
	}

	while (!TAILQ_EMPTY(&nodes)) {
		t = TAILQ_FIRST(&nodes);
		TAILQ_REMOVE(&nodes, t, list);
		free(t);
		--n;
	}
	/*
	 * If n is not 0, this means that one of the sort passes lost
	 * an element on the floor (or that an element magically
	 * appeared). Not good.
	 */
	if (n != 0) {
		fprintf(stderr, "ERROR: Element dropped, n=%u, expected 0\n", n);
		ret = 1;
	}
	return ret;
}


int main(void) {
	struct movie_head movies;
	struct pres_head presidents;

	struct movie *m;
	struct president *p;

	int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);

	if (!interactive)
		return stress_test(interactive);

	TAILQ_INIT(&movies);
	TAILQ_INIT(&presidents);

	for (unsigned i = 0; i < ARRAY_SIZE(marray); ++i) {
		TAILQ_INSERT_TAIL(&movies, &marray[i], list);
		marray[i].magic_a = MOVIE_MAGIC_A;
		marray[i].magic_b = MOVIE_MAGIC_B;
	}
	for (unsigned i = 0; i < ARRAY_SIZE(parray); ++i) {
		TAILQ_INSERT_TAIL(&presidents, &parray[i], list);
		parray[i].magic_a = PRESIDENT_MAGIC_A;
		parray[i].magic_b = PRESIDENT_MAGIC_B;
	}

	while (1) {
		int c = 0;
#define CHOICE(str)   printf("%d: " str "\n", c++)
		CHOICE("Print movies");
		CHOICE("Sort movies by year");
		CHOICE("Sort movies by title");
		CHOICE("Shuffle movies");
		CHOICE("Print presidents");
		CHOICE("Sort presidents by inauguration year");
		CHOICE("Sort presidents by length of term");
		CHOICE("Sort presidents by first name");
		CHOICE("Sort presidents by last name");
		CHOICE("Shuffle presidents");
		CHOICE("Run stress test");
		CHOICE("Quit");
		printf("Your choice: ");
		if (scanf("%d", &c) != 1)
			break;

		switch (c) {
		case 0:
			TAILQ_FOREACH(m, &movies, list)
				print_movie(m);
			break;
		case 1:
			sort_movies(&movies, &cmp_by_year);
			check_sorted(&movies, m, list, cmp_by_year, print_movie);
			break;
		case 2:
			sort_movies(&movies, &cmp_by_title);
			check_sorted(&movies, m, list, cmp_by_title, print_movie);
			break;
		case 3:
			shuffle_movies(&movies);
			break;
		case 4:
			TAILQ_FOREACH(p, &presidents, list)
				print_president(p);
			break;
		case 5:
			sort_presidents(&presidents, &cmp_by_inaug);
			check_sorted(&presidents, p, list, cmp_by_inaug, print_president);
			break;
		case 6:
			sort_presidents(&presidents, &cmp_by_length);
			check_sorted(&presidents, p, list, cmp_by_length, print_president);
			break;
		case 7:
			sort_presidents(&presidents, &cmp_by_first);
			check_sorted(&presidents, p, list, cmp_by_first, print_president);
			break;
		case 8:
			sort_presidents(&presidents, &cmp_by_last);
			check_sorted(&presidents, p, list, cmp_by_last, print_president);
			break;
		case 9:
			shuffle_presidents(&presidents);
			break;
		case 10:
			stress_test(1);
			break;
		default:
			exit(0);
		}

	}

	return 0;
}
