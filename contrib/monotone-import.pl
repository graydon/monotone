#! /usr/bin/perl

# Copyright (c) 2005 by Richard Levitte <richard@levitte.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;
use File::Spec::Functions qw(:ALL);

my $VERSION = '0.6';

######################################################################
# User options
#
my $help = 0;
my $man = 0;
my $user_database = undef;
my $user_branch = undef;
my $user_message = undef;
my $user_tag = undef;
my $quiet = 0;
my $debug = 0;

GetOptions('help|?' => \$help,
	   'man' => \$man,
	   'db=s' => \$user_database,
	   'branch=s' => \$user_branch,
	   'message=s' => \$user_message,
	   'tag=s' => \$user_tag,
	   'quiet' => \$quiet,
	   'debug' => \$debug) or pod2usage(2);

$SIG{HUP} = \&my_exit;
$SIG{KILL} = \&my_exit;
$SIG{TERM} = \&my_exit;
$SIG{INT} = \&my_exit;

######################################################################
# Respond to user input
#

# For starters, output help if requested
pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;

# Then check for certain conditions:
  # If --debug was used, refuse to be quiet
$quiet = 0 if $debug;

######################################################################
# Make sure we have a database, and that the file spec is absolute.
#


my_error("No database given\n") if !defined $user_database;
my_error("No message given\n") if !defined $user_message;
my_error("No branch given\n") if !defined $user_branch;
my_error("No tag given\n") if !defined $user_tag;

if (!file_name_is_absolute($user_database)) {
    $user_database = rel2abs($user_database);
}

######################################################################
# Set up internal variables.
#
my $database = "--db='$user_database'";
my_debug("using database $user_database");

my @files_to_clean_up = ();
my @directories_to_clean_up = ();

my @branches =
    grep { $_ eq ${user_branch} }
	map { chomp; $_ } my_backtick("mtn $database list branches");

if ($#branches > 0) {
    my_error("More than one branch named $user_branch.  This is a serious error in your database!\n");
}

# In case the branch doesn't yet exist, we have no revision.  That's OK!
my @heads = ();
if ($#branches == 0) {
    @heads =
	map {
	    chomp; $_;
	} my_backtick("mtn $database automate heads '$user_branch'");

    if ($#heads > 0) {
	my_error("More than one head in the branch $user_branch.  Please merge before importing\n");
    }
}

######################################################################
# Check if this is a monotone work directory, and bail out if it is.
#
my $MT_dir = catdir(curdir(),"_MTN");
if (-d $MT_dir) {
    my_error("This is a monotone work directory, unsafe to import\n");
}

######################################################################
# Construct the monotone control subdirectory and files.
# This makes the directory to import look like a monotone working
# directory.  This is really the simplest trick to do an import, but
# note that it is sensitive to changes in monotone.
#
my_debug("creating control directory $MT_dir");
mkdir $MT_dir;
push @directories_to_clean_up, $MT_dir;

my ($format, $options, $revision, $log) = map { catfile($MT_dir, $_) } ("format", "options",
							 "revision", "log");

open FORMAT, ">$format" || my_error("Couldn't create $format: $!\n");
print FORMAT "2\n";
close FORMAT;

open OPTIONS, ">$options" || my_error("Couldn't create $options: $!\n");
print OPTIONS "database \"$user_database\"\n";
print OPTIONS "  branch \"$user_branch\"\n";
print OPTIONS "  keydir \"$ENV{HOME}/.monotone/keys\"\n";
close OPTIONS;

my $old_revision = ($heads[0] || "");
my $new_manifest = "";
if ($old_revision ne '') {
    ($new_manifest) =
        map  { $_ =~ s/^.*new_manifest\s+\[(.+)\].*$/$1/s; $_ }
        grep { m/new_manifest\s+\[.+\]/s; }
        my_backtick("mtn $database automate get_revision '$old_revision'");
}
open REVISION, ">$revision" || my_error("Couldn't create $revision: $!\n");
print REVISION "format_version \"1\"\n";
print REVISION "\n";
print REVISION "new_manifest [$new_manifest]\n";
print REVISION "\n";
print REVISION "old_revision [$old_revision]\n";
close REVISION;

