#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>

#include <math.h>
#include <fenv.h>

#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_sort.h>
#include <gsl/gsl_histogram.h>

/* 
 * Reads numbers (floats) from stdin. They can be separated by any
 * kind of whitespace. Infs and NaNs are ignored.
 *
 * Prints on stdout a few statistics about the numbers (count, max,
 * min, average and so on).
 */

static void
usage(void) 
{
	fprintf(stderr, "%s [--linhist] [--loghist]\n", program_invocation_short_name);
	fprintf(stderr, "%s --help\n", program_invocation_short_name);
	exit(1);
}

static void
help(void)
{
	printf("%s - get a few basic statistics\n\n", program_invocation_short_name);
	printf("%s reads stdin and records anything that looks like a (floating point) number.\n", program_invocation_short_name);
	printf("It then computes and prints certain statistics about the data set.\n"
	       "The output is meant to be readable by humans, not computers (implying\n"
	       "that the format may change in the future).\n\n");
	printf("The scalar statistics are:\n");
#define STAT(name, description) printf("    %-26s %s\n", name, description)
	STAT("Count", "number of numbers");
	STAT("Sum", "sum of the numbers");
	STAT("Min/Max", "minimum/maximum of the numbers");
	STAT("Median", "median of the numbers (50% percentile, 2nd quartile)");
	STAT("Q25, Q75", "25% and 75% percentiles (1st and 3rd quartiles)");
	STAT("Arithmetic mean", "");
	STAT("Geometric mean", "(only defined and printed if all numbers are positive)");
	STAT("Variance", "unbiased sample variance, \\sum_i (x_i-\\bar x)/(N-1)");
	STAT("Standard deviation", "square root of variance");
	STAT("Absolute deviation", "arithmetic mean of absolute differences to the arithmetic mean");
	STAT("Median absolute deviation", "median of absolute differences to the median");
#undef STAT
	printf("\n");
	printf("Infinities and NaNs are generally ignored, but if any are encountered,\n");
	printf("separate counts of each are printed.\n");
	printf("\n");
	printf("Options\n");
	printf("\n");
	printf("  --linhist[=<bins>]  print a 'linear' histogram of the values, using <bins> (default 10)\n"
	       "                      equally spaced bins between the min and max values\n");
	printf("  --loghist[=<bins>]  print a 'logarithmic' histogram of the values, using <bins>\n"
	       "                      (default 10) bins with endpoints in geometric progression\n"
	       "                      between the min and max values. This option is ignored if there\n"
	       "                      are any non-positive values.\n");
	exit(0);
}

struct optval {
	bool lin_hist;
	bool log_hist;
	size_t lin_hist_size;
	size_t log_hist_size;
} optval = {
	.lin_hist = false,
	.log_hist = false,
	.lin_hist_size = 10,
	.log_hist_size = 10,
};

static size_t bin_count(const char *s)
{
	size_t x;
	char *tail;

	errno = 0;
	x = strtoul(s, &tail, 0);
	if (tail == s || *tail != '\0' || errno == ERANGE)
		error(1, 0, "invalid number of bins: %s", s);
	if (x < 2 || x > 1000)
		error(1, 0, "invalid number of bins, must be between 2 and 1000, inclusive");
	return x;
}


