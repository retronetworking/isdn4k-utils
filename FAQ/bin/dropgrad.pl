#!/usr/bin/perl
#
# $Id$

($infile, $outfile) = @ARGV;

$infile = "de-i4l-faq" until $inputfile;
$outfile = "$infile.out" until $outfile;

open(IN,"<$infile") || die "cannot open $infile";
open(OUT,">$outfile") || die "cannot open $outfile"; 

while(<IN>) {

    if (/^\°/) {
        print OUT "$'";
    }
    else {
       print OUT "$_";
    }
}
