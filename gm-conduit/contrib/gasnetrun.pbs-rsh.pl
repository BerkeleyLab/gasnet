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
$delay_rexec = 0;
$np = 1;
$rexec = "ssh";
$arch = "LINUX";
$varenv = '';
$dry_run = 0;
$kill_time = 1;
$totalview = 0;
$totalview_cmd = $ENV{'TOTALVIEW'} || 'totalview';
$timeout = 0;
$eager = 0;
$close_stdin = 0;
$pid_socket = 1;
$pid_rexec = 1;
#$default_machinefile = "/usr/local/mpich/1.2.4..8a/gm-1.5.2.1_Linux-2.4.18-10custom/smp/pgi/ssh/share/machines.ch_gm.$arch";
$default_machinefile = $ENV{'PBS_NODEFILE'};
$magic = int (rand (9999999));
$local_host = hostname;
$local_port = '8000';

# GASNet/GM stuff
$trace = '';
$fh_M = '';
$fh_maxvictim = '';

if ($rexec =~ /ssh/) {
  $ssh_reaper = 1;
} else {
  $ssh_reaper = 0;
}

###################
#                 #
#  Sub functions  #
#                 #
###################

sub clean_up {
  # reap remote processes, usefull because ssh is broken and does not
  # clean up remote processes when killed.
  if (($pid_socket == 0) && ($ssh_reaper)) {
    print ("Reap remote processes:\n") if $verbose;
    for ($z=0; $z<$np; $z++) {
      if (defined ($remote_pids[$z])) {
	$pid_reaper = fork;
	if ($pid_reaper == 0) {
	  if ($verbose) {
	    print ("\t$rexec -n $hosts[$z] kill -9 $remote_pids[$z] 2>/dev/null\n");
	  }
	  exec ($rexec, '-n', $hosts[$z], "kill -9 $remote_pids[$z]",
		"2>/dev/null");
	}
      }
    }

    while (wait != -1) {
      ;
    }
  }

  if (($pid_rexec != 0) && ($pid_socket != 0)) {
    if ($pid_socket > 1) {
      if (kill 0 => $pid_socket) {
	kill 'TERM', $pid_socket;
      }
    }
  }

}

sub terminator {
  print "Cleaning up all remaining processes.\n" if $verbose;
  if (($pid_rexec != 0) && ($pid_socket != 0)) {
    if (defined ($pids[0])) {
      foreach $p (@pids) {
	if (kill 0 => $p) {
	  kill 'TERM', $p;
	}
      }
    }
  }

  clean_up;
}

sub cleanup_SIGINT {
  if (($pid_rexec != 0) && ($pid_socket != 0)) {
    print ("Received SIGINT. Cleaning up...\n") if $verbose;
  }
  clean_up;
  exit (1);
}

sub cleanup_SIGTERM {
  if (($pid_rexec != 0) && ($pid_socket != 0)) {
    print ("Received SIGTERM. Cleaning up...\n") if $verbose;
  }
  clean_up;
  exit (1);
}

sub cleanup_SIGKILL {
  if (($pid_rexec != 0) && ($pid_socket != 0)) {
    print ("Received SIGKILL. Cleaning up...\n") if $verbose;
  }
  clean_up;
  exit (1);
}

sub cleanup_SIGQUIT {
  if (($pid_rexec != 0) && ($pid_socket != 0)) {
    print ("Received SIGQUIT. Cleaning up...\n") if $verbose;
  }
  clean_up;
  exit (1);
}

sub cleanup_ALARM {
  print ("Received SIGALRM. Cleaning up...\n") if $verbose;
  terminator;
  exit (1);
}

