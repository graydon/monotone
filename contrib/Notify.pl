#! /usr/bin/perl

use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;
use MIME::Lite;

my $user_database = undef;
my @user_branches = ();
my $help = 0;
my $man = 0;
my $mail = -1;
my $debug = 0;
my $update = -1;
my $from = undef;
my $to = undef;
my $attachments = 1;
my $workdir = undef;

GetOptions('help|?' => \$help,
	   'man' => \$man,
	   'mail!' => \$mail,
	   'from=s' => \$from,
	   'to=s' => \$to,
	   'attachments!' => \$attachments,
	   'debug' => \$debug,
	   'update!' => \$update,
	   'db=s' => \$user_database,
	   'branch=s' => \@user_branches,
	   'workdir=s' => \$workdir) or pod2usage(2);
pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;

$update = 0 if ($debug && $update == -1);
$mail = 0 if ($debug && $mail == -1);

if (!defined $from) {
    print STDERR "You need to specify a From address with --from\n";
    pod2usage(2);
}
if (!defined $to) {
    print STDERR "You need to specify a To address with --to\n";
    pod2usage(2);
}

my $database = "";
if (defined $user_database) {
    $database = " --db=$user_database";
}

my $message_file = "message";

my $remove_workdir = 0;
if (!defined $workdir) {
    $workdir = "/var/tmp/monotone_motify.work.$$";
    mkdir $workdir;
    $remove_workdir = 1;
}
chdir $workdir;

# Format:
# datetime1 => {
#	revision1 => 1,
#	revision2 => 1,
#	...
# }
my %timeline = ();

# Format:
# revision1 => {
#	Ancestors => [ revision11, revision12, ... ]
#	Authors => [ author11, author12, ... ]
#	Date => yyyy-mm-ddThh:mm:ss
#	Branches => [ branch11, branch12, ... ]
#	Tags => [ tag11, tag12, ... ]
#	ChangeLog => "..."
# }
my %revision_data = ();

my %old_heads = ();
my %stop_revisions = ();
my %current_heads = ();

my %cert_types = ( Revision => 's',
		   Date => 's',
		   ChangeLog => 'S',
		   '*' => 'l' );

{
    my $branch;
    my %branches = ();

    print STDERR "Notify: finding the branches.\n";
    print STDERR "DEBUG: 'monotone$database list branches'\n" if ($debug);
    open BRANCHES,"monotone$database list branches|"
	|| die "Couldn't get the branches for database $database: $!\n";
    while(<BRANCHES>) {
	chomp;
	$branches{$_} = 1;
    }
    close BRANCHES;

    if (@user_branches) {
	my %restricted_branches = ();
	map {
	    if (defined $branches{$_}) {
		$restricted_branches{$_} = 1;
	    } else {
		print STDERR "Branch $_ doesn't exist.  Skipping...\n";
	    }
	} @user_branches;
	%branches = %restricted_branches;
    }

    print STDERR "Notify: finding the heads of each branch.\n";
    foreach $branch (keys %branches) {
	print STDERR "DEBUG: 'monotone$database automate heads $branch'\n"
	    if $debug;
	open HEADS,"monotone$database automate heads $branch|"
	    || die "Couldn't get the heads for branch $branch: $!\n";
	while(<HEADS>) {
	    chomp;
	    print STDERR "DEBUG: heads => '$_'\n" if $debug;
	    $current_heads{$_} = 1;
	}
	close HEADS;
    }
}

print STDERR "Notify: finding out where we were last time.\n";
print STDERR "DEBUG: 'monotone$database list vars notify'\n" if $debug;
open DBVARS,"monotone$database list vars notify|"
    || die "Couldn't get the notify variables: $!\n";
my $dbvars_c = 0;
while(<DBVARS>) {
    chomp;
    my ($domain, $revision, @dummy) = split ' ';
    $stop_revisions{$revision} = 1;
    $old_heads{$revision} = 1;
    $dbvars_c++;
}
close DBVARS;
if ($dbvars_c > 0) {
    print STDERR "Notify: ... found $dbvars_c heads in total.\n";
} else {
    print STDERR "Notify: ... none!  We've never been here before!\n";
}

