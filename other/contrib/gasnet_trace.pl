#! /usr/bin/env perl

#############################################################
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/contrib/gasnet_trace.pl,v $
#     $Date: 2004/08/26 19:13:02 $
# $Revision: 1.18 $
#
# All files in this directory (except where otherwise noted) are subject to the
#following licensing terms:
#
#---------------------------------------------------------------------------
#Copyright (c) 2003, The Regents of the University of California, through
#Lawrence Berkeley National Laboratory (subject to receipt of any required
#approvals from U.S. Dept. of Energy)
#
#All rights reserved.
#
#Redistribution and use in source and binary forms with its documentation, with
#or without modification, are permitted for any purpose, without fee, provided
#that the following conditions are met:
#
#(1) Redistributions of source code must retain the above copyright notice, this
#list of conditions and the following disclaimer.
#(2) Redistributions in binary form must reproduce the above copyright notice,
#this list of conditions and the following disclaimer in the documentation and/or
#other materials provided with the distribution.
#(3) Neither the name of Lawrence Berkeley National Laboratory, U.S. Dept. of
#Energy nor the names of its contributors may be used to endorse or promote
#products derived from this software without specific prior written permission.
#
#THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
#ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
#ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#---------------------------------------------------------------------------
#
#author:  Wei Tu
#email:   weitu@ocf.berkeley.edu
#
#############################################################


use strict;
use FileHandle;
use Getopt::Long;
    
# Global Variables
########################

my ($opt_sort, $opt_output, $opt_help, $opt_report);
my ($opt_internal, $opt_full, $opt_thread, $opt_filter);

my (%data, %report, %threads, %nodes);
my (%node_threads); 
my (%job_nodes, %job_seen, %job_uniq); 
#%nodes, %threads are identifier->thread(node)num

# Getting the Options
########################

GetOptions (
    'h|?|help'		=> \$opt_help,
    'sort=s'		=> \$opt_sort,
    'o=s'		=> \$opt_output,
    'report=s'		=> \$opt_report,
    't'			=> \$opt_thread,
    'thread!'		=> \$opt_thread,
    'i'			=> \$opt_internal,
    'internal!'		=> \$opt_internal,
    'f'			=> \$opt_full,
    'full!'		=> \$opt_full,
    'filter=s'		=> \$opt_filter
);

# The main routine
########################
usage() if $opt_help;

if (!@ARGV) {
    usage (-1);
}

if ($opt_output) {
    open(STDOUT, ">$opt_output") or die "Could not write to $opt_output: $!\n";
}

if (!$opt_report) {
    $opt_report="PGB";
} 

while (@ARGV) {
    my $arg = pop @ARGV;
    parse_threadinfo($arg);
    parse_tracefile($arg);
}
foreach my $job (keys %job_nodes) {
    my ($want, $have) = ($job_nodes{$job}, $job_seen{$job});
    if ($have < $want) {
	print STDERR "WARNING: only have traces for $have out of $want nodes of job $job\n";
    }
}

