#!/usr/bin/perl

# use strict 'refs';
# use warnings;
use Socket;
use Sys::Hostname;
use Cwd;

use constant MAX_RECV_LEN => 65536;

srand;

###############
#             #
#  Variables  #
#             #
###############

$verbose = 0;
$pwd = &Cwd::cwd();
$np = 1;
$lamexec = '/usr/bin/lamexec';
$lamboot = '/usr/bin/lamboot';
$wwnodes = '/opt/warewulf/bin/wwnodes';
$gmlib_path = '/usr/local/lib';

$dry_run = 0;
$pid_socket = 1;
$magic = int (rand (9999999));
$lamboot_magic = "/tmp/lamboot.$magic";
$local_host = hostname;
$local_port1 = '8000';
$local_port2 = '';

# GASNet/GM stuff
$trace = '';
$fh_M = '';
$fh_maxvictim = '';

#cannonize program
sub find_program {
  my ($prog) = @_;

  if ($prog =~ m|^/|) {
  } elsif ($prog =~ m|/|) {
    $prog = $pwd."/".$prog;
  } else {
    if (-x $prog) {
      $prog = $pwd."/".$prog;
    } else {
      foreach (split (/:/, $ENV{PATH})) {
	if (-x "$_/$prog") {
	  $prog = "$_/$prog";
	  last;
	}
      }
    }
  }

  -e $prog or die "$prog not found !\n";
  -x $prog or die "$prog is not executable !\n";

  print "Program binary is: $prog\n" if $verbose;
  return $prog;
}

sub usage {
  if ($_[0] ne '') {
    print (STDERR "Error in gasnetrun: @_\n\n");
  }

  print (STDERR "Usage:  gasnetrun [options] [-np <n>] prog [flags]\n");
  print (STDERR "   -v                 Verbose output.\n");
  print (STDERR "   -t                 Dry run.\n");
  print (STDERR "   --trace <file>     GASNet trace file.\n");
  print (STDERR "   --fh-M <n>         Firehose M parameter (suffix can be K,M or G).\n");
  print (STDERR "   --fh-maxvictim <n> Firehose maxvictim parameter (suffix can be K,M or G).\n\n");
  print (STDERR "   -np <n>            Specifies the number of processes.\n");
  print (STDERR "   prog [flags]       Specifies which command line to run.\n");
  exit (1);
}

#set the current dir
if (defined ($ENV {'PWD'})) {
  my @P = stat($ENV {'PWD'}."/.");
  my @p = stat(".");
  if ($p[0] == $P[0] && $p[1] == $P[1]) {
    $pwd = $ENV{'PWD'};
  }
}

#####################
#                   #
#   Args parsing    #
#                   #
#####################


