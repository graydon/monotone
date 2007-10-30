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
use MIME::Lite;
use File::Spec::Functions qw(:ALL);

my $VERSION = '1.0';

######################################################################
# User options
#
my $help = 0;
my $man = 0;
my $user_database = undef;
my $root = undef;
my @user_branches = ();
my @user_not_branches = ();
my $update = -1;
my $mail = -1;
my $attachments = 1;
my $ignore_merges = 1;
my $from = undef;
my $difflogs_to = undef;
my $nodifflogs_to = undef;
my $before = undef;
my $since = undef;
my $workdir = undef;
my $quiet = 0;
my $debug = 0;
my $monotone = "mtn";

GetOptions('help|?' => \$help,
	   'man' => \$man,
	   'db=s' => \$user_database,
	   'root=s' => \$root,
	   'branch=s' => \@user_branches,
	   'not-branch=s' => \@user_not_branches,
	   'update!' => \$update,
	   'mail!' => \$mail,
	   'attachments!' => \$attachments,
	   'ignore-merges!' => \$ignore_merges,
	   'from=s' => \$from,
	   'difflogs-to=s' => \$difflogs_to,
	   'nodifflogs-to=s' => \$nodifflogs_to,
	   'before=s' => \$before,
	   'since=s' => \$since,
	   'workdir=s' => \$workdir,
	   'quiet' => \$quiet,
	   'debug' => \$debug,
	   'monotone=s' => \$monotone) or pod2usage(2);

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
  # If --debug was used and --update wasn't, force --noupdate
$update = 0 if ($debug && $update == -1);
  # If --debug was used and --mail wasn't, force --nomail
$mail = 0 if ($debug && $mail == -1);
  # If --debug was used, refuse to be quiet
$quiet = 0 if $debug;

# The check for missing mandatory options (oxymoron, I know :-))
# Actually, they're only mandatory if $mail or $debug is true...
if ($mail || $debug) {
    if (!defined $from) {
	my_errlog("You need to specify a From address with --from");
	pod2usage(2);
    }
    if (!defined $difflogs_to && !defined $nodifflogs_to) {
	my_errlog("You need to specify a To address with --difflogs-to or --nodifflogs-to");
	pod2usage(2);
    }
}

######################################################################
# Make sure we have a database, and that the file spec is absolute.
#

# If no database is given, check the monotone options file (_MTN/options).
# Do NOT use the branch option from there.
if (!defined $user_database) {
    $root = rel2abs($root) if defined $root;
    $root = rootdir() unless defined $root;

    my $curdir = rel2abs(curdir());
    while(! -f catfile($curdir, "_MTN", "options") && $curdir ne $root) {
	$curdir = updir($curdir);
    }
    my $options = catfile($curdir, "_MTN", "options");

    my_debug("found options file $options");

    open OPTIONS, "<$options"
	|| my_error("Couldn't open $options");
    ($user_database) = grep(/^\s*database\s/, map { chomp; $_ } <OPTIONS>);
    close OPTIONS;
    $user_database =~ s/^\s*database\s"(.*)"$/$1/;

    my_log("found the database $user_database in $options.");
    my_log("the branch option from $options will NOT be used.");
} elsif (!file_name_is_absolute($user_database)) {
    $user_database = rel2abs($user_database);
}

######################################################################
# Set up internal variables.
#
my $database = " --db=$user_database";
my_debug("using database $user_database");

my $remove_workdir = 0;
if (!defined $workdir) {
    $workdir = "/var/tmp/monotone_notify.work.$$";
    mkdir $workdir;
    $remove_workdir = 1;
} elsif (! file_name_is_absolute($workdir)) {
    $workdir = rel2abs($workdir);
}
if (! -d $workdir && ! -w $workdir && ! -r $workdir) {
    my_error("work directory $workdir not accessible, exiting");
}
my_debug("using work directory $workdir");
my_debug("(to be removed after I'm done)") if $remove_workdir;