static void parse_options(int argc, char *argv[])
{
	static struct option Options[] = {
		{"linhist", optional_argument, 0, 'n'},
		{"loghist", optional_argument, 0, 'g'},
		{"help",    no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};
	while (1) {
		int c, option_index;
		c = getopt_long(argc, argv, "n::g::h", Options, &option_index);
		if (c == -1)
			break;
		switch (c) {
		case 'n':
			optval.lin_hist = true;
			if (optarg)
				optval.lin_hist_size = bin_count(optarg);
			break;
		case 'g':
			optval.log_hist = true;
			if (optarg)
				optval.log_hist_size = bin_count(optarg);
			break;
		case 'h':
			help();
			break;
		default:
			usage();
			break;
		}
	}
}


struct qstat {
	double *numbers;
	size_t capacity;
	size_t count;

	size_t NaN_count;
	size_t pInf_count;
	size_t nInf_count;

	double sum, min, max, mean, median, q25, q75;
	double var, sd, absdev, mad;
	double geomean;
	gsl_histogram *linhg;
	gsl_histogram *loghg;
	
	bool stats_valid;
};

static int
qstat_init(struct qstat *qs)
{
	memset(qs, 0, sizeof(*qs));
	if (optval.lin_hist) {
		qs->linhg = gsl_histogram_alloc(optval.lin_hist_size);
		if (!qs->linhg)
			return -1;
	}
	if (optval.log_hist) {
		qs->loghg = gsl_histogram_alloc(optval.log_hist_size);
		if (!qs->loghg) {
			if (qs->linhg)
				gsl_histogram_free(qs->linhg);
			return -1;
		}
	}
	return 0;
}
static void
qstat_destroy(struct qstat *qs)
{
	free(qs->numbers);
	if (qs->linhg)
		gsl_histogram_free(qs->linhg);
	if (qs->loghg)
		gsl_histogram_free(qs->loghg);
}



static void
qstat_ensure_capacity(struct qstat *qs)
{
	size_t newcap;
	double *newarray;

	if (qs->count < qs->capacity)
		return;

	newcap = qs->capacity + (qs->capacity)/4 + 4;
	newarray = realloc(qs->numbers, newcap*sizeof(*qs->numbers));
	if (newarray == NULL) {
		error(2, errno, "reallocating %zu -> %zu failed", 
		      qs->capacity * sizeof(double), newcap * sizeof(double));
	}
	qs->numbers = newarray;
	qs->capacity = newcap;
	assert(qs->count < qs->capacity);
}

static char   *line   = NULL;
static size_t linecap = 0;

static void
qstat_append_file(struct qstat *qs, FILE *fp)
{
	ssize_t       linelen;
	qs->stats_valid = false;

#define FLOWS (FE_OVERFLOW|FE_UNDERFLOW)
	(void) feclearexcept(FLOWS);
	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		char *token;
		char *saveptr;
		for (token = strtok_r(line, " \t\n\r\v", &saveptr); token; token = strtok_r(NULL, " \t\n\r\v", &saveptr)) {
			char *dummy;
			double next = strtod(token, &dummy);
			if (dummy == token) {
				/* Just ignore this token. */
				continue;
			}
			if (fetestexcept(FLOWS)) {
				fprintf(stderr, "warning: %s caused %sflow, treated as %g\n", token, 
					fetestexcept(FE_UNDERFLOW) ? "under" : "over", next);
				(void) feclearexcept(FLOWS);
			}
			if (!isfinite(next)) {
				if (isnan(next))
					qs->NaN_count++;
				else if (isinf(next) > 0)
					qs->pInf_count++;
				else if (isinf(next) < 0)
					qs->nInf_count++;
				else
					fprintf(stderr, "Weird! %s satisfies !isfinite && !isnan && !isinf\n", token);
			} else {
				/* Make sure the array numbers has room for one more, then add it */
				qstat_ensure_capacity(qs);
				qs->numbers[qs->count++] = next;
			}
		}
	}
#undef FLOWS
}


static void
qstat_compute(struct qstat *qs)
{
	double *absdiffs;
	size_t i;

	if (!qs->count)
		return;
	gsl_sort(qs->numbers, 1, qs->count);
	qs->mean = gsl_stats_mean(qs->numbers, 1, qs->count);
	qs->sum = qs->mean * qs->count;
	if (qs->count > 1) {
		qs->var = gsl_stats_variance_m(qs->numbers, 1, qs->count, qs->mean);
		qs->sd = sqrt(qs->var);
	}
	else {
		qs->var = qs->sd = NAN;
	}
	qs->absdev = gsl_stats_absdev_m(qs->numbers, 1, qs->count, qs->mean);

	qs->min = qs->numbers[0];
	qs->max = qs->numbers[qs->count-1];
	qs->median = gsl_stats_median_from_sorted_data(qs->numbers, 1, qs->count);
	qs->q25 = gsl_stats_quantile_from_sorted_data(qs->numbers, 1, qs->count, .25);
	qs->q75 = gsl_stats_quantile_from_sorted_data(qs->numbers, 1, qs->count, .75);

	/* Can MAD be computed without O(N) temporary storage? */
	absdiffs = malloc(qs->count * sizeof(*absdiffs));
	if (absdiffs) {
		for (i = 0; i < qs->count; ++i)
			absdiffs[i] = fabs(qs->numbers[i] - qs->median);
		gsl_sort(absdiffs, 1, qs->count);
		qs->mad = gsl_stats_median_from_sorted_data(absdiffs, 1, qs->count);
		free(absdiffs);
	} else {
		error(0, errno, "could not allocate memory for calculation of median absolute deviation");
		qs->mad = NAN;
	}

	if (qs->linhg) {
		/* XXX: If qs->max is the largest finite floating point value, this breaks. */
		gsl_histogram_set_ranges_uniform(qs->linhg, qs->min, nextafter(qs->max, INFINITY));
		for (i = 0; i < qs->count; ++i)
			gsl_histogram_increment(qs->linhg, qs->numbers[i]);
	}

	if (qs->min > 0.0) {
		double logsum = 0.0;
		/*
		 * The geometric mean is best obtained as the
		 * exponential of the arithmetic mean of the
		 * logarithms. Similarly, we get bins in geometric
		 * progression by making a uniform histogram of the
		 * logarithms of the values, then fixing the range of
		 * the bins.
		 */
		if (qs->loghg)
			gsl_histogram_set_ranges_uniform(qs->loghg, log(qs->min), nextafter(log(qs->max), INFINITY));
		for (i = 0; i < qs->count; ++i) {
			double l = log(qs->numbers[i]);
			logsum += l;
			if (qs->loghg)
				gsl_histogram_increment(qs->loghg, l);
		}
		qs->geomean = exp(logsum/((double) qs->count));
		if (qs->loghg) {
			for (i = 0; i <= qs->loghg->n; ++i) {
				qs->loghg->range[i] = exp(qs->loghg->range[i]);
			}
		}
	}

	qs->stats_valid = true;
}

