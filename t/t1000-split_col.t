#!/bin/bash

test_description='Test the split_col utility'

. sharness/sharness.sh

datafile='Medal_table_Winter_Olympics.txt'

if ! cp "${SHARNESS_TEST_DIRECTORY}/${datafile}" .
then
    skip_all="copying ${datafile} to working directory failed"
    test_done
fi

check_split () {
    local column=$1
    local template=$2
    local infile=$3
    : ${infile:=${datafile}}
    # Get the list of unique values occuring in the given column, then
    # check that the corresponding file exists and has the correct
    # contents. The order of lines is supposed to be preserved.
    cut -f "${column}" "${infile}" | sort -u | while read foo ; do
	[ -e "${template/XXX/$foo}" ] || return 1
	diff -q <(awk -F "\t" "\$${column} == \"${foo}\"" "${infile}") "${template/XXX/$foo}" || return 2
    done || return $?
    return 0
}

test_expect_success "illegal column number" \
    "test_must_fail split_col --column 0 < /dev/null"

test_expect_success "column number out of bounds" \
    "test_must_fail split_col --column 100 ${datafile}"

test_expect_success "illegal template" \
    "test_must_fail split_col --template hello_XYZ_world ${datafile}"

test_expect_success "default settings" \
    "split_col ${datafile} && check_split 1 ${datafile%.txt}_XXX.txt"

test_expect_success "default settings, stdin" \
    "split_col < ${datafile} && check_split 1 STDIN_XXX"

test_expect_success "column with spaces" \
    "split_col -c 4 ${datafile} && check_split 4 ${datafile%.txt}_XXX.txt"

test_expect_success "last column" \
    "split_col -c 8 ${datafile} --template by.medal.XXX && check_split 8 by.medal.XXX"

if [ -e 'by.medal.Gold' ] && [ -e 'by.medal.Silver' ] && [ -e 'by.medal.Bronze' ]
then
    test_expect_success "multiple inputs, implicit template" \
	'split_col -c 1 by.medal.* && \
         ( for m in Gold Silver Bronze ; do check_split 1 by.medal_XXX.${m} by.medal.${m} || exit 1 ; done )'

    # When we first split by medal, then use these files as input (in
    # alphabetic order; the shell globbing does that), the result
    # should be equivalent to first (stably) sorting the original
    # input by medal, then applying split_col.
    test_expect_success "multiple inputs, explicit template" \
	"split_col -c 5 --template=by-country_XXX.txt by.medal.* && \
         sort -s -k8 -t$'\t' ${datafile} > sorted_by_medal.txt &&
         check_split 5 by-country_XXX.txt sorted_by_medal.txt"
fi



test_done
