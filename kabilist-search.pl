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
	#my @lin = split(":");

	#print $lin[0], "\n";
	#print Dumper $lin[1];

	#if (!defined $lin[0]) {
	#	next;
	#}

	#$lin[0] =~ s/^\s+//;;

	#if ($lin[0] eq $srchstr) {
	if (index($_, $srchstr) != -1) {
		print "$_\n";
	}
}
close (FILE);