convert_report();
sort_report();
trace_output(*STDOUT, "GET") if $opt_report =~ /G/;
trace_output(*STDOUT, "PUT") if $opt_report =~ /P/;
trace_output(*STDOUT, "BARRIER") if $opt_report =~ /B/;
# Show program usage
########################
sub usage 
{
    print <<EOF;
GASNet trace file summarization script, v1.0
Usage:  gasnet_trace [options] trace-file(s)

Options:
    -h -? -help         See this message.
    -o [filename]       Output results to file. Default is STDOUT.
    -report [r1][r2]..  One or more capital letters to indicate which 
                        reports to generate: P(PUT), G(GET), and/or B(BARRIER).
                        Default: all reports.
    -sort [f1],[f2]...  Sort output by one or more fields: TOTAL, AVG, MIN, MAX,
                        CALLS, TYPE, or SRC. (for GETS/PUTS, TOTAL, AVG, MIN,
                        and MAX refer to message size: for BARRIERS, to time
                        spent in barrier).  Default: sort by SRC (source
                        file/line). 
    -filter [t1],[t2].. Filter out output by one or more types:
    			LOCAL, GLOBAL, WAIT, WAITNOTIFY.  
    -t -[no]thread      Output detailed information for each thread.
    -i -[no]internal    Show internal events (such as the initial and final
                        barriers) which do not correspond to user source code. 
    -f -[no]full        Show the full source file name.
EOF
    exit(-1);
}


    
# subroutine to read the tracefile and dump the useful information into a 
# data-structure, namely an array of hashes and return the array.
# args : the filename to be read.
########################
sub parse_threadinfo
{
    open (TRACEFILE, $_[0]) or die "Could not open $_[0]: $!\n";
    
    while (<TRACEFILE>) {
        next unless /MAGIC/;
        m/^(\S+).*I am thread\s(\d+).*on node\s(\d+) of (\d+)\s.*<(.+)>$/;
        $threads{$1} = $2;
        $nodes{$1} = $3;
        $node_threads{$3}++;
        $job_nodes{$5} = $4;
        $job_seen{$5}++;
        if ($job_uniq{$5,$2}++) {
            print STDERR "WARNING: duplicate tracing data for thread $2 of job $5\n";
	}
    }	       

}

sub parse_tracefile 
{
    
    open (TRACEFILE, $_[0]) or die "Could not open $_[0]: $!\n";
    
    while (<TRACEFILE>) {
        my ($thread, $src, $pgb, $type, $sz);
	if (/(\S+)\s\S+\s\[([^\]]+)\]\s+\([HPGB]\)\s+(PUT|GET|BARRIER)(.*):\D+(\d+)/) { 
            ($thread, $src, $pgb, $type, $sz) = ($1, $2, $3, $4, $5);
            if ($pgb =~ /PUT|GET/) {
	        $type = ($type =~ /_LOCAL$/) ? "LOCAL" : "GLOBAL";
            } elsif ($pgb =~ /BARRIER/) {
	        $type =~ s/^_//;
		next unless ($type =~ /NOTIFYWAIT|WAIT/);	# discard unknowns
                $thread = $nodes{$thread};
            }
	} else {
	    next;
	}
        update_data($thread, $src, $pgb, $type, $sz);
    }
}
                
sub update_data
{
    my ($thread, $src, $pgb, $type, $sz) = @_;        
    if (!$data{$pgb}{$src}{$type}{$thread}) { # first record
        push @{$data{$pgb}{$src}{$type}{$thread}}, ($sz, $sz, $sz, $sz, 1);        
    } else {
        my ($max, $min, $avg, $total, $totalc) = 
            @{$data{$pgb}{$src}{$type}{$thread}};
        $max = $max > $sz ? $max : $sz;
        $min = $min < $sz ? $min : $sz;
        $total += $sz;
        $totalc += 1;
        $avg = $total / $totalc;
        @{$data{$pgb}{$src}{$type}{$thread}} 
            = ($max, $min, $avg, $total, $totalc);
        
    }
}

