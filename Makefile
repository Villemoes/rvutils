CC=gcc

WARNINGFLAGS = -Wall -Wextra -Wstrict-overflow=5 -Wunused-parameter	\
-Wmissing-parameter-type -Wlogical-op -Wfloat-equal -Wpointer-arith	\
-Wshadow -Wstrict-prototypes -Wwrite-strings -Wmissing-noreturn		\
-Wmissing-prototypes -Wparentheses

INCLUDEFLAGS=
LINKFLAGS=

CFLAGS = -g -pthread -O2 -std=gnu99 -D_GNU_SOURCE $(WARNINGFLAGS) $(INCLUDEFLAGS)

OBJ = tailq_sort.o open_noatime.so
PROG = quickstat

TESTPROG = tailq_sort_test


depsdir = deps.d
depssuffix = deps

-include $(patsubst %,$(depsdir)/%,$(OBJ:.o=.$(depssuffix)))

.PHONY: all test

all: $(OBJ) $(PROG)

test: $(TESTPROG)
	@for x in $(TESTPROG) ; do \
		./$$x < /dev/null ; \
	done

$(depsdir):
	mkdir -p $@

%.so: %.c | $(depsdir)
	$(CC) $(CFLAGS) -shared -fPIC -MMD -MF $(depsdir)/$*.$(depssuffix) -o $@ $< $(LINKFLAGS)

%.o: %.c | $(depsdir)
	$(CC) $(CFLAGS) -fPIC -MMD -MF $(depsdir)/$*.$(depssuffix) -c -o $@ $<

%: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LINKFLAGS)

open_noatime.so: LINKFLAGS += -ldl

quickstat: LINKFLAGS += -lm -lgsl -lgslcblas

tailq_sort_test: tailq_sort.o
tailq_sort_test: LINKFLAGS += -lm

