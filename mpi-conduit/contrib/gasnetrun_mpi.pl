#!/usr/bin/env perl
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/contrib/gasnetrun_mpi.pl,v $
#     $Date: 2005/01/03 15:15:35 $
# $Revision: 1.14.2.1 $
# Description: GASNet MPI spawner
# Terms of use are as specified in license.txt

require 5.004;
use strict;

# NOTE: The value of $ENV{'MPIRUN_CMD'} may be set in the shell wrapper
my $spawncmd = $ENV{'MPIRUN_CMD'} || 'mpirun -np %N %P %A';
$spawncmd = stripouterquotes($spawncmd);
$spawncmd =~ s/%C/%P %A/;	# deal with common alias

# Validate the spawncmd
unless (exists($ENV{'MPIRUN_CMD_OK'}) ||
        (($spawncmd =~ m/%P/) && ($spawncmd =~ m/%A/) && ($spawncmd =~ m/%N/))) {
	die("gasnetrun: ERROR: MPIRUN_CMD='$spawncmd'\n"
          . "The environment variable MPIRUN_CMD must contain the strings '%P' and '%A'\n"
	  . "(or '%C' as an alias for '%P %A') for expansion into the program and its arguments;\n"
	  . "and '%N' for expansion into the number of processes.\n"
	  . "To disable this check, set MPIRUN_CMD_OK in your environment.\n");
}

# Globals
my $envlist = '';
my $numproc = undef;
my $numnode = undef;
my $verbose = 0;
my $dryrun = 0;
my $exename = undef;
my $find_exe = 1;	# should we find full path of executable?
my $tmpdir = undef;
my @tmpfiles = ();

# Define how to pass the environment vars
# 5 parameters to set: val, pre, inter, post and join
# To pass env as "-X A -Y B -Y C -Z" (a made up example)
#%envfmt = ('pre' => '-X', 'inter' => '-Y', 'post' => '-Z');
    my %envfmt = ();

# Probe for which MPI is running
    my $mpirun_cmd  = $spawncmd;
       $mpirun_cmd  =~ s/\s-.*/ -help/;
       $mpirun_cmd  =~ s/\s%[A-Za-z]+//; # required for Cray MPI
    my $mpirun_help = `$mpirun_cmd 2>&1`;
    my $is_lam      = ($mpirun_help =~ m|LAM/MPI|);
    my $is_mpich_nt = ($mpirun_help =~ m|MPIRun|);
    my $is_mpich    = ($mpirun_help =~ m|ch_p4|);
    my $is_mvich    = ($mpirun_help =~ m|MVICH|);
    my $is_cray_mpi = ($mpirun_help =~ m|Psched|);

    if ($is_lam) {
	# pass env as "-x A,B,C"
	%envfmt = ( 'pre' => '-x',
		    'join' => ','
		  );
    } elsif ($is_mpich_nt) {
	# pass env as "-env A=1|B=2|C=3"
	%envfmt = ( 'pre' => '-env',
		    'join' => '|',
		    'val' => ''
		  );
	$find_exe = 0;
    } elsif ($is_mvich) {
	# pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
        my $envprog = `which env`;
  	chomp $envprog;
	%envfmt = ( 'pre' => $envprog,
		    'val' => "'"
		  );
    } elsif ($is_cray_mpi) {
	# cannot reliably use /usr/bin/env at all when running via aprun 
        # (the binary doesnt support placed execution)
	# however, the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
    } else {
	# pass env as "/usr/bin/env A=1 B=2 C=3"
	# Our nearly universal default
	my $envprog = "/usr/bin/env";
        if (! -x $envprog) { # SuperUX has broken "which" implementation, so avoid if possible
          $envprog = `which env`;
  	  chomp $envprog;
        }
	%envfmt = ( 'pre' => $envprog,
		    'val' => ''
		  );
    }


sub usage
{
    print (@_) if (@_);

    print "usage: gasnetrun -n <n> [options] [--] prog [program args]\n";
    print "    options:\n";
    print "      -n <n>                number of processes to run\n";
    print "      -N <n>                number of nodes to run on (not suppored on all mpiruns)\n";
    print "      -E <VAR1[,VAR2...]>   list of environment vars to propagate\n";
    print "      -v                    be verbose about what is happening\n";
    print "      -t                    test only, don't execute anything (implies -v)\n";
    print "      --                    ends option parsing\n";
    exit 1;
}