my $branches_re = "^.*\$";
if ($#user_branches >= 0) {
    $branches_re=
	'^('.join('|', map { s/([^a-zA-Z0-9\[\]\*_])/\\$1/g;
			     s/\*/.\*/g;
			     $_ } @user_branches).')$';
}
my $not_branches_re = "^\$";
if ($#user_not_branches >= 0) {
    $not_branches_re=
	'^('.join('|', map { s/([^a-zA-Z0-9\[\]\*\?_])/\\$1/g;
			     s/\?/./g;
			     s/\*/.\*/g;
			     $_ } @user_not_branches).')$';
}
my_debug("using the regular expression /$branches_re/ to select branches");

my @files_to_clean_up = ();

######################################################################
# Move to the working directory.
#
chdir $workdir;
my_debug("changed current directory to $workdir");

######################################################################
# Save all the branches that we want to look at
#
my %branches =
    map { $_ => 1 }
	grep (/$branches_re/,
	      grep (!/$not_branches_re/,
		    map { chomp; $_ }
			my_backtick("$monotone$database list branches")));
my_debug("collected the following branches:\n",
	 map { "  $_\n" } keys %branches);

######################################################################
# Find all the current leaves, for the branches that we want.
#
my_log("finding all current leaves.");
# Format: revision => { branch1 => 1, branch2 => 1, ... }
my %current_leaves = ();
foreach my $branch (keys %branches) {
    foreach my $revision (my_backtick("$monotone$database automate heads $branch")) {
	chomp $revision;
	$current_leaves{$revision} = {} if !defined $current_leaves{$revision};
	$current_leaves{$revision}->{$branch} = 1;
	my_debug("$revision\@$branch\n");
    }
}
my_debug("found ", list_size(keys %current_leaves)," current leaves");

######################################################################
# Find the IDs of the leaves from last run.
#
my_log("finding all old leaves.");
my %old_leaves = ();
foreach my $notify_entry (my_backtick("$monotone$database list vars notify")) {
    chomp $notify_entry;
    if ($notify_entry =~ /^notify:\s([0-9a-fA-F]{40})\@([^\s]+)\s1$/) {
	# Found the new format that keeps track of which branches each
	# revision is part of.
	if (defined $branches{$2}) {
	    $old_leaves{$1} = {} if !defined $current_leaves{$1};
	    $old_leaves{$1}->{$2} = 1;
	}
    } elsif ($notify_entry =~ /^notify:\s([0-9a-fA-F]{40})\s1$/) {
	$old_leaves{$1} = {} if !defined $current_leaves{$1};
	$old_leaves{$1}->{"*"} = 1;
    }
}

# We save them in a file as well, to be used with
# 'automate ancestry_difference', to avoid problems with system
# that have small command line size limits, in case there were
# many heads...
my $old_leaves_file = "old_leaves";
open OLDLEAVES, ">$old_leaves_file"
    || my_error("Couldn't write to $old_leaves_file: $!");
print OLDLEAVES join("\n", keys %old_leaves),"\n";
close OLDLEAVES;

my_debug("found ", list_size(keys %old_leaves),
	 " previous leaves\n (saved in $old_leaves_file)");

