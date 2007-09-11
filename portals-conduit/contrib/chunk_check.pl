#!/usr/bin/perl

package main;

use Getopt::Std;
use IO::File;
use File::Basename;

getopts("dr:s:");

my $debug = 0;
$debug = 1 if (defined($opt_d));

$req = {
    name => "ReqSB",
    nchunk => 0,
    nbytes => 0,
    chunksz =>0,
    numalloc => 0,
    numfree => 0,
    len => 0,
    max_in_use => 0,
    alloc => {},
};
$rpl = {
    name => "RplSB",
    nchunk => 0,
    nbytes => 0,
    chunksz =>0,
    numalloc => 0,
    numfree => 0,
    inuse => 0,
    max_in_use => 0,
    alloc => {},
};
	
my $lineno = 0;
while (<>) {
    if (/CHUNK_INIT:\s+(\S+)\s+nchunks=(\d+),\s+nbytes=(\d+)/) {
	my $buffer = $1;
	my $nchunk = $2;
	my $nbytes = $3;
	my $allocator;
	if ($buffer eq $req->{name}) {
	    $allocator = $req;
	} elsif ($buffer eq $rpl->{name}) {
	    $allocator = $rpl;
	} else {
	    printf("Unknown buffer in Line\n%s",$_);
	    exit 0;
	}
	if ($allocator->{nchunk} != 0) {
	    printf("Duplicate initialization of allocator %s in line [%d]\n%s",$buffer,$lineno,$_);
	    &print_allocator($allocator);
	    exit 1;
	}
	$allocator->{nchunk} = $nchunk;
	$allocator->{nbytes} = $nbytes;
	$allocator->{chunksz} = $nbytes/$nchunk;
	
    } elsif (/CHUNK_(\S+):\s+name\s+(\S+),\s+inuse\s+=\s+(\d+),.*offset=(\d+)\s*$/) {
	my $af = $1;
	my $buffer = $2;
	my $inuse = $3;
	my $offset = $4;
	my $allocator;
	my $line = $_;
	chomp($line);
	$lineno++;
	if ($buffer eq $req->{name}) {
	    $allocator = $req;
	} elsif ($buffer eq $rpl->{name}) {
	    $allocator = $rpl;
	} else {
	    printf("Unknown buffer in Line\n%s",$_);
	    exit 0;
	}
	if ($af eq "ALLOC") {
	    &do_alloc($allocator,$inuse,$offset,$lineno,$line);
	} elsif ($af eq "FREE") {
	    &do_free($allocator,$inuse,$offset,$lineno,$line);
	} else {
	    printf("Unknown Allocation Fundtion in Line\n%s",$_);
	    exit 0;
	}
    } else {
	printf("Skipping Input Line\n%s",$_) if $debug;
    }
}
# if we got here, found no errors.
printf("Found no Alloc/Free errors in either ReqSB or RplSB\n");
&print_allocator($req);
&print_allocator($rpl);
exit 0;

sub print_allocator {
    my $al = shift;
    printf("Allocator %s: numchunk=%d len=%d  num Alloc=%d  Num Freed=%d Max Used=%d\n",
	  $al->{name},$al->{nchunk},$al->{len},$al->{numalloc},$al->{numfree},$al->{max_in_use});
    my ($off,$val);
    while ( ($off,$val) = each %{$al->{alloc}}) {
	printf("\t\%d  [chunkno=%d]\n",$off,$off/$al->{chunksz}) if ($val == 1);
    }
}

sub do_alloc {
    my $al = shift;
    my $inuse = shift;
    my $off = shift;
    my $lineno = shift;
    my $line = shift;
    my $chunkno = $off/$al->{chunksz};
    if ($chunkno > $al->{nchunk}) {
	printf("%s Allocating at offset %d chunkno=%d is outside range %d in line [%d]:\n%s\n",
	       $al->{name},$off,$chunkno,$al->{nchunk},$lineno,$line);
	&print_allocator($al);
	exit 0;
    } 
    my $alloc = $al->{alloc};
    if ($alloc->{$off} != 0) {
	printf("%s Allocating at offset %d already exists in line [%d]:\n%s\n",
	       $al->{name},$off,$lineno,$line);
	&print_allocator($al);
	exit 0;
    }
    $alloc->{$off} = 1;
    $al->{numalloc} += 1;
    $al->{len} += 1;
    $al->{max_in_use} = $al->{len} if ($al->{len} > $al->{max_in_use});
    if ($al->{len} != $inuse) {
	printf("%s Allocating at offset %d.  len[%d] != inuse[%d] in line [%d]\n%s\n",
	    $al->{name},$off,$al->{len},$inuse,$lineno,$line);
	&print_allocator($al);
	exit 0;
    }
    if ($debug) {
	printf("Allocated %d in %s [%s]\n",$off,$al->{name},$line);
	&print_allocator($al);
    }
}
sub do_free {
    my $al = shift;
    my $inuse = shift;
    my $off = shift;
    my $lineno = shift;
    my $line = shift;
    my $chunkno = $off/$al->{chunksz};
    if ($chunkno > $al->{nchunk}) {
	printf("%s Freeing at offset %d chunkno=%d is outside range %d in line [%d]:\n%s\n",
	       $al->{name},$off,$chunkno,$al->{nchunk},$lineno,$line);
	&print_allocator($al);
	exit 0;
    }
    my $alloc = $al->{alloc};
#    if (! exists $alloc->{$off}) {
    if ($alloc->{$off} != 1) {
	printf("%s Freeing at offset %d not in list at line [%d]:\n%s\n",
	       $al->{name},$off,$lineno,$line);
	&print_allocator($al);
	exit 0;
    }
    $alloc->{$off} = 0;
#    undef $alloc->{$off};
    $al->{numfree} += 1;
    $al->{len} -= 1;
    if ($al->{len} != $inuse) {
	printf("%s Freeing at offset %d.  len[%d] != inuse[%d] in line [%d]\n%s\n",
	    $al->{name},$off,$al->{len},$inuse,$lineno,$line);
	&print_allocator($al);
	exit 0;
    }
    if ($debug) {
	printf("Freed     %d in %s [%s]\n",$off,$al->{name},$line);
	&print_allocator($al);
    }
}