sub stripouterquotes {
    my ($val) = @_;
    while ( $val =~ s/['"](.*?)['"]/$1/ ) { }
    return $val;
}

# "Multiply" array(s) for mapping procs to nodes
sub expand {
  my $ppn = int($numproc / $numnode);
  my $full = $numproc - $numnode * $ppn;  # nodes carrying ($ppn + 1) procs
  my $part = $numnode - $full;       # nodes carrying $ppn procs
                                                                                                              
  while (my $arr_ref = shift @_) {
    my @tmp = ();
    for (my $i = 0; $i < $full; ++$i) {
      my $elem = shift @$arr_ref;
      for (my $j = 0; $j <= $ppn; ++$j) { push @tmp, $elem; }
    }
    for (my $i = 0; $i < $part; ++$i) {
      my $elem = shift @$arr_ref;
      for (my $j = 0; $j < $ppn; ++$j) { push @tmp, $elem; }
    }
    @$arr_ref = @tmp;
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
	    $numproc = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numproc >= 1;
	} elsif ($_ =~ /^(-np?)([0-9]+)$/) {
	    $numproc = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numproc >= 1;
	} elsif ($_ eq '-N') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numnode = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numnode >= 1;
	} elsif ($_ =~ /^(-N)([0-9]+)$/) {
	    $numnode = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numnode >= 1;
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
    if (!defined($numproc) && $spawncmd =~ /%N/) {
	usage "Required option -n was not given\n";
    }

# Validate -N as needed
    if (defined($numnode) && !$is_lam) {
	warn "WARNING: Don't know how to control process->node layout with your mpirun\n";
	warn "WARNING: PROCESS LAYOUT MIGHT NOT MATCH YOUR REQUEST\n";
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
        die("gasnetrun: unable to locate program '$exebase'\n")
		    unless (defined($exename) && -x $exename);
        print("gasnetrun: located executable '$exename'\n") if ($verbose);
    } else {
        $exename = $exebase;
    }

# We need to gather a list of important environment variables
    # Form a list of the vars given by -E, plus any GASNET_* vars
    $ENV{"GASNET_VERBOSEENV"} = "1" if ($verbose);
    my @envvars = ((grep {+exists($ENV{$_})} split(',', $envlist)),
		   (grep {+m/^GASNET_/} keys(%ENV)));

# Build up the environment-passing arguments in several steps
    my @envargs = @envvars;
    if (@envvars) {
        # pair the variables with their values if desired
        if (defined $envfmt{val}) {
	    my $q = $envfmt{val};
	    @envargs = map { "$_=$q$ENV{$_}$q" } @envargs;
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
	if (defined $envfmt{noenv}) {
	    @envargs = ();
	}
    }

    # Special case for the mpich spawner
    if ($is_mpich && !$is_mpich_nt) {
        # General approach: create a wrapper script for the rsh/ssh command invoked by MPICH
        # that glues on the correct environment variables in a way that won't disturb MPICH
        $tmpdir = "gasnetrun_mpi-temp-$$";
        mkdir ($tmpdir, 0777) or die "gasnetrun: cannot create \'$tmpdir\'";
	my @spawners = ('ssh', 'rsh'); # default is to create ssh and rsh capture scripts
                                       # always create them because MPICH-GM device overrides RSHCOMMAND
        my $realprog = undef;
        my $realprog_args = undef;
        # If we have a direct path to the MPICH spawn script, rewrite it and replace RSHCOMMAND
        # for the most robust spawner capture (because RSHCOMMAND is sometimes an absolute path)
        if ($spawncmd =~ /^\s*(\S+)/ && -x "$1" && `grep 'RSHCOMMAND=' "$1" 2> /dev/null` ne "") {
          my $mpirun_script = stripouterquotes($1);
          my $tmprun = "$tmpdir/mpirun-tmp";
          my $tmprsh = 'mpirun-rsh';
          open (MPIRUN, $mpirun_script) or die "gasnetrun: can't open '$mpirun_script' for reading\n";
          open (TMPRUN, ">$tmprun") or die "gasnetrun: can't open '$tmprun' for writing\n";
          print "gasnetrun: cloning '$mpirun_script' to '$tmprun'\n" if ($verbose);
          while (<MPIRUN>) {
            my $line = $_;
            if ($line =~ /^\s*RSHCOMMAND=(.+)$/) {
              $realprog = $1;
              $line =~ s/$realprog/"$tmprsh"/; 
              $realprog = stripouterquotes($realprog);
              $realprog =~ s/^(\S+)\s*(.*)$/$1/;
              $realprog_args = $2;
              $realprog = stripouterquotes($realprog);
            }
            print TMPRUN "$line";
          }
          close (MPIRUN);
          close (TMPRUN);
	  chmod 0700, $tmprun or die "gasnetrun: cannot \'chmod 0700, $tmprun\'";
	  unshift @tmpfiles, "$tmprun";
	  if (!($realprog =~ /^\//)) { # RSHCOMMAND is a relative path - get absolute
             chomp($realprog = `which "$realprog" 2> /dev/null` || $realprog);
          }
          if (! -x "$realprog") {
	    print "gasnetrun: warning: cannot find MPICH underlying spawner '$realprog'\n" if ($verbose);
            $realprog = `which "ssh" 2> /dev/null`; $realprog_args = undef;
          }
	  unshift @spawners, $tmprsh;
          $spawncmd =~ s#$mpirun_script#$tmprun#;
        } 
	my $args = join(' ',map { "\"\'$_\'\"" } @envargs);
	(my $degooped_exename = $exename) =~ s/#/\\#/g;
	(my $degooped_args = join(' ',map { "\'$_\'" } @envargs)) =~ s/#/\\#/g;
	foreach my $spawner (@spawners) {
          unless (defined $realprog) { chomp($realprog = `which "$spawner" 2> /dev/null`); }
	  if (! -x "$realprog") { # Can't find that spawner - Assume we're not using it
            print "gasnetrun: warning: cannot find \'$spawner\'\n" if ($verbose);
	    next;
  	  }

	  my $tmpfile = "$tmpdir/$spawner";
	  unshift @tmpfiles, "$tmpfile";
          print "gasnetrun: building '$tmpfile' to wrap '$realprog'\n" if ($verbose);
	  open (TMPSPAWN, ">$tmpfile") or die "gasnetrun: cannot open $tmpfile";
	  print TMPSPAWN <<EOF;
#!/bin/sh

for arg in "\$@" ; do 
  #echo \$arg
  shift
  if test "\$arg" = "$exename" ; then 
    # prog name appears alone as an argument - prepend our env command
    set -- "\$@" $args "\'\$arg\'"
  elif test "\`echo \"\$arg\" | grep \"$exename\" ; exit 0\`"; then
    # prog name appears embedded in a larger quoted argument (eg mpich_gm/rsh)
    # keep it as one large arg and insert our env call
    newarg=`echo \"\$arg\" | sed \"s#$degooped_exename#$degooped_args \'$degooped_exename\'#\"`
    set -- "\$@" "\$newarg"
  else
    set -- "\$@" "\$arg"
  fi
done

if test "$verbose" != "0" ; then
  echo \$0 executing command:
  echo '$realprog' $realprog_args "\$@"
fi
  exec '$realprog' $realprog_args "\$@"
EOF
	  close(TMPSPAWN);
	  chmod 0700, $tmpfile or die "gasnetrun: cannot \'chmod 0700, $tmpfile\'";
          $realprog = undef;
          $realprog_args = undef;
   	}	
	$ENV{PATH} = "$tmpdir:$ENV{PATH}";
	@envargs = ();
     }
    
# Exec it
    my @spawncmd = map {  if ($_ eq '%N') {
			      if ($is_lam && $numnode) {
				  my @tmp = (0..($numnode-1));
				  expand \@tmp;
				  ($numproc, 'n' . join(',', @tmp));
			      } else {
				  $numproc;
			      }
			  } elsif ($_ eq '%P') {
                              (@envargs, $exename);
                          } elsif ($_ eq '%A') {
			      (@ARGV);
                          } elsif ($_ eq '%V') {
			      $verbose?("-v"):();
			  } else {
                              $_;
                          }
			} split(" ", $spawncmd);
    print("gasnetrun: running: ", join(' ', @spawncmd), "\n")
	if ($verbose);
    exit(0) if ($dryrun);

    if (1 && defined $tmpdir) {
	system(@spawncmd);
	foreach (@tmpfiles) {
	    unlink "$_" or die "gasnetrun: failed to unlink \'$_\'";
	}
	rmdir $tmpdir or die "gasnetrun: failed to rmdir \'$tmpdir\'";
    } else {
	exec(@spawncmd);
	die "gasnetrun: exec failed: $!\n";
    }
__END__