if ($mail || $debug) {
    ##################################################################
    # Collect IDs for all revisions we want to log.
    #
    # Use the old_leaves file created by the previous collection.
    #
    my_log("collecting all revision IDs between current and old leaves.");
    my %revisions =
	map { chomp; $_ => 1 }
	    map { my_backtick("$monotone$database automate ancestry_difference $_ -@ $old_leaves_file") }
		keys %current_leaves;
    push @files_to_clean_up, "$old_leaves_file";
    my @revisions_keys = keys %revisions;
    my_debug("found ",
	     list_size(keys %revisions),
	     " revisions to log");

    ##################################################################
    # Collect all the logs.
    #
    # Note that if we would discard it, we skip this step and the next,
    # so as not to waste time...
    #
    my_log("collecting the logs for all collected revision IDs.");
    my %timeline = ();		# hash of revision lists, keyed by date.
    my %revision_data = ();	# hash of logs (represented as arrays of
				# strings), keyed by revision id.

    foreach my $revision (keys %revisions) {
	$revision_data{$revision} =
	    [ map { chomp; $_ }
	      my_backtick("$monotone$database log --no-graph --last=1 --from=$revision") ];
	my $date = (split(' ', (grep(/^Date:/, @{$revision_data{$revision}}))[0]))[1];

	if (defined $before && $date ge $before) {
	    my_debug("Rejecting $revision because it's too recent ($date >= $before (--before))\n");
	    next;
	}
	if (defined $since && $date lt $since) {
	    my_debug("Rejecting $revision because it's too old ($date < $since (--since))\n");
	    next;
	}
	$timeline{$date} = {} if !defined $timeline{$date};
	$timeline{$date}->{$revision} = 1;
    }

    ##################################################################
    # Generate messages.
    #
    my_log("generating messages for all collected revision IDs.");

    my $message_count = 0;	# It's nice with a little bit of statistics.

    foreach my $date (sort keys %timeline) {
	foreach my $revision (keys %{$timeline{$date}}) {
	    foreach my $sendinfo (([ 1, "Notify.debug-diffs", $difflogs_to ],
				   [ 0, "Notify.debug-nodiffs", $nodifflogs_to ])) {
		my $diffs = $sendinfo->[0];
		my $debugfile = $sendinfo->[1];
		my $to = $sendinfo->[2];
		next if !defined $to;

		my @ancestors =
		    map { (split ' ')[1] }
			grep(/^Ancestor:/, @{$revision_data{$revision}});

		# If this revision has more than one ancestor, it's the
		# result of a merge.  If we have already shown the
		# participating ancestors, let's not show the diffs again.
		if ($ignore_merges && $diffs && $#ancestors > 0) {
		    my $will_ignore = 1;
		    my %revision_branches =
			map { (split ' ')[1] => 1 }
			    grep /^Branch:/, @{$revision_data{$revision}};
		    my_debug("Checking if ${revision}'s ancestor have already been shown (probably).");
		    foreach (@ancestors) {
			if (!revision_is_in_branch($_,
						   { %branches,
						     %revision_branches },
						   { %revision_data })) {
			    my_debug("Not ignoring this one!");
			    $will_ignore = 0;
			}
		    }
		    if ($will_ignore) {
			$diffs = 0;
			my_debug("Not showing diff for revision $revision, because all it's ancestors\nhave already been shown.");
		    }
		}

		# If --nodiffs was used, it's silly to use attachments
		my $attach = $attachments;
		$attach = 0 if $diffs == 0;

		my $msg;
		my @files = ();	# Makes sure we have the files in
				# correctly sorted order.
		my %file_info = (); # Hold information about each file.

		# Make sure we have a null ancestor if there are none.
		# generate_diff will do the right thing with it.
		if ($#ancestors < 0) {
		    push @ancestors, "";
		}

		######################################################
		# Create the summary.
		#
		my $summary_file = "message.txt";
		open SUMMARY,">$summary_file"
		    || my_error("Notify: couldn't create $summary_file: $!");
		foreach (@{$revision_data{$revision}}) {
		    print SUMMARY "$_\n";
		}
		if (!$diffs) {
		    print SUMMARY "\n";
		}
		close SUMMARY;
		push @files, $summary_file;

		# This information is only used when $attachments is true.
		$file_info{$summary_file} = { Title => 'change summary',
					      Disposition => 'inline' };

		######################################################
		# Create the diffs.
		#
		if ($attach) {
		    foreach my $ancestor (@ancestors) {
			my $diff_file = "diff.$ancestor.txt";
			generate_diff($database, $ancestor, $revision,
				      ">$diff_file", $diffs, 0);
			push @files, $diff_file;

			$file_info{$diff_file} = {
			    Title => "Diff [$ancestor] -> [$revision]",
			    Disposition => 'attachment' };
		    }
		} else {
		    foreach my $ancestor (@ancestors) {
			generate_diff($database, $ancestor, $revision,
				      ">>$summary_file", $diffs, 1);
		    }
		    open SUMMARY,">>$summary_file"
			|| my_error("Notify: couldn't append to $summary_file: $!");
		    print SUMMARY "-" x 70,"\n";
		    close SUMMARY;
		}

		######################################################
		# Create the email.
		#
		if ($attach) {
		    $msg = MIME::Lite->new(From => $from,
					   To => $to,
					   Subject => "Revision $revision",
					   Type => 'multipart/mixed');
		    foreach my $file (@files) {
			$msg->attach(Type => 'TEXT',
				     Path => $file,
				     Disposition => $file_info{$file}->{Disposition},
				     Encoding => '8bit');
		    }

		    # MIME:Lite has some quircks that we need to deal with
		    foreach my $part ($msg->parts()) {
			my $filename = $part->filename();
			my_debug("message part: $filename: { ",
				 join(', ',
				      map { "$_ => $file_info{$filename}->{$_}" }
				      keys %{$file_info{$filename}}),
				 " }");
			# Hacks to avoid having file names, and to added a
			# description field
			$part->attr("content-disposition.filename" => undef);
			$part->attr("content-type.name" => undef);
			$part->attr("content-description" =>
				    $file_info{$filename}->{Title});
		    }
		} else {
		    $msg = MIME::Lite->new(From => $from,
					   To => $to,
					   Subject => "Revision $revision",
					   Type => 'TEXT',
					   Path => "$summary_file",
					   Encoding => '8bit');
		    # Hacks to avoid having file names
		    $msg->attr("content-disposition.filename" => undef);
		    $msg->attr("content-type.name" => undef);
		}

		######################################################
		# Send it or log it (or discard it).
		#
		if ($mail) {
		    $msg->send();
		} elsif ($debug) {
		    open MESSAGEDBG,">>$debugfile"
			|| my_error("Couldn't create $debugfile: $!");
		    print MESSAGEDBG "======================================================================\n";
		    $msg->print(\*MESSAGEDBG);
		    print MESSAGEDBG "======================================================================\n";
		    close MESSAGEDBG;
		}
		$message_count++;

		######################################################
		# Clean up the files used to create the message.
		#
		my_debug("cleaning up.");
		unlink @files;
	    }
	}
    }

    my_log("$message_count messages were sent.");
}

