#!/usr/bin/perl

use strict;
use warnings;

use Getopt::Long;
use File::Basename;

use Errno qw(EMFILE ENFILE);

# --column
# --template
### --substr
# --header

# TODO:
#
# * Add option to remove the selected column from the output.
#
# * Add option to sanitize the column content, e.g. replace / by _,
# maybe also handle other filename-unfriendly chars.

my $template = undef;
my $prefix = undef;
my $suffix = undef;
my $template_substr = 'XXX';
my $header = 0;
my $col = 1;
my $delim = "\t";
my $headerline = undef;
my $help = 0;


sub err_exit {
    my $fmt = shift;
    my $txt = sprintf($fmt, @_);
    $txt .= "\n" unless $txt =~ m/\n$/;
    printf STDERR $txt;
    exit(1);
}

sub print_usage {
    err_exit("usage: %s [--template=aXXXb] [--column=<n>] files", $0);
}

sub print_help {
    print <<HELP;
$0 - split a file using column contents

Options:
  --column n      Use column n (counting 1-based), default 1
  --header        Treat the first line of each input file as a header line.
                  That line will be copied verbatim to each output file.
  --template str  Use str as the template for output files names. str
                  should contain the substring XXX precisely once -
                  this will be replaced by the contents of the
                  column.

If --template is not given, the output from an input file foo.bar.ext
will use the template foo.bar_XXX.ext. If the input filename contains
no ., the implicitly used template is filename_XXX.

The only supported column delimiter is tab (\\t).

For example, suppose foo contains four lines

    horse     4
    spider    8
    cow       4
    octopus   8
    human     2
    millipede 1000

Running "$0 --column 2 foo" will produce four files, foo_2, foo_4,
foo_8, foo_1000, containing 1, 2, 2, and 1 lines, respectively. Had
the option "--template animals_with_XXX_legs" been given, the output
files would have had more descriptive names.

Sanitizing the column contents or the resulting file names is not
attempted. It is obviously dangerous if the column may contain
slashes, so this should be avoided, as should any character which one
wouldn't want to end up in a filename.

Use - to read from stdin. When reading from stdin, the filename
implicitly used for generating output file names is STDIN, so it is
recommended to use the --template option in that case.
HELP
    exit(0);
}

GetOptions("template=s" => \$template,
#           "substr=s"   => \$template_substr,
           "header!"    => \$header,
           "column=i"   => \$col,
           "help|h"     => \$help)
    or print_usage();

if ($help) {
    print_help();
}

err_exit("column index must be positive") if ($col <= 0);
# $col is given 1-based, but we use 0-based indexing internally
$col--;

if (defined $template) {
    if ($template =~ m/^(.*)$template_substr(.*)$/) {
	$prefix = $1;
	$suffix = $2;
    }
    else {
	err_exit("the substring %s must occur precisely once in the template",
		 $template_substr);
    }
}

sub compute_prefix_suffix {
    my $infn = shift;
    if ($infn =~ m/^(.*)(\..*)$/) {
	$prefix = $1 . '_';
	$suffix = $2;
    }
    else {
	$prefix = $infn . '_';
	$suffix = '';
    }
}


my %outfiles = ();
my $listhead = {};
$listhead->{next} = $listhead->{prev} = $listhead;
my $number_of_open_files = 0; # number of files open for writing, that is

sub compute_outfn {
    my $col = shift;
    return $prefix . $col . $suffix;
}

sub do_input_file {
    my $infile = shift;
    my $h = $infile eq '-' ? \*STDIN : open_read($infile);
    $infile = 'STDIN' if $infile eq '-';
    my ($line, $x, $outfn, $outhandle);

    if ($header) {
        $headerline = <$h>;
    }

    compute_prefix_suffix($infile) if (!defined $template);

    while (<$h>) {
        # Save a copy of the text and then chomp $_. We don't want the
        # terminating newline to show up in $x if we are splitting
        # based on the last column.
        $line = $_;
        chomp;
        $x = (split /\Q$delim\E/o)[$col];
        defined $x
            or die "not enough columns on line $. of $infile";
        $outfn = compute_outfn($x);
        $outhandle = get_output_handle($outfn);
        print $outhandle $line;
    }
    close($h);
}


