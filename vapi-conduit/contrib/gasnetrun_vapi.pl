#!/usr/bin/env perl
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/contrib/Attic/gasnetrun_vapi.pl,v $
#     $Date: 2005/04/17 03:55:17 $
# $Revision: 1.2 $
# Description: GASNet VAPI spawner
# Terms of use are as specified in license.txt

require 5.004;
use strict;

# Globals
my @mpi_args = ();
my $numproc = undef;
my $numnode = undef;
my $verbose = 0;
my $keep = 0;
my $dryrun = 0;
my $exename = undef;
my $nodefile = $ENV{'GASNET_NODEFILE'} || $ENV{'PBS_NODEFILE'};
my @tmpfiles = (defined($nodefile) && $ENV{'GASNET_RM_NODEFILE'}) ? ("$nodefile") : ();
my $spawner = $ENV{'GASNET_VAPI_SPAWNER'};

sub usage
{
    print (@_) if (@_);

    print "usage: gasnetrun -n <n> [options] [--] prog [program args]\n";
    print "    options:\n";
    print "      -n <n>                number of processes to run\n";
    print "      -N <N>                number of nodes to run on (not always supported)\n";
    print "      -E <VAR1[,VAR2...]>   list of environment vars to propagate\n";
    print "      -v                    be verbose about what is happening\n";
    print "      -t                    test only, don't execute anything (implies -v)\n";
    print "      -k                    keep any temporary files created (implies -v)\n";
    print "      -spawner=(ssh|mpi)    force use of MPI or SSH for spawning\n";
    print "      --                    ends option parsing\n";
    exit 1;
}

# We need to parse our command-line arguments
# We also build up @mpi_args, stripping out ones that are purely ours
   
    while (@ARGV > 0) {
	$_ = $ARGV[0];
	push @mpi_args, $_;

	if ($_ eq '--') {
	    shift;
	    last;
	} elsif ($_ eq '-n' || $_ eq '-np') {
	    shift;
	    push @mpi_args, $ARGV[0];
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numproc = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numproc >= 1;
	} elsif ($_ =~ /^(-np?)([0-9]+)$/) {
	    $numproc = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numproc >= 1;
	} elsif ($_ eq '-N') {
	    shift;
	    push @mpi_args, $ARGV[0];
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numnode = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numnode >= 1;
	} elsif ($_ =~ /^(-N)([0-9]+)$/) {
	    $numnode = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numnode >= 1;
	} elsif ($_ eq '-E') {
	    shift;
	    push @mpi_args, $ARGV[0];
	    usage ("-E option given without an argument\n") unless @ARGV >= 1;
	} elsif ($_ =~ /^-spawner=(.+)$/) {
	    $spawner = $1;
	    pop @mpi_args;	# not known to mpi spawner
	} elsif ($_ eq '-v') {
	    $verbose = 1;
	} elsif ($_ eq '-t') {
	    $dryrun = 1;
	    $verbose = 1;
	} elsif ($_ eq '-k') {
	    $keep = 1;
	    $verbose = 1;
	} elsif (m/^-/) {
	    usage ("unrecognized option '$_'\n");
	} else {
	    last;
	}
	shift;
    }
    pop @mpi_args; # pop off program name
    push @mpi_args, @ARGV;
    $spawner = uc($spawner);

# Validate flags
    if (!defined($numproc)) {
        usage "Required option -n was not given\n";
    }
    if (!defined($spawner)) {
        usage "Option -spawner was not given and no default is set\n"
    }
    if (($spawner eq 'MPI') && !$ENV{VAPI_BOOTSTRAP_MPI}) {
        usage "Spawner is set to MPI, but MPI support was not compiled in\n"
    }

# Find the program
    my $exebase = shift or usage "No program specified\n";
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
    die("gasnetrun: unable to locate program '$exebase'\n")
		unless (defined($exename) && -x $exename);
    print("gasnetrun: located executable '$exename'\n") if ($verbose);

# Verify the program's capabilities
    open (FILE, $exename) or die "can't open file '$exename'\n";
    {   local $/ = '$'; # use $ as the line break symbol
	my $pattern = "^GASNet" . $spawner . "Spawner: 1 \\\$";
	my $found = 0;
        while (<FILE>) {
            if (/$pattern/o) { $found = 1; last; }
        }
        die "Executable does not support spawner '$spawner'\n" unless $found;
    }

# Run it which ever way makes sense
    $ENV{"GASNET_VERBOSEENV"} = "1" if ($verbose);
    if ($spawner eq 'MPI') {
        print("gasnetrun: forwarding to mpi-based spawner\n") if ($verbose);
        @ARGV = @mpi_args;
        (my $mpi = $0) =~ s/\.pl$/-mpi.pl/;
        do $mpi or die "cannot run $mpi\n";
    } elsif ($spawner eq 'SSH') {
	my @cmd = grep { defined($_); } ($exename, '-GASNET-SPAWN-master',
					 $verbose ? '-v' : undef,
					 "$numproc" . ($numnode ? ":$numnode" : ''),
					 '--', @ARGV);
	print("gasnetrun: running: ", join(' ', @cmd), "\n") if ($verbose);
	unless ($dryrun) { exec(@cmd) or die "failed to exec $exebase\n"; }
    } else {
        die "Unknown spawner '$spawner' requested\n";
    }

__END__
