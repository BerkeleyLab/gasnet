#!/usr/bin/env perl
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/contrib/gasnetrun_mpi.pl,v 1.2 2003/11/09 07:39:52 phargrov Exp $
# Description: GASNet MPI spawner
# Terms of use are as specified in license.txt

require 5.004;
use strict;

# NOTE: The value of $ENV{'MPIRUN_CMD'} may be set in the shell wrapper
my $spawncmd = $ENV{'MPIRUN_CMD'} || 'mpirun -np %N %P %A';
$spawncmd =~ s/%C/%P %A/;	# deal with common alias

# Globals
my $envlist = '';
my $nnodes = undef;
my $verbose = 0;
my $dryrun = 0;
my $exename = undef;
my $find_exe = 1;	# should we find full path of executable?

# Define how to pass the environment vars
# 5 parameters to set: val, pre, inter, post and join
# To pass env as "-X A -Y B -Y C -Z" (a made up example)
#%envfmt = ('pre' => '-X', 'inter' => '-Y', 'post' => '-Z');
    my %envfmt = ();

# Probe for which MPI is running
    my $mpirun_help = `mpirun -help 2>&1`;
    my $is_lam      = ($mpirun_help =~ m|LAM/MPI|);
    my $is_mpich_nt = ($mpirun_help =~ m|MPIRun|);
    my $is_mpich    = ($mpirun_help =~ m|ch_p4|);

    if ($is_lam) {
	# pass env as "-x A,B,C"
	%envfmt = ( 'pre' => '-x',
		    'join' => ','
		  );
    } elsif ($is_mpich_nt) {
	# pass env as "-env A=1|B=2|C=3"
	%envfmt = ( 'pre' => '-env',
		    'join' => '|',
		    'val' => 1
		  );
	$find_exe = 0;
    } else {
	# pass env as "/usr/bin/env A=1 B=2 C=3"
	# Our nearly universal default
	chomp(my $envprog = `which env`);
	%envfmt = ( 'pre' => $envprog,
		    'val' => 1
		  );
    }


sub usage
{
    print (@_) if (@_);

    print "usage: gasnetrun -n <n> [options] [--] prog [program args]\n";
    print "    options:\n";
    print "      -n <n>                number of nodes to run on\n";
    print "      -E <VAR1[,VAR2...]>   list of environment vars to propagate\n";
    print "      -v                    be verbose about what is happening\n";
    print "      -t                    test only, don't execute anything (implies -v)\n";
    print "      --                    ends option parsing\n";
    exit 1;
}

# Function to apply shell quoting for spaces and metachars.
# Only used for human readable output
sub do_quote
{
    $_ = shift;

    if (m/[\\`" !#&*$()<>|]/) {
	if (m/'/) { s/'/'\\''/; }
	return "'$_'";
    } elsif (m/'/) {
	return '"' . $_ . '"';
    } else {
	return $_;
    }
}
	

# We need to parse our command-line arguments
    while (@ARGV > 0) {
	$_ = $ARGV[0];

	if ($_ eq '--') {
	    shift;
	    last;
	} elsif ($_ eq '-n' || $_ eq '-np') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $nnodes = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $nnodes >= 1;
	} elsif ($_ =~ /^(-np?)([0-9]+)$/) {
	    $nnodes = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $nnodes >= 1;
	} elsif ($_ eq '-E') {
	    shift;
	    usage ("-E option given without an argument\n") unless @ARGV >= 1;
	    $envlist = $ARGV[0];
	} elsif ($_ eq '-v') {
	    $verbose = 1;
	} elsif ($_ eq '-t') {
	    $dryrun = 1;
	    $verbose = 1;
	} elsif (m/^-/) {
	    usage ("unrecognized option '$_'\n");
	} else {
	    last;
	}
	shift;
    }

# Validate -n as needed
    if (!defined($nnodes) && $spawncmd =~ /%N/) {
	usage "Required option -n was not given\n";
    }

# Find the program
    my $exebase = shift or usage "No program specified\n";
    if ($find_exe) {
        if ($exebase =~ m|^/|) {
	    # full path, don't do anything to it
	    $exename = $exebase;
        } elsif ($exebase =~ m|/| || -x $exebase) {
	    # has directory components or exists in cwd
	    my $cwd = `pwd`;
	    chomp $cwd;
	    $exename = "$cwd/$exebase";
        } else {
	    # search PATH
	    foreach (split(':', $ENV{PATH})) {
	        my $tmp = "$_/$exebase";
	        if (-x $tmp) {
		    $exename = $tmp;
		    last;
	        }
	    }
        }
        die("Unable to locate program '$exebase'\n")
		    unless (defined($exename) && -x $exename);
        print("Located executable '$exename'\n") if ($verbose);
    } else {
        $exename = $exebase;
    }

# We need to gather a list of important environment variables
    # Form a list of the vars given by -E, plus any GASNET_* vars
    my @envvars = ((grep {+exists($ENV{$_})} split(',', $envlist)),
		   (grep {+m/^GASNET_/} keys(%ENV)));

# Build up the environment-passing arguments in several steps
    my @envargs = @envvars;
    if (@envvars) {
        # pair the variables with their values if desired
        if (defined $envfmt{val}) {
	    @envargs = map { "$_=$ENV{$_}" } @envargs;
        }
        # join them into a single argument if desired
        if (defined $envfmt{join}) {
	    @envargs = join($envfmt{join}, @envargs);
        }
        # introduce 'inter' arg between variable (no effect if already joined)
        if (defined $envfmt{inter}) {
	    @envargs = map { ($_, $envfmt{inter}) } @envargs;
	    pop @envargs;
        }
        # tack on 'pre' and 'post' args
        if (defined $envfmt{pre}) {
	    unshift @envargs, $envfmt{pre};
        }
        if (defined $envfmt{post}) {
	    push @envargs, $envfmt{post};
        }
    }

# Exec it
    my @spawncmd = map { +s/%N/$nnodes/g;
                          if (m/^%P$/) {
                              (@envargs, $exename);
                          } elsif (m/^%A$/) {
			      (@ARGV);
			  } else {
                              $_;
                          }
			} split(" ", $spawncmd);
    print("running: ", join(' ', (map { do_quote $_; } @spawncmd)), "\n")
	if ($verbose);
    exit(0) if ($dryrun);

exec(@spawncmd);
die "exec failed: $!\n";
__END__