while (@ARGV > 0) {
  $_ = $ARGV[0];

  if ($_ eq '-v') {
    $verbose = 1;
  } elsif ($_ eq '-t') {
    $dry_run = 1;
  } elsif ($_ eq '--fh-M') {
    shift;
    $fh_M = $ARGV[0];
  } elsif ($_ eq '--fh-maxvictim') {
    shift;
    $fh_maxvictim = $ARGV[0];
  } elsif ($_ eq '--trace') {
    shift;
    $trace = $ARGV[0];
  } elsif ($_ eq '-wd') {
    shift;
    usage ("No working directory specified (-wd) !") unless @ARGV >= 1;
    $wdir = $ARGV[0];
  } elsif ($_ eq '-np') {
    shift;
    if ((@ARGV == 0) || !($ARGV[0] =~ /^(\s*)(\d+)$/)) {
      usage ("Bad number of processes (-np) !");
    }
    $np = $ARGV[0];
  } elsif (($_ eq '-help') || ($_ eq '--help') || ($_ eq '-h')) {
    usage ('');
  } elsif ($_ eq '-mvback' ) {
  } elsif ($_ eq '-mvhome' ) {
  } elsif (/^-/) {
    usage ("Unknown option ($_) !");
  } else {
    @app = (find_program ($ARGV[0]), @ARGV[1..$#ARGV]);
    last;
  }
  shift;
}

@app || %app or usage (" Missing program name !");



# If the machine file is not defined, use the system-wide one.
$machine_file = $gstat_machinefile unless defined ($machine_file);

# If the machine file is not an absolute path, add the current directory.
#$machine_file = $pwd."/".$machine_file if !($machine_file =~ m|^/|);


# Print the settings if verbose.
if ($verbose) {
  print ("Dry-run mode enabled (Testing).\n") if $dry_run;
  print ("GASNet/GM tracefile is $trace.\n") if $trace;
  print ("No GASNet/GM tracefile is created.\n") if !$trace;
  printf ("Firehose M parameter is %s.\n", $fh_M ? $fh_M : "determined at bootstrap");
  printf ("Firehose maxvictim parameter is %s.\n", 
      $fh_maxvictim ? $fh_maxvictim : "determined at bootstrap");
  print ("Set working directory to $wdir.\n") if (defined ($wdir));
  print ("$np processes will be spawned: \n") if (!defined ($procgroup_file));
}


if (!defined ($wdir)) {
  $wdir = $pwd;
}

# Open 2 sockets with the first available ports.
if (!$dry_run) {
  print ("Open a socket on $local_host...\n") if $verbose;
  socket (FIRST_SOCKET, AF_INET, SOCK_STREAM, getprotobyname ('tcp'))
    or die ("First socket creation failed: $!\n");
  setsockopt (FIRST_SOCKET, SOL_SOCKET, SO_REUSEADDR, 1)
    or warn ("Error setting first socket option: $!\n");

  while (!(bind (FIRST_SOCKET, sockaddr_in ($local_port1, INADDR_ANY)))
	 && ($local_port1 < 20000)) {
    $local_port1 += 1;
  }
  if ($local_port1 < 20000) {
    print ("Got a first socket opened on port $local_port1.\n") if $verbose;
    listen (FIRST_SOCKET, SOMAXCONN)
      or die ("Error when listening on first socket: $!\n");
  } else {
    die ("Unable to open a socket on $local_host !\n");
  }

  $local_port2 = $local_port1 + 1;
  socket (SECOND_SOCKET, AF_INET, SOCK_STREAM, getprotobyname ('tcp'))
    or die ("Second socket creation failed: $!\n");
  setsockopt (SECOND_SOCKET, SOL_SOCKET, SO_REUSEADDR, 1)
    or warn ("Error setting second socket option: $!\n");

  while (!(bind (SECOND_SOCKET, sockaddr_in ($local_port2, INADDR_ANY)))
	 && ($local_port2 < 20000)) {
    $local_port2 += 1;
  }
  if ($local_port2 < 20000) {
    print ("Got a second socket opened on port $local_port2.\n") if $verbose;
    listen (SECOND_SOCKET, SOMAXCONN)
      or die ("Error when listening on second socket: $!\n");
  } else {
    die ("Unable to open a socket (2) on $local_host !\n");
  }
}


if (!$dry_run) {
  $pid_socket = fork;
  if ($pid_socket == 0) {
    # Gather the information from all remote processes via sockets.

    $index = $np;
    while ($index > 0) {
      accept (INCOMING_SOCKET, FIRST_SOCKET);
      recv (INCOMING_SOCKET, $incoming_data, MAX_RECV_LEN, 0);

      if ($incoming_data !~ /^<<<(\d+):(\d+):(\d+):(\d):(\d+):(\d+)>>>$/) {
        warn ("Received invalid data format !\n");
        close (INCOMING_SOCKET);
        next;
      }

      # Check the magic number.
      if ($1 != $magic) {
	warn ("Received bad magic number !\n");
	close (INCOMING_SOCKET);
	next;
      }

      if ($2 > $np) {
	terminator;
	die "GASNet Id received is out of range ($2 over $np)\n";
      }

      if ($3 == 0) {
	terminator;
	die "GASNet Id $2 was unable to open a GM port.)\n";
      }
	
      if (defined ($port_ids[$2])) {
	warn ("Ignoring message from the GASNet Id $2 ($_) !\n");
	close (INCOMING_SOCKET);
	next;
      }

      $port_ids[$2] = $3;
      $board_ids[$2] = $4;
      $node_ids[$2] = $5;
      $remote_pids[$2] = $6;
      $index--;
      close (INCOMING_SOCKET);

      if ($verbose) {
	print ("GASNet Id $2 is using GM port $3, board $4, GM_id $5.\n");
      }
    }

    close (FIRST_SOCKET);
    print ("Received data from all $np GASNet processes.\n") if $verbose;

	
    # Build the Port ID/Board ID mapping.
    $global_mapping = '[[[';
    for ($i=0; $i<$np; $i++) {
      $global_mapping .= 
	'<'.$port_ids[$i].':'.$board_ids[$i].':'.$node_ids[$i].'>';
    }
    $global_mapping .= ']]]';


    # Send the Port ID/Board ID mapping to all remote processes.
    $index = $np;
    while ($index > 0) {
      accept (INCOMING_SOCKET, SECOND_SOCKET);
      recv (INCOMING_SOCKET, $incoming_data, MAX_RECV_LEN, 0);

      if ($incoming_data !~ /^<->(\d+):(\d+)<->$/) {
	warn ("Received invalid data format !\n");
	close (INCOMING_SOCKET);
	next;
      }

      # Check the magic number.
      if ($1 != $magic) {
	warn ("Received bad magic number !\n");
	close (INCOMING_SOCKET);
	next;
      }

      if ($2 > $np) {
	terminator;
	die "GASNet Id received is out of range ($2 over $np)\n";
      }

      if ($port_ids[$2] == 0) {
	warn ("Ignoring message from the GASNet Id $2 ($_) !\n");
	close (INCOMING_SOCKET);
	next;
      }

      print ("Send mapping to GASNet Id $2.\n") if $verbose;

      send (INCOMING_SOCKET, "$global_mapping", 0);
      close (INCOMING_SOCKET);

      $port_ids[$2] = 0;
      $index--;
    }
    alarm (0);
    print ("Data sent to all processes.\n") if $verbose;

      exit (0);
    }
}

# LAM sets some of the ID and NP variables
$ENV{'GASNETGM_ID'} = -1;	# overridden by LAMRANK
$ENV{'GASNETGM_NP'} = -1;	# overridden by LAMWORLD
$ENV{'GASNETGM_MAGIC'} = $magic;
$ENV{'GASNETGM_BOARD'} = -1;	# No multi-board support
$ENV{'GASNET_TRACEFILE'} = $trace if $trace;
$ENV{'GASNETGM_FIREHOSE_M'} = $fh_M if $fh_M;
$ENV{'GASNETGM_FIREHOSE_MAXVICTIM'} = $fh_maxvictim if $fh_maxvictim;
$ENV{'GASNETGM_MASTER'} = $local_host;
$ENV{'GASNETGM_PORT1'}= $local_port1;
$ENV{'GASNETGM_PORT2'}= $local_port2;
$ENV{'LD_LIBRARY_PATH'} .= ":$gmlib_path";

# Run lamboot
system("$wwnodes --mpi >$lamboot_magic");
die "Can't run $wwnodes" unless !$?;
system("$lamboot $lamboot_magic 2>&1 >/dev/null");
die "Can't run $lamboot" unless !$?;

my $cmdline = '';
for ($k=0; $k<scalar (@app); $k++) {
	$cmdline .= $app[$k].' ';
}

my $varenv = '';
foreach (keys %ENV) {
	next unless /^(GASNET|LD_LIBRARY_PATH)/;
	$varenv .= $_ . "=" . $ENV{$_} . ",";
}

my $nplam = "n0";
$nplam .= "-" . ($np - 1) if ($np > 1);

print  "$lamexec $nplam -x $varenv $cmdline", "\n" if $verbose;
system "$lamexec $nplam -x $varenv $cmdline";

$index = $np;
while (1) {
  $next_pid = wait;
  if ($next_pid == -1 || $next_pid == $pid_socket) {
    print ("All remote GASNet processes have exited.\n") if $verbose;
    terminator;
    exit 0;
  } else {
    # the process waiting for an Abort has exited, so let's aborting
    terminator;
    exit 0;
  }
}
exit 0;
