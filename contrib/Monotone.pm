# This is a simple Perl module to start a monotone automate sub-process and then pass commands to it.
# Written by Will Uther, but I'm not a PERL hacker and I'm hoping someone will come along and fix it
# to make it right.

package Monotone;

use warnings;
use strict;
use FileHandle;
use IPC::Open2;

require Exporter;
our @ISA = qw(Exporter);
our %EXPORT_TAGS = ( 'all' => [ qw() ] );
our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );
our @EXPORT = qw( );
 
our $VERSION = '0.01';

#constructor
sub new {
    my $class = shift;
    my $self = {
        In => undef,
        Out  => undef,
        PID => undef,
        CmdNum => undef,
    };
    bless ($self, $class);
    return $self;
}

sub open ($$) {
    my ( $self, $db, $workspace ) = @_;
    local (*READ, *WRITE);
    if (defined($db) && defined($workspace)) {
        $self->{PID} = open2(\*READ, \*WRITE, "mtn --db=$db --root=$workspace automate stdio" );
    } elsif (defined($workspace)) {
        $self->{PID} = open2(\*READ, \*WRITE, "mtn --root=$workspace automate stdio" );
    } else {
        $self->{PID} = open2(\*READ, \*WRITE, "mtn automate stdio" );
    }
    $self->{In} = *READ;
    $self->{Out} = *WRITE;
    $self->{CmdNum} = 0;
}

sub call {
    my $self = shift;
    
    return if !defined($self->{PID});
    
    my $read = $self->{In};
    my $write = $self->{Out};
        
    print $write "l";
    
    my $arg;    
    while (defined($arg = shift)) {
        my $arglen = length $arg;
        # print "Arg: " . $arg . " with len: " . $arglen . "\n";
        print $write $arglen;
        print $write ":";
        print $write $arg;
    }
    print $write "e";
    my $count=0;
    my @ret = ("", "");
    my $last;
    
    do {
        my $numString = "";
        my $ch;
        while (($ch = getc($read)) ne ':') {
            $numString = $numString . $ch;
        }
        die("Got wrong command number from monotone: ". $numString . ".") if ($numString != $self->{CmdNum});
        my $err = getc($read);
        die("Parser confused.") if ($err ne '0' && $err ne '1' && $err ne '2');
        die("Parser confused.") if (getc($read) ne ':');
        $last = getc($read);
        die("Parser confused.") if ($last ne 'l' && $last ne 'm');
        die("Parser confused.") if (getc($read) ne ':');
        $numString = "";
        while (($ch = getc($read)) ne ':') {
            $numString = $numString . $ch;
        }
        my $input = "";
        while ($numString > 0) {
            $input = $input . getc($read);
            $numString--;
        }
        # print "Got input: " . $input;
        if ($err eq '1') {
            die("Syntax error in Monotone stdio");
        } elsif ($err eq '2') {
            $ret[1] = $ret[1] . $input;
        } elsif ($err eq '0') {
            $ret[0] = $ret[0] . $input;
        }
    } while ($last eq 'm');
    
    die("Parser confused.") if ($last ne 'l');
    
    $self->{CmdNum} += 1;
    return @ret;
}

sub close {
    my $self = shift;
    
    close $self->{Out} if defined($self->{Out});
    $self->{Out} = undef;
    close $self->{In} if defined($self->{In});
    $self->{In} = undef;
    waitpid($self->{PID}, 0);
    $self->{PID} = undef;
}

# print "starting tests\n";
# 
# my $test = Monotone->new();
# $test->open("/Users/willu/src/monotone/mt.db","/Users/willu/src/monotone/monotone-source");
# 
# my @revs = $test->call("get_base_revision_id");
# print "got revisions: " . $revs[0] . "\n";
# 
# my $rev = $revs[0];
# chomp $rev; # remove the trailing \n that monotone leaves there.
# 
# my @certs = $test->call("certs", $rev);
# my $cert = $certs[0];
# 
# print "Got certs:\n" . $cert . "\n";
# 
# $test->close();
# 
# print "done\n";