# subroutine to canonicalize the msg size
# e.g -> 14336->14K, 2516582->2.4M
# args: the msg size to be canonicalized
########################
sub shorten
{
    my ($msg_sz, $type) = @_;
    if ($type =~ /GET|PUT/) {
    	if ($msg_sz < 1024) {
    	    return sprintf("%.0f B", $msg_sz);
    	} elsif ($msg_sz < 1024 * 1024) {
    	    return sprintf("%.2f K", $msg_sz / 1024.0);
    	} elsif ($msg_sz < 1024 * 1024 * 1024) {
    	    return sprintf("%.2f M", $msg_sz / (1024.0 * 1024.0));
    	} elsif ($msg_sz < 1024 * 1024 * 1024 * 1024) {
    	    return sprintf("%.2f G", $msg_sz / (1024.0 * 1024.0 * 1024.0));
    	} else {
    	    return sprintf("%.2f T", $msg_sz / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    	}
    } else {
    	if ($msg_sz < 1000) {
    	    return sprintf("%.1f us", $msg_sz);
    	} elsif ($msg_sz < 1000 * 1000) {
    	    return sprintf("%.1f ms", $msg_sz / 1000.0);
    	} elsif ($msg_sz < 1000 * 1000 * 60) {
    	    return sprintf("%.1f  s", $msg_sz / (1000.0 * 1000.0));
    	} else {
    	    return sprintf("%.1fmin", $msg_sz / (1000.0 * 1000.0 * 60.0));
    	}
    }
}

# subroutine to separate the source file name 
# and the line number
# args: the source line to be separated
#######################
sub src_line
{
    my ($line) = @_;
	
    $line =~ /(.*):(\d+)$/;    
    return ($1, $2);
}

# transfer the raw data structure into report -- a hash of arrays 
#######################
sub convert_report 
{
    foreach my $pgb (keys %data) {
    	foreach my $line (keys %{$data{$pgb}}) {
    	    foreach my $type (keys %{$data{$pgb}{$line}}) {

    	    	my ($max, $min, $avg, $total, $totalc);
    	    	foreach my $thread (keys %{$data{$pgb}{$line}{$type}}) {
    	    	    # For Barrier $thread is actually the node number
    	    	    my ($tmax, $tmin, $tavg, $ttotal, $ttotalc) 
			 = @{$data{$pgb}{$line}{$type}{$thread}};
    	    	    $max = $max > $tmax ? $max : $tmax;
    	    	    $min = ($min > $tmin || !$min) ? $tmin : $min;
    	    	    if ($pgb =~ /BARRIER/) {
    	    	        $total += $ttotal * $node_threads{$thread};
    	    	        $totalc += $ttotalc * $node_threads{$thread};
    	    	    } else { 
    	    	        $total += $ttotal;
    	    	        $totalc += $ttotalc;
                    }		
    	    	}
		die "INTERNAL ERROR" unless $totalc;
    	    	$avg = $total / $totalc;
    	    	if ($pgb =~ /BARRIER/) {
		    die "INTERNAL ERROR" unless scalar (keys %nodes);
    	    	    $totalc = $totalc / (scalar (keys %nodes));
                }
    	    	my @entry = ($line, $type, $max, $min, $avg, $total, $totalc);
		push @{$report{$pgb}}, \@entry; 
    	    }
    	}
    }
}


# report_sorting criterion
#######################
sub criterion
{
    my @mtd = @_;
    my $result;
    my $sort_mtd = shift @mtd;
    # Breaking ties using the less important fields.
    while (!$result && $sort_mtd) {
    	if ($sort_mtd eq "CALLS") {
            $result = ${$b}[6] <=> ${$a}[6];;
    	} 
        if ($sort_mtd eq "TOTAL") {
            $result = ${$b}[5] <=> ${$a}[5];
        }
        if ($sort_mtd eq "AVG") {
            $result = ${$b}[4] <=> ${$a}[4];
        }
        if ($sort_mtd eq "MIN") {
            $result = ${$b}[3] <=> ${$a}[3];
        }
        if ($sort_mtd eq "MAX") {
            $result = ${$b}[2] <=> ${$a}[2];
        }
        if ($sort_mtd eq "TYPE") {
            $result = (${$a}[1] cmp ${$b}[1]);
        }
        if ($sort_mtd eq "SRC") {
            my ($a_src, $a_line) = src_line${$a}[0];
            my ($b_src, $b_line) = src_line${$b}[0];
            $result = ($a_src cmp $b_src) ||
                      ($a_line <=> $b_line);
        }
    	
    	$sort_mtd = shift @mtd;
    }
    return $result;
}

# sorting the report
########################
sub sort_report 
{

    my @sortmtd = split /,/, $opt_sort;
    # Checking for valid input
    foreach my $mtd (@sortmtd) {
        $mtd =~ /^(CALLS|AVG|MAX|MIN|TOTAL|SRC|TYPE)$/
        or die "Could not recognize $mtd\n"; 
    }
    
    foreach my $pgb (keys %report) {
	if ($opt_sort) {
	    @{$report{$pgb}} = sort {criterion(@sortmtd)} @{$report{$pgb}};
	} else {
	    @{$report{$pgb}} = sort {criterion("SRC")} @{$report{$pgb}};
    	}
    }
	 
}

sub get_threads 
{
    my ($node) = @_;
    my @threads;
    foreach my $identifier (keys %nodes) {
        if ($nodes{$identifier} == $node) {
            push @threads, $threads{$identifier};
        }
    }
    @threads = sort @threads;
    return $threads[0] . ".." . $threads[-1];
}       

# subroutine to process the data structure produced by the parse_tracefile 
# subroutine and print out in a format that the caller specifies.
# args:	-filehandler -- specifying where the output should go
########################
sub trace_output 
{
    my ($handle, $pgb) = @_;
    
    my %filters;
    foreach my $filter (split /,/, $opt_filter) {
    	$filters{$filter}++;
    }

    # Print out 
    print "\n$pgb REPORT:\n";
    

    print <<EOF;
NO     SOURCE  LINE  TYPE          MSG:(min    max     avg     total)    CALLS  
==============================================================================    	
EOF
    
    # Setting up variables;
    my ($rank, $src_num, $source, $lnum, $type, $min, $max, $avg, $total, $calls);
    my ($threadnum, $tmin, $tmax, $tavg, $ttotal, $tcalls);
    $rank = 1;

    if (!$report{$pgb}) {
        print "NONE\n";
    }
    foreach my $entry (@{$report{$pgb}}) { 
        ($src_num, $type, $max, $min, $avg, $total, $calls) = @{$entry};
        ($source, $lnum) = src_line($src_num);
        # Skip internal events (having lnum==0) if not specified.
        next unless ($lnum || $opt_internal);
	# Filter out certain types;
	next unless !$filters{$type};
        
        
        $max = shorten($max, $pgb);
        $min = shorten($min, $pgb);
        $avg = shorten($avg, $pgb);
        $total = shorten($total, $pgb);
        
        # Options for showing the full file name
        if ($opt_full) {
	    printf "%3d %s\n", $rank, $source;
	    $handle->format_name("FULL");             
        }
        else {
            $source = substr $source, -10, 10;
            $handle->format_name("DEFAULT");
        }
        write($handle);
        $rank++;
        
        if ($opt_thread) {
            foreach my $thread (sort keys %{$data{$pgb}{$src_num}{$type}}) {
            	if ($pgb =~ /P|G/) {
            	    $threadnum = $threads{$thread};
            	} else {
            	    $threadnum = get_threads($thread);
                }
            	($tmax, $tmin, $tavg, $ttotal, $tcalls) = 
            	    @{$data{$pgb}{$src_num}{$type}{$thread}};
    		$tmax = shorten($tmax, $pgb);
		$tmin = shorten($tmin, $pgb);
    		$tavg = shorten($tavg, $pgb);
		$ttotal = shorten($ttotal, $pgb);

    		$handle->format_name("THREAD");
    		write($handle);
    	    }
    	}
        
        # Current max number of ranks is 999
        if ($rank >= 1000) {
            return;
	}
    }
    
# formats
########################

    format DEFAULT = 
@>> @<<<<<<<<< @>>>> @>>>>>>>>>  @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>
$rank, $source, $lnum, $type, $min, $max, $avg, $total, $calls
.

    format FULL = 
               @>>>> @>>>>>>>>>  @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>
               $lnum, $type, $min, $max, $avg, $total, $calls
.
	    
    format THREAD =
    Thread @<<<<<<<<<<<<         @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>>>> @>>>>>
$threadnum, $tmin, $tmax, $tavg, $ttotal, $tcalls
.
}

        

   
