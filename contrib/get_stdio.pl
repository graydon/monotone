#!/usr/bin/perl
#Timothy Brownawell

#get_stdio.pl filename n
#Get the output from command n as one piece
#filename holds the output from "monotone automate stdio"

$x="";
open $file, $ARGV[0];
while(<$file>)
{
	$x = $x . $_;
}

# packet is cmdnum:errcode:(last|more):size:contents
# note that contents may contain newlines (or may even be binary)
# this does:

# for i in split_into_packets($x)
#     $results[i.cmdnum] += i.contents
#
# print $results[n]

# read a packet from $x (the contents of the file)
while($x =~ /(\d+)\:\d+\:[lm]\:(\d+)\:/s)
{# $1, $2, etc (what was matched by the parentheses) are dynamically scoped
	$m=int($2);
	my $n="";
	$x = $'; #' # what's left after the end of the match
	for(;$m > 0; $m--) # read $m bytes, leaving $x as what's left
	{
		$x =~ /(.)/s;
		$n = $n . $1;
		$x = $'; #' # gedit syntax coloring gets messed up on $' .
	}
	$results[$1] = $results[$1] . $n; # the $1 from the while
}
print $results[$ARGV[1]];