######################################################################
# Update the database with new heads
#
my %new_notifications =
    map { my $rev = $_;
	  map { my $key = "$rev\@$_"; $key => 1 }
	      keys %{$current_leaves{$rev}} }
	keys %current_leaves;
my %old_notifications =
    map { my $rev = $_;
	  map { my $key = "$rev\@$_"; $key => 1 }
	      keys %{$old_leaves{$rev}} }
	keys %old_leaves;

my_log("updating the table of last logged revisions.");

map { my_conditional_system($update,
			    "$monotone$database set notify $_ 1") }
    grep { !defined $old_notifications{$_} && $_ !~ /\@\*$/ }
	keys %new_notifications;
map { s/\@\*$//;
      my_conditional_system($update,
			    "$monotone$database unset notify $_") }
    grep { !defined $new_notifications{$_} }
	keys %old_notifications;

######################################################################
# Clean up.
#
my_exit();

######################################################################
# Subroutines
#

# generate_diff does just that, including for the case where there is
# no ancestor.  For that latter case, we need to synthesise the diff,
# since monotone doesn't know how to do that
sub generate_diff
{
    my ($db, $ancestor, $revision, $filespec, $really_show_diffs,
	$decorate_p, @dummy) = @_;

    open OUTPUT, $filespec
	|| my_error("Couldn't write to $filespec: $!");
    if ($really_show_diffs && $decorate_p) {
	print OUTPUT "-" x 70, "\n";
	print OUTPUT "Diff [$ancestor] -> [$revision]\n";
    }
    if ($ancestor eq "") {
	if (!$really_show_diffs) {
	    print OUTPUT "This is the first commit, and there's no easy way to create a diff\n";
	    print OUTPUT "These are the commands to view the individual files of that commit instead:\n";
	    print OUTPUT "\n";
	}
	my @status = my_backtick("$monotone$db automate get_revision $revision");
	my $line;
	$line = shift @status;
	if ($line =~ /^format_version\s+"([0-9]+)"\s*$/) {
	    # New versioned format
	    my $format_version = $1;
	    if ($format_version == 1) {
		while($line =~ /^(\s*
				  |format_version\s+"[0-9]+"
				  |new_manifest\s+\[[0-9a-f]{40}\]
				  |old_revision\s+\[[0-9a-f]{40}\]
				  )\s*$/x) {
		    $line = shift @status;
		}
	    }
	} else {
	    while($line =~ /^(\s*
			      |(new|old)_manifest\s+\[[0-9a-f]{40}\]
			      |old_revision\s+\[[0-9a-f]{40}\]
			      )\s*$/x) {
		$line = shift @status;
	    }
	}
	foreach (@status) {
	    chomp;
	    print OUTPUT ($_ eq "" ? "#" : "# $_"), "\n";
	}
	my $added_file = "";
	foreach my $line (@status) {
	    my $id = undef;
	    $added_file = $1 if $line =~ /^add_file\s+"(.*)"\s*$/;
	    $id = $1 if $line =~ /^\s+content\s\[([0-9a-fA-F]{40})\]\s*$/;
	    # older format had the add_file just name the file, and having
	    # the content IDs come much later, preceded by a patch line
	    $added_file = $1 if $line =~ /^patch\s+"(.*)"\s*$/;
	    $id = $1 if $line =~ /^\s+to\s\[([0-9a-fA-F]{40})\]\s*$/;
	    if (defined $id) {
		if ($really_show_diffs) {
		    my @file = my_backtick("$monotone$db automate get_file $id");
		    print OUTPUT "--- $added_file\n";
		    print OUTPUT "+++ $added_file\n";
		    print OUTPUT "\@\@ -0,0 +1,",list_size(@file)," \@\@\n";
		    map { print OUTPUT "+" . $_ } @file;
		} else {
		    print OUTPUT "$monotone --db={your.database} automate get_file $id\n";
		}
	    }
	}
    } else {
	if ($really_show_diffs) {
	    print OUTPUT my_backtick("$monotone$db diff --revision=$ancestor --revision=$revision");
	} else {
	    print OUTPUT "mtn --db={your.database} diff --revision=$ancestor --revision=$revision\n";
	}
    }
    close OUTPUT;
}