static void
print_double(const char *name, double val)
{
	printf("%-20s  %g\n", name, val);
}
static void
print_size_t(const char *name, size_t val)
{
	printf("%-20s  %zu\n", name, val);
}
static void
print_histogram(const char *title, const gsl_histogram *hist, size_t total)
{
	size_t bins;
	double low, high;
	double freq, frac;
	size_t i;

	bins = gsl_histogram_bins(hist);
	if (title != NULL)
		printf("\t%s\n", title);
	printf("           Range               Frequency\n");
	for (i = 0; i < bins; ++i) {
		gsl_histogram_get_range(hist, i, &low, &high);
		freq = gsl_histogram_get(hist, i);
		frac = freq/total;
		printf("%#10.5g <= x < %-#10.5g\t%8lu (%#5.2f%%)\n", low, high,
			(unsigned long)freq, 100.0*frac);
	}
}


static void
qstat_print(const struct qstat *qs)
{
	if (!qs->stats_valid)
		return;
	print_size_t("Count",       qs->count);
	print_double("Sum",         qs->sum);
	print_double("Arith. mean", qs->mean);

	print_double("Minimum", qs->min);
	print_double("Q25",     qs->q25);
	print_double("Median",  qs->median);
	print_double("Q75",     qs->q75);
	print_double("Maximum", qs->max);

	print_double("Variance",  qs->var);
	print_double("Std. dev.", qs->sd);
	print_double("Abs. dev.", qs->absdev);
	if (!isnan(qs->mad))
		print_double("MAD",       qs->mad);
	if (qs->min > 0.0)
		print_double("Geo. mean", qs->geomean);

	if (qs->pInf_count)
		print_size_t("+Infs", qs->pInf_count);
	if (qs->nInf_count)
		print_size_t("-Infs", qs->nInf_count);
	if (qs->NaN_count)
		print_size_t("NaNs", qs->NaN_count);

	if (qs->linhg)
		print_histogram("Linear histogram", qs->linhg, qs->count);
	if (qs->loghg && qs->min > 0.0)
		print_histogram("Logarithmic histogram", qs->loghg, qs->count);
}


int main(int argc, char *argv[]) {
	struct qstat qs;
	int i;

	parse_options(argc, argv);
	argc -= optind;
	argv += optind;

	if (qstat_init(&qs))
		error(1, errno, "initialization failed");
  
	if (argc <= 0) {
		qstat_append_file(&qs, stdin);
	}
	else {
		for (i = 0; i < argc; ++i) {
			FILE *f = fopen(argv[i], "r");
			if (f == NULL) {
				error(0, errno, "could not open %s", argv[i]);
				continue;
			}
			qstat_append_file(&qs, f);
			fclose(f);
		}
	}

	if (qs.count) {
		qstat_compute(&qs);
		qstat_print(&qs);
	}
	else {
		fprintf(stderr, "no data\n");
	}

	/* Make valgrind happy. */
	qstat_destroy(&qs);
	free(line);

	return 0;
}