# We work around EMFILE/ENFILE as follows:
#
# Files open for output are tracked in %outfiles. For a given filename
# $fn, $outfiles{$fn} is a ref to a hash with three members: next, prev, handle.
#
# ->{handle} is, if defined, an file handle opened for writing to
# $fn. The two other fields are used for maintaining a LRU list of
# open file handles; the use of this is explained below.
#
# When we read a line of input and figure out what file $fn that line
# should go to, we first try to look up $fn in %outfiles. If it exists
# and ->{handle} is defined, we move $outfiles{$fn} to the front of
# the LRU list and use the handle directly.
#
# If $outfiles{$fn} exists but ->{handle} is not defined, it means
# that the file has at some point been opened, but we needed to close
# it to free up a file descriptor. In that case, we will open the file
# again, but in append mode so as not to overwrite the already written
# contents.
#
# If $outfiles{$fn} does not exist, we have never encountered $fn
# before. So open $fn for writing and insert an appropriate anon href
# in %outfiles.
#
# In either case, the just (re)opened file is inserted in the front of
# the list of open files.
#
# Now, if (re)opening a file for writing (appending) fails, and errno
# (aka $! in perl) is ENFILE or EMFILE, we may be able to solve the
# problem by closing an open file and trying again. We always close
# the least recently used file, since real input has a tendency to be
# somewhat sorted. We keep trying to close file handles as long as we
# have any file open for output (in the case of ENFILE, some other
# process may have successfully opened a file after we closed one but
# before we got around to trying again). If we run out of files to
# close, or if we encounter an error other than E[MN]FILE, we die with
# an error message.
#

sub list_insert_head {
    my $href = shift;
    die unless (!defined $href->{prev} && !defined $href->{next});
    $href->{next} = $listhead->{next};
    $href->{next}->{prev} = $href;
    $href->{prev} = $listhead;
    $listhead->{next} = $href;
}
sub list_remove {
    my $href = shift;
    die if $href == $listhead;
    $href->{prev}->{next} = $href->{next};
    $href->{next}->{prev} = $href->{prev};
    $href->{prev} = $href->{next} = undef;
}
sub list_move_to_front {
    my $href = shift;
    return if $listhead->{next} == $href;
    list_remove($href);
    list_insert_head($href);
}
sub list_remove_last {
    my $href = $listhead->{prev};
    return undef if $href == $listhead;
    list_remove($href);
    return $href;
}

sub get_output_handle {
    my $fn = shift;
    my $href = exists $outfiles{$fn} ? $outfiles{$fn} : undef;
    if (defined $href && defined $href->{handle}) {
        list_move_to_front($href);
        return $href->{handle}
    }
    if (defined $href) {
        $href->{handle} = open_append($fn);
    }
    else {
        $href = {prev => undef, next => undef, handle => open_write($fn)};
        $outfiles{$fn} = $href;
    }
    list_insert_head($href);
    $number_of_open_files++;
    return $href->{handle};
}

sub open_read {
    my $fn = shift;
    my ($h, $r);
    while (!($r = open($h, '<', $fn)) && ($! == EMFILE || $! == ENFILE)) {
        last if $number_of_open_files == 0;
        close_lru_file();
    }
    if (!$r) {
        die "unable to open $fn for reading: $!";
    }
    return $h;
}
sub open_write {
    my $fn = shift;
    my ($h, $r);
    while (!($r = open($h, '>', $fn)) && ($! == EMFILE || $! == ENFILE)) {
        last if $number_of_open_files == 0;
        close_lru_file();
    }
    if (!$r) {
        die "unable to open $fn for writing: $!";
    }
    print $h $headerline if defined $headerline;
    return $h;
}
sub open_append {
    my $fn = shift;
    my ($h, $r);
    while (!($r = open($h, '>>', $fn)) && ($! == EMFILE || $! == ENFILE)) {
        last if $number_of_open_files == 0;
        close_lru_file();
    }
    if (!$r) {
        die "unable to open $fn for appending: $!";
    }
    return $h;
}

sub close_lru_file {
    if ($number_of_open_files <= 0) {
        die "BUG! ", (caller(0))[3], "() should not be called when number_of_open_files == $number_of_open_files";
    }
    my $href = list_remove_last();
    if (!defined $href) {
        die "BUG! ", (caller(0))[3], "() called while no files appear to be open for writing";
    }
    close($href->{handle}) or die "closing file handle failed: $!";
    $href->{handle} = undef;
    $number_of_open_files--;
}



push @ARGV, '-' if (@ARGV == 0);
for my $inp (@ARGV) {
    do_input_file($inp);
}

# Close all output files. We reuse close_lru_file() so that errors are reported.
while ($number_of_open_files > 0) {
    close_lru_file();
}