foreach my $head (keys %current_heads) {
    print STDERR "Notify: reading log for revision $head.  This may take some time.\n";
    print STDERR "DEBUG: 'monotone$database log $head'\n" if $debug;
    open LOG,"monotone$database log $head|"
	|| die "Couldn't get the log for head $head: $!\n";

    my %info = ();
    my $current_cert_name = undef;
    my %to_be_checked = ($head => 1);

    my $line = "";
    while(defined $line) {
	$line = <LOG>;
	if (defined $line) {
	    chomp $line;
	    print STDERR "DEBUG: log => '$line'\n" if $debug;
	} else {
	    print STDERR "DEBUG: log => EOF\n" if $debug;
	}

	if ((!defined $line || $line =~ /^-+$/) && defined $info{Revision}) {
	    my $revision = ${$info{Revision}}[0];
	    print STDERR "DEBUG: Organising data for revision $revision\n"
		if $debug;
	    foreach my $cert (keys %info) {
		my $data = $info{$cert};
		if (defined $cert_types{$cert}) {
		    if ($cert_types{$cert} eq 's') {
			$revision_data{$revision}->{$cert} =
			    join(' ',@{$data});
		    } elsif ($cert_types{$cert} eq 'S') {
			$revision_data{$revision}->{$cert} =
			    join("\n",@{$data})."\n";
		    }
		    print STDERR "DEBUG: Revision data $cert = '$revision_data{$revision}->{$cert}'\n"
			if $debug;
		} else {
		    $revision_data{$revision}->{$cert} = $data;
		    map {
			print STDERR "DEBUG: Revision data $cert = '$_'\n"
			    if $debug;
		    } @{$revision_data{$revision}->{$cert}};
		}
	    }

	    if (!defined $timeline{$revision_data{$revision}->{Date}}) {
		$timeline{$revision_data{$revision}->{Date}} = {}
	    }
	    $timeline{$revision_data{$revision}->{Date}}->{$revision} = 1;

	    if (defined $to_be_checked{$revision}) {
		delete $to_be_checked{$revision};
		foreach $revision (@{$revision_data{$revision}->{Ancestor}}) {
		    if (!defined $stop_revisions{$revision}) {
			$to_be_checked{$revision} = 1;
		    }
		}
	    }
	    %info = ();
	    $current_cert_name = undef;
	    next;
	}

	if (defined $line) {
	    if (!defined $current_cert_name
		|| $current_cert_name ne "ChangeLog") {
		$line =~ s/^\s*//g;
		$line =~ s/\s*$//g;
		if ($line =~ /^([A-Za-z ]+): *(.+)$/) {
		    if (defined $info{$1}) {
			print STDERR "DEBUG: $1 += '$2'\n" if $debug;
		    } else {
			print STDERR "DEBUG: $1 = '$2'\n" if $debug;
		    }
		    push @{$info{$1}}, $2;
		    $current_cert_name = $1;
		    next;
		}

		if ($line =~ /^([A-Za-z\s]+):\s*$/) {
		    print STDERR "DEBUG: Start on $1\n" if $debug;
		    $current_cert_name = $1;
		    next;
		}

		next if $line =~ /^\s*$/;
	    }

	    if (defined $current_cert_name) {
		if (defined $info{$current_cert_name}) {
		    print STDERR "DEBUG: $current_cert_name += '$line'\n" if $debug;
		} else {
		    print STDERR "DEBUG: $current_cert_name = '$line'\n" if $debug;
		}
		push @{$info{$current_cert_name}}, $line;
	    }
	}
	last if !keys %to_be_checked;
    }
    close LOG;
}

print STDERR "Notify: generating log messages.  This may also take a while.\n";

# Output:
#
# ----------------------------------------------------------------------
# Branch:	{branch name}
# Revision:	{revision}
# Name:		{author name}
# Date:		{revision date}
# [Tag:		{tag}]
#
# Added files:
#		{file list}
# Modified files:
#		{file list}
#
# ChangeLog:
#   {changelog}
#
# ----------------------------------------------------------------------
# {diff}
# ----------------------------------------------------------------------