open LOG, ">$log" || my_error("Couldn't create $log: $!\n");
print LOG "";
close LOG;

######################################################################
# Figure out what files dropped out since the last import, and have them
# explicitely removed. Make sure any attributes associated with them are
# removed as well.
#
my_debug("determining files to remove");
map {
    chomp;
	# Because monotone will complain and refuse to do anything if
	# a file is missing before it's dropped, we need to make sure
	# it's there long enough to be able to drop it.  So, we "touch"
	# the file.  Let's not forget to create intermediary directories
	# if needed...
	my @current_dir = ( File::Spec->curdir() );
	my @created_dirs = ();
	map {
	    push @current_dir, $_;
	    my $d = File::Spec->catdir(@current_dir);
	    if (! -d $d) {
		mkdir $d;
		unshift @created_dirs, $d;
	    }
	} File::Spec->splitdir(dirname($_));
	open FILE, ">$_"; close FILE; # touch

	my_system("mtn drop '$_'");
	my_system("mtn attr drop '$_' mtn:execute");

	unlink $_;
	map { rmdir $_ } @created_dirs;
} my_backtick("mtn list missing");

######################################################################
# Figure out what files are new since the last import, and have them
# explicitely added.
#
my_debug("determining files to add");
my @new_files = map { chomp; my_system("mtn add '$_'"); $_ }
		    my_backtick("mtn list unknown");

######################################################################
# Figure out which of the new files are executable, and give them the
# execute attribute.
#
my_debug("determining files with executable attribute");
map {
    if (-x $_) {
	my_system("mtn attr set '$_' mtn:execute true");
    }
} @new_files;

######################################################################
# Commit and tag.
#
my_debug("commit and tag");
my_system("mtn commit --message='$user_message'");
my ($newrev) = map { chomp; $_ } my_backtick("mtn automate get_base_revision_id");
my_system("mtn tag '$newrev' '$user_tag'");

######################################################################
# Tell the user what he can do with the import.
#
print "********** IMPORTANT NOTICE **********\n";
print "If you want the changes that come with the import to appear in\n";
print "another branch (like your development branch), do the following\n";
print "\n";
print "mtn $database propagate '$user_branch' {your-chosen-branch}\n";
print "**************************************\n";

######################################################################
# Clean up.
#
my_exit();

######################################################################
# Subroutines
#

# my_log will simply output all it's arguments, prefixed with "Notify: ",
# unless $quiet is true.
sub my_log
{
    if (!$quiet && $#_ >= 0) {
	print STDERR "Notify: ", join("\nNotify: ",
				      split("\n",
					    join('', @_))), "\n";
    }
}

# my_errlog will simply output all it's arguments, prefixed with "Notify: ".
# my_error will output all it's arguments, prefixed with "Notify: ", then die.
sub my_error
{
    my $errorstring = join("\nNotify: ", split("\n", join('', @_)));
    die $errorstring;
}

# debug will simply output all it's arguments, prefixed with "DEBUG: ",
# when $debug is true.
sub my_debug
{
    if ($debug && $#_ >= 0) {
	print STDERR "DEBUG: ", join("\nDEBUG: ",
				     split("\n",
					   join('', @_))), "\n";
    }
}

# my_system does the same thing as system, but will print a bit of debugging
# output when $debug is true.  It will also die if the subprocess returned
# an error code.
sub my_system
{
    my $command = shift @_;

    my_debug("'$command'\n");
    my $return = system($command);
    my $exit = $? >> 8;
    die "'$command' returned with exit code $exit\n" if ($exit);
    return $return;
}

# my_conditional_system does the same thing as system, but will print a bit
# of debugging output when $debug is true, and will only actually run the
# command if the condition is true.  It will also die if the subprocess
# returned an error code.
sub my_conditional_system
{
    my $condition = shift @_;
    my $command = shift @_;
    my $return = 0;		# exit code for 'true'

    my_debug("'$command'\n");
    if ($condition) {
	$return = system($command);
	my $exit = $? >> 8;
	die "'$command' returned with exit code $exit\n" if ($exit);
    } else {
	my_debug("... not actually executed.\n");
    }
    return $return;
}

