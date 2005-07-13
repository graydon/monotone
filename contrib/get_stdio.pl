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
while($x =~ /(\d+)\:\d+\:[lm]\:(\d+)\:/s)
{
	$m=int($2);
	my $n="";
	$x = $';#'
	for(;$m > 0; $m--)
	{
		$x =~ /(.)/s;
		$n = $n . $1;
		$x = $';#'
	}
	$results[$1] = $results[$1] . $n;
}
print $results[$ARGV[1]];
