#!/usr/bin/perl -w
use strict;
use FileHandle;

# possible values that can be extracted from the file...
my ($cpus, $max_size, $max_resident, $copied, $malloc);

die "Usage: <test|header> <step> <file> " unless @ARGV == 3;

my ($test,$what,$file) = @ARGV;

parse_file($file);

if (defined $copied && defined $malloc && defined $max_resident && defined $max_size) {
    if ($test eq 'header') {
	print <<'END_OF_HEADER';
                                     Maximum (MiB)      Copied    Malloc
     *Test*      Operation  CPU(s)   Size  Resident      (MiB)     (MiB)
---------------- ---------  ------  -------  -------   --------  --------
END_OF_HEADER
    }
    eval '
    format STDOUT =
@<<<<<<<<<<<<<<< @<<<<<<<<  @###.#  @###.##  @###.##   @#######  @#######
$test, $what, $cpus, $max_size, $max_resident, $copied, $malloc
.
    '; die $@ if $@
} elsif (defined $max_resident && defined $max_size) {
    if ($test eq 'header') {
	print <<'END_OF_HEADER';
                                     Maximum (MiB)  
     *Test*      Operation  CPU(s)   Size  Resident  
---------------- ---------  ------  -------  ------- 
END_OF_HEADER
    }
    eval '
    format STDOUT =
@<<<<<<<<<<<<<<< @<<<<<<<<  @###.#  @###.##  @###.##  
$test, $what, $cpus, $max_size, $max_resident, 
.
    '; die $@ if $@
} else {
    if ($test eq 'header') {
	print <<'END_OF_HEADER';
     *Test*      Operation  CPU(s) 
---------------- ---------  ------ 
END_OF_HEADER
    }
    eval '
    format STDOUT =
@<<<<<<<<<<<<<<< @<<<<<<<<  @###.#
$test, $what, $cpus
.
    '; die $@ if $@
}

exit(0) if $test eq 'header';

$max_size ||= -1;
$max_resident ||= -1;
$copied ||= -1;
$malloc ||= -1;

write;

sub parse_file {
    my($file) = @_;

    my $fh = new FileHandle($file) or die "Can't open $file for read: $!";
    my ($usertime,$systime);
    while(<$fh>) {
	# parse builtin accounting...
	$cpus = $1 + $2
	    if /^STATS: User time: (\d+\.\d+)s, System time: (\d+\.\d+)s$/o;
	($max_size,$max_resident) = ($1,$2)
	    if /^STATS: Max Size MiB: (\d+\.\d+), Max Resident MiB: (\d+\.\d+)$/o;
	$copied = $1
	    if /^STATS: MiB copied: (\d+\.\d+), Total/o;
	$malloc = $1
	    if /^STATS: MiB malloced: (\d+\.\d+), Malloc/o;

	# parse external accounting by /usr/bin/time on debian...
	$usertime = $1 * 60 + $2
	    if /^user\s+(\d+)m(\d+\.\d+)s$/o;
	$systime = $1 * 60 + $2
	    if /^sys\s+(\d+)m(\d+\.\d+)s$/o;
	# parse external accounting by time in zsh...
	($usertime,$systime) = ($1,$2) 
	    if /^(\d+\.\d+)user (\d+\.\d+)system .*CPU/o;
    }

    if (defined $usertime || defined $systime) {
	die "both internal and external statistics?? log in $file"
	    if defined $cpus;
	die "missing user or system time?? log in $file"
	    unless defined $usertime && defined $systime;
	$cpus = $usertime + $systime;
    }
    die "internal, didn't get cpu seconds from $file?!" unless defined $cpus;
}