# my_backtick does the same thing as backtick commands, but will print a bit
# of debugging output when $debug is true.  It will also die if the subprocess
# returned an error code.
sub my_backtick
{
    my $command = shift @_;

    my_debug("\`$command\`\n");
    my @return = `$command`;
    my $exit = $? >> 8;
    if ($exit) {
	my_debug(map { "> ".$_ } @ return);
	die "'$command' returned with exit code $exit\n";
    }
    return @return;
}

# my_exit removes temporary files and directories, then exits.
sub my_exit
{
    my_log("cleaning up.");
    unlink @files_to_clean_up;
    my @reverse_dirs = ();
    while(@directories_to_clean_up) {
	my $dir = $directories_to_clean_up[0];
	if (opendir DIR, $dir) {
	    my @dircontent = grep { !/^\.$/ && !/^\.\.$/ } readdir DIR;
	    closedir DIR;
	    my @newdirs =
		grep { ! -l $_ && -d $_ } map { catdir($dir,$_) } @dircontent;
	    map {
		my_debug("unlink $_");
		unlink $_ unless $debug;
		} grep { -l $_ || ! -d $_ } map { catfile($dir,$_) } @dircontent;
	    if (@newdirs) {
		push @directories_to_clean_up, @newdirs;
	    }
	    unshift @reverse_dirs, $dir;
	}
	shift @directories_to_clean_up;
    }
    foreach (@reverse_dirs) {
	my_debug("rmdir $_");
	rmdir $_ unless $debug;
    }
    my_log("all done.");
    exit(0);
}

# list_size returns the size of the list.  It's better than $#{var}
# because it doesn't require the input to be a variable, and it
# doesn't return one less than the size.
sub list_size
{
    return $#_ + 1;
}


__END__

=head1 NAME

monotone-import.pl - a script to send monotone change notifications by email

=head1 SYNOPSIS

monotone-import.pl [--help] [--man]
[--db=database] [--branch=branch] [--message=message] [--tag=tag]
[--quiet] [--debug]

=head1 DESCRIPTION

B<monotone-import.pl> is used to subsequently import unversioned
third-party sources into a Monotone repository for versioning purposes.
To some extend this resembles the "vendor branch" approach of CVS's "cvs
import" command.

=head1 OPTIONS

=over 4

=item B<--help>

Print a brief help message and exit.

=item B<--man>

Print the manual page and exit.

=item B<--db>=I<database>

Sets which database the import should be stored in.

=item B<--branch>=I<branch>

Sets which branch the import should be placed in.

=item B<--message>=I<message>

Sets the message for the commit of the import.

=item B<--tag>=I<tag>

Sets the tag to be associated with the import.

=item B<--debug>

Makes B<monotone-import.pl> go to debug mode.  It means a LOT of extra
output.

=item B<--quiet>

Makes B<monotone-import.pl> really silent.  It will normally produce a
small log of it's activities, but with B<--quiet>, it will only output
error messages.  If B<--debug> was given, B<--quiet> is turned off
unconditionally.

=back

=head1 NOTES

This program was designed to mimic "cvs import".  Still, there are a
few differences:

=over 2

=item *

B<monotone-import.pl> never decides for you which branch the import
is going to.

=item *

B<monotone-import.pl> will not propagate the import anywhere, since
there's no defined trunk.  It's left to the user to do that, and
B<monotone-import.pl> ends with a friendly note instructing the user
how to do so.

=back

=head1 BUGS

Probably...

=head1 SEE ALSO

L<monotone(1)>

=head1 AUTHOR

Richard Levitte, <richard@levitte.org>

=head1 COPYRIGHT AND LICENSE

Copyright (c) 2005 by Richard Levitte <richard@levitte.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

=over 3

=item 1.

Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

=item 2.

Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

=back

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=cut