# revision_is_in_branch checks if the given revision is in one of the
# given branches.  The latter is given in form of a hash.
sub revision_is_in_branch
{
    my ($revision, $branches, $revision_data) = @_;
    my $bool = 0;

    my_debug("Checking if $revision has already been shown in one of
    these branches:\n  ",
	     join("\n  ", keys %$branches));

    if (!defined $$revision_data{$revision}) {
	$$revision_data{$revision} =
	    [ map { chomp; $_ }
	      my_backtick("$monotone$database log --no-graph --last=1 --from=$revision") ];
    }

    map {
	my $branch = (split ' ')[1];
	if (defined $$branches{$branch}) {
	    $bool = 1;
	    my_debug("Found it in $branch");
	}
    } grep /^Branch:/, @{$$revision_data{$revision}};

    my_debug("Didn't find it in any of the branches...") if !$bool;

    return $bool;
}

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
sub my_errlog
{
    if ($#_ >= 0) {
	print STDERR "Notify: ", join("\nNotify: ",
				     split("\n",
					   join('', @_))), "\n";
    }
}

# my_error will output all it's arguments, prefixed with "Notify: ", then die.
sub my_error
{
    my $save_syserr = "$!";
    if ($#_ >= 0) {
	print STDERR "Notify: ", join("\nNotify: ",
				     split("\n",
					   join('', @_))), "\n";
    }
    die "$save_syserr";
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

    my_debug("'${command}'\n");
    my $return = system($command);
    my $exit = $? >> 8;
    die "'${command}' returned with exit code $exit\n" if ($exit);
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

    my_debug("'${command}'\n");
    if ($condition) {
	$return = system($command);
	my $exit = $? >> 8;
	die "'${command}' returned with exit code $exit\n" if ($exit);
    } else {
	my_debug("... not actually executed.\n");
    }
    return $return;
}

# my_exit removes temporary files and then exits.
sub my_exit
{
    my_log("cleaning up.");
    unlink @files_to_clean_up;
    rmdir $workdir if $remove_workdir;
    my_log("all done.");
    exit(0);
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
	die "'${command}' returned with exit code $exit\n";
    }
    return @return;
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

monotone-notify.pl - a script to send monotone change notifications by email

=head1 SYNOPSIS

monotone-notify.pl [--help] [--man]
[--db=database] [--root=path] [--branch=branch ...]
[--[no]update] [--[no]mail] [--[no]attachments] [--[no]ignore-merges]
[--from=email-sender]
[--difflogs-to=email-recipient] [--nodifflogs-to=email-recipient]
[--workdir=path] [--before=yyyy-mm-ddThh:mm:ss] [--since=yyyy-mm-ddThh:mm:ss]
[--quiet] [--debug] [--monotone=path]