foreach my $date (sort keys %timeline) {
    print STDERR "DEBUG: date $date\n" if $debug;
    foreach my $revision (keys %{$timeline{$date}}) {
	print STDERR "DEBUG: revision $revision\n" if $debug;

	my %message_files = ( "$message_file.0.txt"
			      => { Title => 'change summary',
				   Disposition => 'inline' } );

	open MESSAGE,">$message_file.0.txt"
	    || die "Couldn't open $message_file.0.txt to write: $!\n";
	foreach my $cert_name (sort( grep !/(files|directories|^ChangeLog)$/,
				     keys %{$revision_data{$revision}} )) {
	    my $title = $cert_name;
	    my $value = undef;
	    my $cert_type = 'l';
	    if (defined $cert_types{$cert_name}) {
		$cert_type = $cert_types{$cert_name};
	    }
	    if ($cert_type eq 'l') {
		if ($#{$revision_data{$revision}->{$cert_name}} > 0) {
		    if ($cert_name eq "Branch") {
			$title .= "es";
		    } else {
			$title .= "s";
		    }
		    $value =
			"\n        "
			. join("\n        ",
			       @{$revision_data{$revision}->{$cert_name}});
		} elsif (defined $revision_data{$revision}->{$cert_name}->[0]) {
		    $value = ' ' x (19 - length($title))
			. $revision_data{$revision}->{$cert_name}->[0];
		} else {
		    $value = ' ' x (19 - length($title)) . "... none ...";
		}
	    } else {
		$value = ' ' x (19 - length($title))
		    . $revision_data{$revision}->{$cert_name};
	    }
	    print MESSAGE "$title:$value\n";
	}
	print MESSAGE "\n";

	foreach my $cert_name (sort( grep /^Deleted\s/,
				     keys %{$revision_data{$revision}} )) {
	    my $title = $cert_name;
	    my $value =
		"\n        "
		. join("\n        ",
		       @{$revision_data{$revision}->{$cert_name}});
	    print MESSAGE "$title:$value\n";
	}
	foreach my $cert_name (sort( grep /^Renamed\s/,
				     keys %{$revision_data{$revision}} )) {
	    my $title = $cert_name;
	    my $value =
		"\n        "
		. join("\n        ",
		       @{$revision_data{$revision}->{$cert_name}});
	    print MESSAGE "$title:$value\n";
	}
	foreach my $cert_name ("Added files", "Modified files") {
	    next if !defined $revision_data{$revision}->{$cert_name};
	    my $title = $cert_name;
	    my $value =
		"\n        "
		. join("\n        ",
		       @{$revision_data{$revision}->{$title}});
	    print MESSAGE "$title:$value\n";
	}
	print MESSAGE "\n";

	{
	    my $title = "ChangeLog";
	    my $value = "\n" . $revision_data{$revision}->{$title} . "\n";
	    print MESSAGE "$title:$value\n";
	}

	if ($#{$revision_data{$revision}->{Ancestor}} >= 0) {
	    my $count = 0;
	    foreach my $ancestor (@{$revision_data{$revision}->{Ancestor}}) {
		$count++;

		if ($attachments) {
		    close MESSAGE;
		    open MESSAGE,">$message_file.$count.txt"
			|| die "Couldn't open $message_file.$count.txt to write: $!\n";
		    $message_files{"$message_file.$count.txt"} = {
			Title => "Diff [$ancestor] -> [$revision]",
			Disposition => 'attachment' };
		} else {
		    print MESSAGE "----------------------------------------------------------------------\n";
		    print MESSAGE "Diff [$ancestor] -> [$revision]\n";
		}

		print STDERR "DEBUG: 'monotone$database diff --revision=$ancestor --revision=$revision'\n" if $debug;
		print MESSAGE `monotone$database diff --revision=$ancestor --revision=$revision`;
	    }
	} else {
	    if ($attachments) {
		close MESSAGE;
		open MESSAGE,">$message_file.1.txt"
		    || die "Couldn't open $message_file.1.txt to write: $!\n";
		$message_files{"$message_file.1.txt"} = {
		    Title => "Diff [] -> [$revision]",
		    Disposition => 'attachment' };
	    } else {
		print MESSAGE "----------------------------------------------------------------------\n";
		print MESSAGE "Diff [] -> [$revision]\n";
	    }

	    print STDERR "DEBUG: 'monotone$database cat revision $revision'\n" if $debug;
	    my @status = `monotone$database cat revision $revision`;
	    shift @status;	# remove "new_manifest ..."
	    shift @status;	# remove ""
	    shift @status;	# remove "old_revision ..."
	    shift @status;	# remove "old_manifest ..."
	    map {
		if ($_ eq "") {
		    print MESSAGE "#\n";
		} else {
		    print MESSAGE "# $_";
		}
	    } @status;
	    my $patched_file = "";
	    foreach my $line (@status) {
		if ($line =~ /^patch\s+"(.*)"\s*$/) {
		    $patched_file = $1;
		} elsif ($line =~ /^\s+to \[([0-9a-fA-F]{40})\]\s*$/) {
		    my $id = $1;
		    print STDERR "DEBUG: 'monotone$database cat file $id'\n"
			if $debug;
		    my @file = `monotone$database cat file $id`;
		    my $lines = $#file + 1;
		    print MESSAGE "--- $patched_file\n";
		    print MESSAGE "+++ $patched_file\n";
		    print MESSAGE "@@ -0,0 +1,$lines @@\n";
		    map { print MESSAGE "+" . $_ } @file;
		}
	    }
	}
	close MESSAGE;

	my $message_type; 
	my $msg;

	if ($attachments) {
	    $msg = MIME::Lite->new(From => $from,
				   To => $to,
				   Subject => "Revision $revision",
				   Type => 'multipart/mixed');
	    foreach my $filename (sort keys %message_files) {
		$msg->attach(Type => 'TEXT',
			     Path => $filename,
			     Disposition
			     => $message_files{$filename}->{Disposition},
			     Encoding => '8bit');
	    }
	    # MIME:Lite has some quircks that we need to deal with
	    foreach my $part ($msg->parts()) {
		my $filename = $part->filename();
		# Hacks to avoid having file names, and to added a
		# description field
		$part->attr("content-disposition.filename" => undef);
		$part->attr("content-type.name" => undef);
		$part->attr("content-description"
			    => $message_files{$filename}->{Title});
	    }
	} else {
	    $msg = MIME::Lite->new(From => $from,
				   To => $to,
				   Subject => "Revision $revision",
				   Type => 'TEXT',
				   Path => "$message_file.0.txt",
				   Encoding => '8bit');
	    # Hacks to avoid having file names
	    $msg->attr("content-disposition.filename" => undef);
	    $msg->attr("content-type.name" => undef);
	}
	if ($mail) {
	    $msg->send();
	} elsif ($debug) {
	    open MESSAGEDBG,">>Notify.debug"
		|| die "Couldn't open Notify.debug for append: $!\n";
	    print MESSAGEDBG "======================================================================\n";
	    $msg->print(\*MESSAGEDBG);
	    print MESSAGEDBG "======================================================================\n";
	    close MESSAGEDBG;
	}

	map { unlink $_; } keys %message_files;
    }
}

