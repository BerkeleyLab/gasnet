#!/usr/bin/perl

package ammsg;

sub new {
    my $class = shift;
    my $str = shift;
    $str=~ s/^\s+//;
    # Line Format: [S S Req s=0 d=1 sq=0 sr=0 h=64 mlen=16 dlen=0 crc=0 cred=0:0:1 narg=4 1 0 0 1 0 0 0 0]
    my ($node,$time,$junk,$amd,$sr,$amtype,$req,$src,$dest,$seq,$sr_seq,$hndlr,$mlen,$dlen,$crc,$cred,$narg,@args) = split(/\s+/,$str);
    $time =~ s/\>//;
    $src  =~ s/s=//;
    $dest =~ s/d=//;
    $seq  =~ s/sq=//;
    $sr_seq  =~ s/sr=//;
    $hndlr =~ s/^h=//;
    $mlen =~ s/^mlen=//;
    $dlen =~ s/^dlen=//;
    $crc =~ s/^crc=//;
    $cred =~ s/^crc=//;
    $narg =~ s/^narg=//;
#    printf("[%d] %s %s_%s seqno=%d [%s]\n",$node,$sr,$amtype,$req,$seq,$str);
    my $self = {
	line => $str,
	node => $node,
	amtype => $amtype,
	send => ($sr eq "S"),
	isreq => ($req eq "Req"),
	seq  => $seq,
	src => $src,
	dest => $dest,
	hndlr => $hndlr,
	mlen => $mlen,
	dlen => $dlen,
	crc => $crc,
	cred => $cred,
	narg => $narg,
	args => @args
    };
    bless $self, $class;
    return $self;
}

sub fatal {
    my $s = shift;
    my $str = shift;
    my $o = shift;

    printf("Error: %s\n",$str);
    printf("\t%s\n",$s->{line});
    printf("\t%s\n",$o->{line});
#    exit(1);
}

sub cmp {
    my $s = shift;
    my $r = shift;
#    printf("Comparing [%s]\n",$s->{line});
#    printf("To        [%s]\n",$o->{line});
#    printf("sr = %s, amtype = %s, req_rpl = %s\n",$s->{sr},$s->{amtype},$s->{reqrpl});
#    printf("hndlr = %d, mlen = %d, dlen = %d, narg = %d\n",
#	   $s->{hndlr},$s->{mlen},$s->{dlen},$s->{narg});
    if ($s->{seq} ne $r->{seq}) {
	$s->fatal("Sequence number mismatch",$r);
	return 0;
    }
    if (!$s->{send} || $r->{send}) {
	$s->fatal("Send/Recv Tag mismatch",$r);
	return 0;
    }
    if ($s->{amtype} != $r->{amtype}) {
	$s->fatal("AM Type Mismatch",$r);
	return 0;
    }
    if ($s->{hndlr} != $r->{hndlr}) {
	$s->fatal("Handler ID Mismatch",$r);
	return 0;
    }
    if ($s->{mlen} != $r->{mlen}) {
	if (($r->{mlen} == -1) && ($r->{amtype} eq "L")) {
	    # this is ok, lost msg length in 2-part unpacked data recv
	} else {
	    $s->fatal("Message Length Mismatch",$r);
	}
	return 0;
    }
    if ($s->{dlen} != $r->{dlen}) {
	$s->fatal("Data Length Mismatch",$r);
	return 0;
    }
    if ($s->{crc} != $r->{crc}) {
	$s->fatal("Data CRC Mismatch",$r);
	return 0;
    }
    if ($s->{narg} != $r->{narg}) {
	$s->fatal("Arg Count Mismatch",$r);
	return 0;
    }
    my $i;
    for ($i = 0; $i < $s->{narg}; $i++) {
	if ($s->{args}[$i] != $r->{args}[$i]) {
	    $s->fatal("Mismatch in Arg $i",$r);
	    return 0;
	}
    }
    return 1;
}
# ========================================================================
# Perl script read lowcred output files and select lines that have
# the specified Recv node.
# ========================================================================
package main;

use Getopt::Std;
use IO::File;
use File::Basename;

getopts("vdS:r:s:");

my $verbose = 0;
$verbose = 1 if (defined($opt_v));
my $debug = 0;
$debug = 1 if (defined($opt_d));
$verbose = 1 if $debug;
my $sendfile = "trace-0";
$sendfile = $opt_s if (defined($opt_s));
my $recvfile = "trace-1";
$recvfile = $opt_r if (defined($opt_r));
if (defined($opt_S)) {
    if ($opt_S == 0) {
	$sendfile = "trace-0";
	$recvfile = "trace-1";
    } else {
	$sendfile = "trace-1";
	$recvfile = "trace-0";
    }
}
# read from stdin, write to stdout
printf("Reading Sends from $sendfile\n");
printf("Reading Recvs from $recvfile\n");

my $sf = new IO::File("< $sendfile");
die "Cant open [$sendfile] for reading" if (!defined($sf));
my $rf = new IO::File("< $recvfile");
die "Cant open [$recvfile] for reading" if (!defined($rf));

# hash that contains all non-completed message states
%state = ();

my $cnt = 0;
while (!$sf->eof()  && !$rf->eof()) {
    &parseline($sf,1);
    &parseline($rf,0);
    $cnt++;
    last if ($debug && $cnt > 20);
}
printf("Unfinished Messages at end of input\n");
&dumpstate();
exit(0);

sub dumpstate {
    my $seq;
    my $rec;
    while (($seq,$rec) = each %state) {
	if ($rec) {
	    &dump_rec($rec);
	    printf("\n");
	}
    }
}

