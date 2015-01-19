#!/usr/bin/perl

use warnings;
use strict;
use Data::Dumper;
$Data::Dumper::Indent = 1;
$Data::Dumper::Sortkeys = 1;

my $filnam = $ARGV[0];
my $srchstr = $ARGV[1];
print "$filnam\n";
print "$srchstr\n";

open (FILE, "<$filnam");
while (<FILE>) {
	chomp;
	if (index($_, $srchstr) != -1) {
		print "$_\n";
	}
}
close (FILE);