print STDERR "Notify: remembering where we are now.\n";
foreach my $head (keys %current_heads) {
    if (!defined $old_heads{$head}) {
	print STDERR "DEBUG: 'monotone$database set notify $head 1'\n"
	    if ($debug);
	system("monotone$database set notify $head 1\n") if $update;
    }
}

print STDERR "Notify: cleaning up.\n";
foreach my $head (keys %old_heads) {
    if (!defined $current_heads{$head}) {
	print STDERR "DEBUG: 'monotone$database unset notify $head'\n"
	    if ($debug);
	system("monotone$database unset notify $head\n") if $update;
    }
}

chdir "/";
rmdir $workdir if $remove_workdir;

print STDERR "Notify: done.\n";


__END__

=head1 NAME

Notify.pl - a script to send monotone change notifications by email

=head1 SYNOPSIS

Notify.pl [--help] [--man] [--db=database] [--branch=branch ...]
[--workdir=path] [--from=email-sender] [--to=email-recipient]
[--debug] [--[no]update] [--[no]mail] [--[no]attachments]

=head1 DESCRIPTION

B<Notify.pl> is used to generate emails containing monotone change logs for
recent changes.  It uses monotone database variables in the domain 'notify'
to keep track of the latest revisions already logged.

=head1 OPTIONS

=over 4

=item B<--help>

Print a brief help message and exit.

=item B<--man>

Print the manual page and exit.

=item B<--db>=I<database>

Sets which database to use.  If not given, the database given in MT/options
is used.

=item B<--branch>=I<branch>

Sets a branch that should be checked.  Can be used multiple times to set
several branches.  If not given at all, all available branches are used.

=item B<--workdir>=I<path>

Sets the working directory to use for temporary files.  This working
directory should be empty to avoid having files overwritten.  When
B<--debug> is used and unless B<--mail> is given, there will be a file
called C<Notify.debug> left in the work directory.

The default working directory is F</var/tmp/monotone_motify.work.$$>,
and will be removed automatically unless Notify.debug is left in it.

=item B<--from>=I<from>

Sets the sender address to be used when creating the emails.  There is
no default, so this is a required option.

=item B<--to>=I<to>

Sets the recipient address to be used when creating the emails.  There
is no default, so this is a required option.

=item B<--debug>

Makes Notify.pl go to debug mode.  It means a LOT of extra output, and
also implies B<--noupdate> and B<--nomail> unless specified differently
on the command line.

=item B<--update>

Has Notify.pl update the database variables at the end of the run.  This
is the default unless B<--debug> is given.

=item B<--noupdate>

The inverse of B<--update>.  This is the default when B<--debug> is given.

=item B<--mail>

Has Notify.pl send the constructed logs as emails.  This is the default
unless B<--debug> is given.

=item B<--nomail>

The inverse of B<--mail>.  This is the default when B<--debug> is given.

=item B<--attachments>

Add the change summary and the output of 'monotone diff' as attachments
in the emails.  This is the default behavior.

=item B<--noattachments>

Have the change summary and the output of 'monotone diff' in the body of
the email, separated by lines of dashes.

=back

=head1 BUGS

Tons, I'm sure...

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