=head1 DESCRIPTION

B<monotone-notify.pl> is used to generate emails containing monotone
change logs for recent changes.  It uses monotone database variables
in the domain 'notify' to keep track of the latest revisions already
logged.

=head1 OPTIONS

=over 4

=item B<--help>

Print a brief help message and exit.

=item B<--man>

Print the manual page and exit.

=item B<--db>=I<database>

Sets which database to use.  If not given, the database given in
_MTN/options is used.

=item B<--root>=I<path>

Stop the search for a working copy (containing the F<_MTN> directory) at
the specified root directory rather than at the physical root of the
filesystem.

=item B<--branch>=I<branch>

Sets a branch that should be checked.  Can be used multiple times to
set several branches.  If not given at all, all available branches are
used.

=item B<--update>

Has B<monotone-notify.pl> update the database variables at the end of
the run.  This is the default unless B<--debug> is given.

=item B<--noupdate>

The inverse of B<--update>.  This is the default when B<--debug> is
given.

=item B<--mail>

Has B<monotone-notify.pl> send the constructed logs as emails.  This
is the default unless B<--debug> is given.

=item B<--nomail>

The inverse of B<--mail>.  This is the default when B<--debug> is
given.

=item B<--attachments>

Add the change summary and the output of 'monotone diff' as
attachments in the emails.  This is the default behavior.

=item B<--noattachments>

Have the change summary and the output of 'monotone diff' in the body
of the email, separated by lines of dashes.

=item B<--ignore-merges>

Do not create difflogs for merges (revisions with more than one
ancestor), if the ancestors are in at least one of the branches that
are monitored.  This is the default behavior.

=item B<--noignore-merges>

Always create difflogs, even for merges.

=item B<--from>=I<from>

Sets the sender address to be used when creating the emails.  There is
no default, so this is a required option.

=item B<--difflogs-to>=I<to>

Sets the recipient address to be used when creating the emails with
logs containing diffs.  This or B<--nodifflogs-to> MUST be used, and
both can be given at the same time (thereby generating two emails for
each log).

=item B<--nodifflogs-to>=I<to>

Sets the recipient address to be used when creating the emails with
logs without diffs.  This or B<--difflogs-to> MUST be used, and both
can be given at the same time (thereby generating two emails for each
log).

=item B<--before>=I<yyyy-mm-ddThh:mm:ss>

Only log revisions where the datetime is less than the one given.

=item B<--since>=I<yyyy-mm-ddThh:mm:ss>

Only log revisions where the datetime is greater or equal to than the
one given.

=item B<--workdir>=I<path>

Sets the working directory to use for temporary files.  This working
directory should be empty to avoid having files overwritten.  When
B<--debug> is used and unless B<--mail> is given, one or both of the
two files F<Notify.debug-diffs> and F<Notify.debug-nodiffs> will be
left in the work directory.

The default working directory is F</var/tmp/monotone_notify.work.$$>,
and will be removed automatically unless F<Notify.debug-diffs> or
F<Notify.debug-nodiffs> are left in it.

=item B<--debug>

Makes B<monotone-notify.pl> go to debug mode.  It means a LOT of extra
output, and also implies B<--noupdate> and B<--nomail> unless
specified differently on the command line.

=item B<--quiet>

Makes B<monotone-notify.pl> really silent.  It will normally produce a
small log of it's activities, but with B<--quiet>, it will only output
error messages.  If B<--debug> was given, B<--quiet> is turned off
unconditionally.

=item B<--monotone>=I<path>

Gives the name or path to mtn(1) or both.  The default is simply
F<mtn>.

=back

=head1 NOTES

You might notice that a series of logs for some branch may span over
other branches.  This is because some development may actually go
through those other branches by virtue of 'monotone propagate' and
other means to move changes from one branch to another.

This behavior isn't entirely deterministic, as it depends on when the
last run of B<monotone-notify.pl> was done, and what head revisions
were active at that time.  It might be seen as a bug, but if
corrected, it might miss out on development that moves entirely to
another branch and moves back later in time, thereby creating a hole
in the branch currently looked at.  This has actually happened in the
development of monotone itself.

For now, it's assumed that a little too much information is better
than (unjust) lack of information.

=head1 BUGS

Fewer than before.

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
