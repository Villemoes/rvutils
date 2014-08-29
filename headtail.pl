#!/usr/bin/perl

use strict;
use warnings;

use Getopt::Long;

# We allow three options:
#
# -h controls the number of 'head' lines
# -t controls the number of 'tail' lines
# -n controls both of the above
#
# Of course, if -h or -t is present, it takes precedence over -n. So
# "-h 5 -n 7" is equivalent to "-h 5 -t 7". An unspecified value
# defaults to 10.

my $head = undef;
my $tail = undef;
my $both = 10;

# This seems to be necessary to allow "-h4" instead of "-h 4".
Getopt::Long::Configure ("bundling");
GetOptions("h|head=i" => \$head,
	   "t|tail=i" => \$tail,
	   "n=i" => \$both);

$head = $both unless defined $head;
$tail = $both unless defined $tail;

my $lines = 0;
my @buffer = ();

while (<>) {
    if ($lines++ < $head) {
	print;
    } else {
	push @buffer, $_;
	shift @buffer if @buffer > $tail;
    }
}

# To avoid printing a line twice if the number of input lines turned
# out to be less than $head+$tail, we've only put lines into @buffer
# if they were not already printed, and we've capped the size of
# @buffer at $tail. So now we just need to print @buffer.
print @buffer;