sub dumpfile {
    my $fh = shift;
    my $name = shift;
    if (! $fh->eof()) {
	printf("\nRemainder of file [%s]\n",$name);
	while (<$fh>) {
	    print;
	}
    }
}

sub new_rec {
    my $rec = {
	req_send => 0,
	req_recv => 0,
	rpl_send => 0,
	rpl_recv => 0
    };
    return $rec;
}

sub check_req_send {
    my $s = shift;
    my $seq = $s->{seq};
    printf("Found Req_Send for %d in [%s]\n",$seq,$s->{line}) if $debug;
    my $rec = $state{$seq};
    if (! $rec) {
	$rec = &new_rec;
	$rec->{req_send} = $s;
	$state{$seq} = $rec;
	return;
    }
    if ($rec->{rpl_send} || $rec->{rpl_recv}) {
	printf("Got Req Send for %d but Reply already exists in [%s]\n",$seq,$s->{line});
	&dump_rec($rec);
	exit 1;
    }
    my $r = $rec->{req_recv};
    if ($r) {
	if (! $s->cmp($r)) {
	    &dump_rec($rec);
	    exit 1;
	}
    }
}
sub check_req_recv {
    my $r = shift;
    my $seq = $r->{seq};
    printf("Found Req_Recv for %d in [%s]\n",$seq,$s->{line}) if $debug;
    my $rec = $state{$seq};
    if (! $rec) {
	$rec = &new_rec;
	$rec->{req_recv} = $r;
	$state{$seq} = $rec;
	return;
    }
    if ($rec->{rpl_send} || $rec->{rpl_recv}) {
	printf("Got Req Recv for %d but Reply already exists in [%s]\n",$seq,$s->{line});
	&dump_rec($rec);
	exit 1;
    }
    $rec->{req_recv} = $r;
    my $s = $rec->{req_send};
    if ($s) {
	if (! $s->cmp($r)) {
	    &dump_rec($rec);
	    exit 1;
	}
    }
}
sub check_rpl_send {
    my $s = shift;
    my $seq = $s->{seq};
    printf("Found Rpl_Send for %d in [%s]\n",$seq,$s->{line}) if $debug;
    my $rec = $state{$seq};
    if (! $rec) {
	printf("Got Rpl Send for %d but Req not found in [%s]\n",$seq,$s->{line});
	&dump_rec($rec);
	exit 1;
    }
    if (!$rec->{req_recv}) {
	printf("Got Rpl Send for %d but Req Recv not found in [%s]\n",$seq,$s->{line});
	&dump_rec($rec);
	exit 1;
    }
    $rec->{rpl_send} = $s;
    my $r = $rec->{rpl_recv};
    if ($r) {
	if (! $s->cmp($r)) {
	    &dump_rec($rec);
	    exit 1;
	}
    }
}
sub check_rpl_recv {
    my $r = shift;
    my $seq = $r->{seq};
    printf("Found Rpl_Recv for %d\n",$seq) if $debug;
    my $rec = $state{$seq};
    if (! $rec) {
	printf("Got Rpl Recv for %d but Req not found in [%s]\n",$seq,$s->{line});
	&dump_rec($rec);
	exit 1;
    }
    if (!$rec->{req_send}) {
	printf("Got Rpl Recv for %d but Req Send not found in [%s]\n",$seq,$s->{line});
	&dump_rec($rec);
	exit 1;
    }
    $rec->{rpl_recv} = $r;
    my $s = $rec->{rpl_send};
    if ($s) {
	if (! $s->cmp($r)) {
	    &dump_rec($rec);
	    exit 1;
	}
    }
}
sub dump_rec {
    my $rec = shift;
    printf("%s\n",$rec->{req_send}->{line}) if ($rec->{req_send});
    printf("%s\n",$rec->{req_recv}->{line}) if ($rec->{req_recv});
    printf("%s\n",$rec->{rpl_send}->{line}) if ($rec->{rpl_send});
    printf("%s\n",$rec->{rpl_recv}->{line}) if ($rec->{rpl_recv});
}
sub check_complete {
    my $seq = shift;
    my $rec = $state{$seq};
    my $cnt = 0;
    $cnt++ if $rec->{req_send};
    $cnt++ if $rec->{req_recv};
    $cnt++ if $rec->{rpl_send};
    $cnt++ if $rec->{rpl_recv};
    if ($cnt == 4) {
	if ($verbose) {
	    printf("\nCompleted record\n");
	    &dump_rec($rec);
	}
	$state{$seq} = 0;
    }
}
sub check_msg {
    my $msg = shift;
    my $isReq = shift;
    my $send  = shift;

    if ($isReq && $send) {
	&check_req_send($msg);
    } elsif ($isReq) {
	&check_req_recv($msg);
    } elsif ($send) {
	&check_rpl_send($msg);
    } else {
	&check_rpl_recv($msg);
    }
    &check_complete($msg->{seq});
}
	

sub parseline {
    my $fh = shift;
    my $sender = shift;
    while (<$fh>) {
	if (/ AMD (\S) (\S) (\S{3})/) {
	    my $sending = ($1 eq "S");
	    my $isReq = ($3 eq "Req");
	    my $line = $_;
	    chomp($line);
	    if ($sender) {
		# sender file on checks req_send and rpl_recv
		if (($isReq && $sending) || (!$isReq && !$sending)) {
		    my $s = new ammsg($line);
		    &check_msg($s,$isReq,$sending);
		    return;
		}
	    } else {
		# receiver file only checks req_recv and rpl_send
		if (($isReq && !$sending) || (!$isReq && $sending)) {
		    my $s = new ammsg($line);
		    &check_msg($s,$isReq,$sending);
		    return;
		}
	    }
	}
    }
    return;
}

