#! /usr/bin/perl

use warnings;
use strict;
use File::Basename;
use File::Spec;

my $here = dirname($0);
my $there = File::Spec->catdir($here,File::Spec->updir());
my $monotone_texi = File::Spec->catfile($there, "monotone.texi");
my $ext_hooks_lua_in = File::Spec->catfile($here, "ext_hooks.lua.in");
my $ext_hooks_lua = File::Spec->catfile($here, "ext_hooks.lua");

# Format:  $name => $arglist
my %hooks = ();

open INPUT, $monotone_texi ||
    die "Couldn't open $monotone_texi for reading: $!\n";

# First, read until we find the Hooks section
while(<INPUT>) {
    chomp;
    last if (/^\s*\@section\s+Hooks\s*$/);
}

# Now, parse all the hooks docs until we find the Additional Lua Functions
# section
while(<INPUT>) {
    chomp;
    last if (/^\s*\@section\s+Additional\s+Lua\s+Functions\s*$/);
    if (/^\s*\@item\s+(\w+)\s*\((\@var\{(\w+|\.\.\.)\}(,\s*\@var\{(\w+|\.\.\.)\})*)?\)\s*$/) {
	my $name = $1;
	my $arglist = "";
	if (defined $2) {
	    $arglist = join(", ",
			    map {
				my $x = $_;
				$x =~ s/^\s*\@var\{(\w+|\.\.\.)\}\s*$/$1/;
				$x;
			    } split(/,\s*/, $2));
	}
	$hooks{$name} = $arglist;
    }
}

close INPUT;

foreach my $n (sort keys %hooks) {
    print $n," => \"",$hooks{$n},"\"\n";
}

open INPUT, $ext_hooks_lua_in ||
    die "Couldn't open $ext_hooks_lua_in for reading: $!\n";
open OUTPUT, ">$ext_hooks_lua" ||
    die "Couldn't open $ext_hooks_lua for writing: $!\n";

# Format : $name => [ list of strings ]
my %expansions = ( HOOKS_LIST => [],
		   HOOKS_FUNCTIONS => []);

foreach my $n (sort keys %hooks) {
    push(@{$expansions{HOOKS_LIST}}, $n);
    push(@{$expansions{HOOKS_FUNCTIONS}},
"function $n ($hooks{$n})
   local i
   local fn
   res_$n = nil
   for i,fn in pairs(mtn_luahooks[\"$n\"]) do
      res = fn($hooks{$n})
      if res ~= nil then res_$n = res end
   end
   return res_$n
end"
	 );
}

while(<INPUT>) {
    chomp;
    if (/^(.*)\@\@(\w+)\@\@(.*)$/) {
	my $prefix = $1;
	my $listname = $2;
	my $postfix = $3;
	if (defined $expansions{$listname}) {
	    foreach my $item (@{$expansions{$listname}}) {
		print OUTPUT $prefix,$item,$postfix,"\n";
	    }
	} else {
	    print OUTPUT $prefix,$listname,$postfix,"\n";
	}
    } else {
	print OUTPUT $_,"\n";
    }
}

close OUTPUT;
close INPUT;