sub cleanup_TIMEOUT {
  print ("Timeout: still waiting for data from remote GASNet processes !\n");
  print ("Timeout: cleaning up...\n");
  terminator;
  exit (1);
}


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

  print (STDERR "Usage:\n \t gasnetrun [options] [-np <n>] prog [flags]\n");
  print (STDERR "   -v   Verbose - provide additional details of the script's execution.\n");
  print (STDERR "   -t   Testing - do not actually run, just print what would be executed.\n");
  print (STDERR "   -s   Close stdin - can run in background without tty input problems.\n");
  print (STDERR "   -machinefile <file>   Specifies a machine file, default is\n");
  print (STDERR "                         $default_machinefile.\n");
  print (STDERR "   --trace <file>     GASNet trace file.\n");
  print (STDERR "   --fh-M <n>         Firehose M parameter (suffix can be K,M or G).\n");
  print (STDERR "   --fh-maxvictim <n> Firehose maxvictim parameter (suffix can be K,M or G).\n");
  print (STDERR "   --gm-wait <n>      Wait <n> seconds between each spawning step.\n");
  print (STDERR "   --gm-kill <n>      Kill all processes <n> seconds after the first exits.\n");
  print (STDERR "   -totalview         Specifies Totalview debugging session.\n");
  print (STDERR "   -pg <file>         Specifies the procgroup file.\n");
  print (STDERR "   -wd <path>         Specifies the working directory.\n");
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
  } elsif ($_ eq '-s') {
    $close_stdin = 1;
  } elsif ($_ eq '-machinefile') {
    shift;
    usage ("No machine file specified (-machinefile) !") unless @ARGV >= 1;
    $machine_file = $ARGV[0];
  } elsif ($_ eq '--gm-wait') {
    shift;
    usage ("No waiting time specified (--gm-wait) !") unless @ARGV >= 1;
    $delay_rexec = $ARGV[0];
  } elsif ($_ eq '--gm-kill') {
    shift;
    if ((@ARGV == 0) && ($ARGV[0] !~ /^\d+$/)) {
      usage ("No termination delay specified (--gm-kill) !");
    }
    $kill_time = $ARGV[0];
  } elsif ($_ eq '--fh-M') {
    shift;
    $fh_M = $ARGV[0];
  } elsif ($_ eq '--fh-maxvictim') {
    shift;
    $fh_maxvictim = $ARGV[0];
  } elsif ($_ eq '--trace') {
    shift;
    $trace = $ARGV[0];
  } elsif (($_ eq '-totalview') || ($_ eq '-tv')) {
    $totalview = 1;
  } elsif ($_ eq '-pg') {
    shift;
    usage ("No procgroup file specified (-pg) !") unless @ARGV >= 1;
    $procgroup_file = $ARGV[0];
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
  } elsif (/=/) {
    $varenv .= " $ARGV[0]";
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
$machine_file = $default_machinefile unless defined ($machine_file);

# If the machine file is not an absolute path, add the current directory.
$machine_file = $pwd."/".$machine_file if !($machine_file =~ m|^/|);


# Print the settings if verbose.
if ($verbose) {
  print ("Dry-run mode enabled (Testing).\n") if $dry_run;
  print ("Machines file is $machine_file\n");
  print ("Delay of $delay_rexec between spanwing steps.\n") if $delay_rexec;
  print ("Processes will be killed $kill_time after first exits.\n") if $kill_time;
  print ("GASNet/GM tracefile is $trace\n.") if $trace;
  print ("No GASNet/GM tracefile is created.\n") if !$trace;
  printf ("Firehose M parameter is %s.\n", $fh_M ? $fh_M : "determined at bootstrap");
  printf ("Firehose maxvictim parameter is %s.\n", 
      $fh_maxvictim ? $fh_maxvictim : "determined at bootstrap");
  print ("Use Totalview for debugging session.\n") if $totalview;
  print ("Set working directory to $wdir.\n") if (defined ($wdir));
  print ("$np processes will be spawned: \n") if (!defined ($procgroup_file));
}


if (!defined ($wdir)) {
  $wdir = $pwd;
}

if (defined ($procgroup_file)) {
  # Open the procgroup file, read it and close it.
  open (PROCGROUP_FILE, "$procgroup_file")
    or die "Cannot open the procgroup file $procgroup_file: $!\n";
  @procgroup_file_data = <PROCGROUP_FILE>;
  close(PROCGROUP_FILE);

  # Extract the informations from the procgroup file.
  $np = 0;
  $line_number = 0;
  while (scalar (@procgroup_file_data)) {
    $line = shift (@procgroup_file_data);
    $line_number++;
    next if ($line =~ /^\s*$/);
    next if ($line =~ /^\#/);
    chomp ($line);

    if ($line =~ /^\S+\s+\d+\s*\S*\s*\S*/) {
      @fields = split (/\s+/, $line);
    } else {
      die "Bad line in $procgroup_file (line $line_number): \"$line\"";
    }

    if (scalar (@fields) < 2) {
      die "Bad line at $machine_file:$line_number): \"$line\"";
    }

    # Extract the hostname, the index and the executable (and maybe the login)
    $i = $fields[1];
    if ($np == 0) {
      $i++;
      if ($fields[0] eq "local") {
        $fields[0] = $local_host;
      }
      if (!defined ($fields[2])) {
        $fields[2] = $app[0];
      }
    }

    # sanity checks
    if (!defined ($fields[2])) {
      die "Missing progname in $procgroup_file (line $line_number): \"$line\"";
    }

    for ($j=0; $j<$i; $j++) {
      $hosts[$np] = $fields[0];
      $boards[$np] = -1;
      $apps[$np] = $fields[2];
      if (defined ($fields[3])) {
        $logins[$np] = $fields[3];
      }
      $np++;
    }
  }
} else {
  # Open the machines file, read it and close it.
  open (MACHINE_FILE, "$machine_file")
    or die "Cannot open the machines file $machine_file: $!\n";
  @machine_file_data = <MACHINE_FILE>;
  close(MACHINE_FILE);

  # Extract the informations from the machines file.
  $i = 0;
  $line_number = 0;
  while ($i<$np) {
    $line = shift (@machine_file_data);
    push (@machine_file_data, $line);
    $line_number++;
    next if ($line =~ /^\s*$/);
    next if ($line =~ /^\#/);
    chomp ($line);

    if ($line =~ /^\S+\s*\d*\s*$/) {
      @fields = split (/\s+/, $line);
    } else {
      die "Bad line in $machine_file (line $line_number): \"$line\"";
    }

    # Extract the board number if present.
    if (scalar (@fields) > 1) {
      if ($fields[1] =~ /^\d$/) {
	$board_id = $fields[1];
      } else {
	die "Bad board number at $machine_file:$line_number): \"$line\"";
      }
    } else {
      $board_id = -1;
    }

    # Extract the host name and eventually the number of processors.
    if ($fields[0] =~ /^(\S+):(\d+)$/) {
      if ($2 < 1) {
	die "Bad counts in $machine_file (line $line_number): \"$line\"";
      }

      for ($j=0; $j<$2; $j++) {
	$hosts[$i] = $1;
	$boards[$i] = $board_id;
	
	$apps[$i] = '';
	for ($k=0; $k<scalar (@app); $k++) {
	  $apps[$i] .= $app[$k].' ';
	}
	$i++;
      }
    } else {
      $hosts[$i] = $fields[0];
      $boards[$i] = $board_id;

      $apps[$i] = '';
      for ($k=0; $k<scalar (@app); $k++) {
	$apps[$i] .= $app[$k].' ';
      }
      $i++;
    }
  }
}

# Print the configuration.
if ($verbose) {
  for ($i=0; $i<$np; $i++) {
    if ($boards[$i] >= 0) {
      print ("\tProcess $i ($apps[$i]) on $hosts[$i] and board $boards[$i]\n");
    } else {
      print ("\tProcess $i ($apps[$i]) on $hosts[$i]\n");
    }
  }
}


# Open 2 sockets with the first available ports.
if (!$dry_run) {
  print ("Open a socket on $local_host...\n") if $verbose;
  socket (FIRST_SOCKET, AF_INET, SOCK_STREAM, getprotobyname ('tcp'))
    or die ("First socket creation failed: $!\n");
  setsockopt (FIRST_SOCKET, SOL_SOCKET, SO_REUSEADDR, 1)
    or warn ("Error setting first socket option: $!\n");

  while (!(bind (FIRST_SOCKET, sockaddr_in ($local_port, INADDR_ANY)))
	 && ($local_port < 20000)) {
    $local_port += 1;
  }
  if ($local_port < 20000) {
    print ("Got a first socket opened on port $local_port.\n") if $verbose;
    $varenv .= " GASNETGM_MASTER=$local_host GASNETGM_PORT1=$local_port";
    listen (FIRST_SOCKET, SOMAXCONN)
      or die ("Error when listening on first socket: $!\n");
  } else {
    die ("Unable to open a socket on $local_host !\n");
  }

  $local_port += 1;
  socket (SECOND_SOCKET, AF_INET, SOCK_STREAM, getprotobyname ('tcp'))
    or die ("Second socket creation failed: $!\n");
  setsockopt (SECOND_SOCKET, SOL_SOCKET, SO_REUSEADDR, 1)
    or warn ("Error setting second socket option: $!\n");

  while (!(bind (SECOND_SOCKET, sockaddr_in ($local_port, INADDR_ANY)))
	 && ($local_port < 20000)) {
    $local_port += 1;
  }
  if ($local_port < 20000) {
    print ("Got a second socket opened on port $local_port.\n") if $verbose;
    $varenv .= " GASNETGM_PORT2=$local_port";
    listen (SECOND_SOCKET, SOMAXCONN)
      or die ("Error when listening on second socket: $!\n");
  } else {
    die ("Unable to open a socket (2) on $local_host !\n");
  }
}


# put options on varenv command line
$varenv .= " GASNET_TRACEFILE=$trace" if $trace;
$varenv .= " GASNETGM_FIREHOSE_M=$fh_M" if $fh_M;
$varenv .= " GASNETGM_FIREHOSE_MAXVICTIM=$fh_maxvictim" if $fh_maxvictim;

$SIG{'INT'} = 'cleanup_SIGINT';
$SIG{'TERM'} = 'cleanup_SIGTERM';
$SIG{'KILL'} = 'cleanup_SIGKILL';
$SIG{'QUIT'} = 'cleanup_SIGQUIT';

if (!$dry_run) {
  $pid_socket = fork;
  if ($pid_socket == 0) {
    # Gather the information from all remote processes via sockets.
    $SIG{'ALRM'} = 'cleanup_TIMEOUT';
    alarm ($timeout);

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

    # Keep the second socket opened for abort messages.
    while (1) {
      accept (ABORT_SOCKET, SECOND_SOCKET);
      recv (ABORT_SOCKET, $incoming_data, MAX_RECV_LEN, 0);

      if ($incoming_data !~ /^<<<ABORT_(\d+)_ABORT>>>$/) {
        print ("Received spurious abort message, keep listening...\n");
        close (ABORT_SOCKET);
        next;
      }

      if ($1 != $magic) {
	print ("Received bad magic number in abort message!\n");
	close (ABORT_SOCKET);
	next;
      }

      close (ABORT_SOCKET);
      close (SECOND_SOCKET);
      print ("Received valid abort message !\n") if $verbose;
      exit (0);
    }
  }
}


# Spawn remote processes.
for ($i=0; $i<$np; $i++) {
  $pid_rexec = fork;
  if ($pid_rexec == 0) {
    $SIG{'INT'} = 'DEFAULT';
    $SIG{'TERM'} = 'DEFAULT';
    $SIG{'KILL'} = 'DEFAULT';
    $SIG{'QUIT'} = 'DEFAULT';

    $varenv .= " GASNETGM_MAGIC=$magic";
    $varenv .= " GASNETGM_ID=$i";
    $varenv .= " GASNETGM_NP=$np";
    $varenv .= " GASNETGM_BOARD=$boards[$i]";

    if (defined ($logins[$i])) {
      $login = 1;
      $login_id = $logins[$i];
    } else {
      $login = 0;
    }

    if ($totalview) {
      if ($i == 0) {
	$cmdline = "cd $wdir ; env $varenv $totalview_cmd $apps[$i] -a -mpichtv";
      } else {
	$cmdline = "cd $wdir ; env $varenv $totalview_cmd $apps[$i] -mpichtv";
      }
    } else {
      $cmdline = "cd $wdir ; env $varenv $apps[$i]";
    }

    if ($dry_run) {
      if (($i == 0) && (!$close_stdin)) {
	if ($login) {
	  print ("$rexec -l $login_id $hosts[$i] $login $cmdline\n");
	} else {
	  print ("$rexec $hosts[$i] $login $cmdline\n");
	}
      } else {
	if ($login) {
	  print ("$rexec -n -l $login_id $hosts[$i] $login $cmdline\n");
	} else {
	  print ("$rexec -n $hosts[$i] $login $cmdline\n");
	}
      }
      exit (0);
    } else {
      if (($i == 0) && (!$close_stdin)) {
	if ($login) {
	  print ("$rexec -l $login_id $hosts[$i] $cmdline\n") if ($verbose);
	  exec ($rexec, '-l', $login_id, $hosts[$i],$cmdline);
	} else {
	  print ("$rexec $hosts[$i] $cmdline\n") if ($verbose);
	  exec ($rexec, $hosts[$i],$cmdline);
	}
      } else {
	if ($login) {
	  print ("$rexec -n -l $login_id $hosts[$i] $cmdline\n") if ($verbose);
	  exec ($rexec, '-n', '-l', $login_id, $hosts[$i], $cmdline);
	} else {
	  print ("$rexec -n $hosts[$i] $cmdline\n") if ($verbose);
	  exec ($rexec, '-n', $hosts[$i], $cmdline);
	}
      }
    }
    exit (0);
  } else {
    $pids[$i] = $pid_rexec;
    sleep ($delay_rexec) if ($delay_rexec);
  }
}

# If dry_run, there is nothing more to do.
if ($dry_run) {
  while (wait != -1) {
    ;
  }
  exit (0);
}

# Wait and eventually kill remaining processes;
if ($kill_time) {
  $first_pid = wait;
  if ($first_pid == -1) {
    clean_up;
    exit 0;
  }

  if ($first_pid == $pid_socket) {
    terminator;
    exit 0;
  }

  if ($verbose) {
    for ($i=0; $i<$np; $i++) {
      if ($first_pid == $pids[$i]) {
	print ("GASNet Process $i has exited, wait $kill_time seconds and kill all remaining processes...\n") if $verbose;
	last;
      }
    }
  }

  $SIG{'ALRM'} = 'cleanup_ALARM';
  alarm ($kill_time + 1);
}

$index = $np;
while (1) {
  $next_pid = wait;
  if ($next_pid == -1) {
    print ("All remote GASNet processes have exited.\n") if $verbose;
    terminator;
    exit 0;
  }

  if ($next_pid != $pid_socket) {
    $index--;
    if ($index == 0) {
      print ("All remote GASNet processes have exited.\n") if $verbose;
      terminator;
      exit 0;
    }
  } else {
    # the process waiting for an Abort has exited, so let's aborting
    terminator;
    exit 0;
  }
}
exit 0;
