dnl   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/acinclude.m4,v $
dnl     $Date: 2004/10/23 05:02:05 $
dnl $Revision: 1.49 $
dnl Description: m4 macros
dnl Copyright 2004,  Dan Bonachea <bonachea@cs.berkeley.edu>
dnl Terms of use are as specified in license.txt

dnl determine the autoconf version used to build configure script 
AC_DEFUN([GASNET_GET_AUTOCONF_VERSION],[
AC_REQUIRE([AC_PROG_AWK])
AC_MSG_CHECKING(autoconf version)
dnl AUTOCONF_VERSION=`cat ${srcdir}/configure | perl -e '{ while (<STDIN>) { if (m/enerated.*utoconf.*([[0-9]]+)\.([[0-9]]+).*/) { print "[$]1.[$]2\n"; exit 0 } } }'`
AUTOCONF_VERSION_STR=`cat ${srcdir}/configure | $AWK '/.*enerated.*utoconf.*([[0-9]]+).([[0-9]]+).*/ { [match]([$]0,"[[0-9]]+.[[0-9]]+"); print [substr]([$]0,RSTART,RLENGTH); exit 0 } '`
AUTOCONF_VERSION=`echo $AUTOCONF_VERSION_STR | $AWK -F. '{ printf("%i%i",[$]1,[$]2); }'`
AC_MSG_RESULT($AUTOCONF_VERSION_STR)
])

dnl GASNET_GCC_VERSION_CHECK(type)  type=CC or CXX
AC_DEFUN([GASNET_GCC_VERSION_CHECK],[
AC_MSG_CHECKING(for known buggy compilers)
badgccmsg=""
AC_TRY_COMPILE([
#if __GNUC__ == 2 && __GNUC_MINOR__ == 96 && __GNUC_PATCHLEVEL__ == 0
# error
#endif
],[ ], [:], [
AC_MSG_RESULT([$1] is gcc 2.96)
badgccmsg="Use of gcc/g++ 2.96 for compiling this software is strongly discouraged. \
It is not an official GNU release and has many serious known bugs, especially \
in the optimizer, which may lead to bad code and incorrect runtime behavior. \
Consider using \$[$1] to select a different compiler."
GASNET_IF_ENABLED(allow-gcc296, Allow the use of the broken gcc/g++ 2.96 compiler, [
  AC_MSG_WARN([$badgccmsg])
  ],[
  AC_MSG_ERROR([$badgccmsg \
  You may enable use of this broken compiler at your own risk by passing the --enable-allow-gcc296 flag.])
])
])
AC_TRY_COMPILE([
#if __GNUC__ == 3 && __GNUC_MINOR__ == 2 && __GNUC_PATCHLEVEL__ <= 2
# error
#endif
],[ ], [:], [
AC_MSG_RESULT([$1] is gcc 3.2.0-2)
badgccmsg="Use of gcc/g++ 3.2.0-2 for compiling this software is strongly discouraged. \
This version has a serious known bug in the optimizer regarding structure copying, \
which may lead to bad code and incorrect runtime behavior when optimization is enabled. \
Consider using \$[$1] to select a different compiler."
GASNET_IF_ENABLED(allow-gcc32, Allow the use of the known broken gcc/g++ 3.2.0-2 compiler, [
  AC_MSG_WARN([$badgccmsg])
  ],[
  AC_MSG_ERROR([$badgccmsg \
  You may enable use of this broken compiler at your own risk by passing the --enable-allow-gcc32 flag.])
])
])
if test -z "$badgccmsg"; then
  AC_MSG_RESULT(ok)
fi
])

AC_DEFUN([GASNET_FIX_SHELL],[
AC_MSG_CHECKING(for good shell)
if test "$BASH" = '' && test `uname` = HP-UX; then
  AC_MSG_RESULT([no, switching to bash])
  case [$]# in
    0) exec bash - "[$]0"        ;;
    *) exec bash - "[$]0" "[$]@" ;;
  esac
else
  AC_MSG_RESULT(yes)
fi])

dnl find full pathname for a given header file, if it exists and AC_SUBST it
AC_DEFUN([GASNET_FIND_HEADER],[
AC_REQUIRE([AC_PROG_AWK])
AC_CHECK_HEADERS($1)
pushdef([lowername],patsubst(patsubst(patsubst([$1], [/], [_]), [\.], [_]), [-], [_]))
pushdef([uppername],translit(lowername,'a-z','A-Z'))
if test "$ac_cv_header_[]lowername" = "yes"; then
  AC_MSG_CHECKING(for location of $1)
  header_pathname=`echo "#include <$1>" > conftest.c ; $CPP conftest.c | grep $1 | head -1`
  header_pathname=`echo $header_pathname | $AWK '{ printf("%s",[$]3); }'`
  if test -z "$header_pathname"; then
    # IBM xlc doesn't always emit include file name in output: try /usr/include
    if test -r /usr/include/$1; then
        header_pathname="\"/usr/include/$1\""
    fi
  fi
  AC_MSG_RESULT($header_pathname)
  have=1
else
  header_pathname=
  have=0
fi
PATH_[]uppername=$header_pathname
HAVE_[]uppername=$have
AC_SUBST(PATH_[]uppername)
AC_SUBST(HAVE_[]uppername)
popdef([uppername])
popdef([lowername])
])

dnl do AC_CHECK_SIZEOF and also AC_SUBST the result
AC_DEFUN([GASNET_CHECK_SIZEOF],[
  pushdef([lowername],patsubst(patsubst([$1], [\ ], [_]), [\*], [p]))
  pushdef([uppername],translit(lowername,'a-z','A-Z'))

  AC_CHECK_SIZEOF($1, $2)
  SIZEOF_[]uppername=$ac_cv_sizeof_[]lowername
  AC_SUBST(SIZEOF_[]uppername)
  if test "$SIZEOF_[]uppername" = "0" ; then
    AC_MSG_ERROR(failed to find sizeof($1))
  fi

  popdef([lowername])
  popdef([uppername])
])

dnl GASNET_CHECK_INTTYPES(headername) 
dnl AC_DEFINE and set HAVE_HEADERNAME_H if the header exists
dnl AC_DEFINE and AC_SUBST COMPLETE_HEADERNAME_H if it contains all the inttypes 
dnl that we care about (all of which are mandated by C99 and POSIX!)
AC_DEFUN([GASNET_CHECK_INTTYPES],[
  AC_CHECK_HEADERS([$1])
  pushdef([lowername],patsubst(patsubst(patsubst([$1], [/], [_]), [\.], [_]), [-], [_]))
  pushdef([uppername],translit(lowername,'a-z','A-Z'))
  HAVE_[]uppername=$ac_cv_header_[]lowername
  GASNET_TRY_CACHE_RUN([for a complete $1],[COMPLETE_[]uppername],[
    #include <$1>
    int main() {
    	int8_t    i8;
    	uint8_t   u8;
    	int16_t  i16;
    	uint16_t u16;
    	int32_t  i32;
    	uint32_t u32;
    	int64_t  i64;
    	uint64_t u64;
    	intptr_t  ip;
    	uintptr_t up;
	if (sizeof(i8) != 1) return 1;
	if (sizeof(u8) != 1) return 1;
	if (sizeof(i16) != 2) return 1;
	if (sizeof(u16) != 2) return 1;
	if (sizeof(i32) != 4) return 1;
	if (sizeof(u32) != 4) return 1;
	if (sizeof(i64) != 8) return 1;
	if (sizeof(u64) != 8) return 1;
	if (sizeof(ip) != sizeof(void*)) return 1;
	if (sizeof(up) != sizeof(void*)) return 1;
        return 0;
    }
  ],[ 
    COMPLETE_[]uppername=1
    AC_SUBST(COMPLETE_[]uppername)
    AC_DEFINE(COMPLETE_[]uppername)
  ])
  popdef([lowername])
  popdef([uppername])
])

dnl Appends -Dvar_to_define onto target_var, iff var_to_define is set
dnl GASNET_APPEND_DEFINE(target_var, var_to_define)
AC_DEFUN([GASNET_APPEND_DEFINE],[
  if test "$[$2]" != ""; then
    [$1]="$[$1] -D[$2]"
  fi
]) 

dnl add file to list of executable outputs that should be marked +x
dnl would be nice to use AC_CONFIG_COMMANDS() for each file, but autoconf 2.53
dnl  stupidly fails to execute commands having the same tag as a config output file
dnl  on subsequent calls to config.status
AC_DEFUN([GASNET_FIX_EXEC],[
  cv_prefix[]exec_list="$cv_prefix[]exec_list $1"
])

dnl ensure the "default" command is run on every invocation of config.status
AC_DEFUN([GASNET_FIX_EXEC_SETUP],[[
  dnl round-about method ensure autoconf 2.53 picks up depfiles command
  if test "\${config_commands+set}" != set ; then
    config_commands="default"
  fi
  CONFIG_COMMANDS="\$config_commands"
  cv_prefix[]exec_list="$cv_prefix[]exec_list"
]])

AC_DEFUN([GASNET_FIX_EXEC_OUTPUT],[[
  for file in $cv_prefix[]exec_list; do
   case "$CONFIG_FILES" in
     *${file}*) chmod +x ${file} ;;
   esac
  done
]])

AC_DEFUN([GASNET_LIBGCC],[
AC_REQUIRE([AC_PROG_CC])
AC_CACHE_CHECK(for libgcc link flags, cv_prefix[]lib_gcc,
[if test "$GCC" = yes; then
  #LIBGCC="`$CC -v 2>&1 | sed -n 's:^Reading specs from \(.*\)/specs$:-L\1 -lgcc:p'`"
  LIBGCC="-L`$CC -print-libgcc-file-name | xargs dirname` -lgcc"
  if test -z "$LIBGCC"; then
    AC_MSG_ERROR(cannot find libgcc)
  fi
fi
cv_prefix[]lib_gcc="$LIBGCC"])
LIBGCC="$cv_prefix[]lib_gcc"
AC_SUBST(LIBGCC)
])

dnl GASNET_ENV_DEFAULT(envvar-name, default-value)
dnl  load an environment variable, using default value if it's missing from env.
dnl  caches the results to guarantee reconfig gets the originally loaded value
dnl  also adds a --with-foo-bar= option for the env variable FOO_BAR
AC_DEFUN([GASNET_ENV_DEFAULT],[
  pushdef([lowerdashname],patsubst(translit([$1],'A-Z','a-z'), _, -))
  pushdef([lowerscorename],patsubst(translit([$1],'A-Z','a-z'), -, _))

  AC_MSG_CHECKING(for $1 in environment)

  dnl create the help prompt just once
  ifdef(with_expanded_[$1], [], [
    AC_ARG_WITH(lowerdashname, 
       GASNET_OPTION_HELP(with-[]lowerdashname[]=, value for [$1]), 
      [], [])
  ])
  define(with_expanded_[$1], [set])

  envval_src_[$1]="cached"
  AC_CACHE_VAL(cv_prefix[]envvar_$1, [
      case "$[$1]" in
	'') if test "$with_[]lowerscorename" != ""; then
	      cv_prefix[]envvar_$1="$with_[]lowerscorename"
	      envval_src_[$1]=given
	    else
	      cv_prefix[]envvar_$1="[$2]"
	      envval_src_[$1]=default
	    fi 
	    ;;
	*)  cv_prefix[]envvar_$1="$[$1]"
	    envval_src_[$1]=given
      esac
  ])

  [$1]="$cv_prefix[]envvar_$1"
  case "$envval_src_[$1]" in
      'cached')
	  AC_MSG_RESULT([using cached value \"$[$1]\"]) ;;
      'default')
	  AC_MSG_RESULT([no, defaulting to \"$[$1]\"]) ;;
      'given')
	  AC_MSG_RESULT([yes, using \"$[$1]\"]) ;;
      *) AC_MSG_ERROR(_GASNET_ENV_DEFAULT broken)
  esac

  popdef([lowerdashname])
  popdef([lowerscorename])
])

dnl GASNET_RESTORE_AUTOCONF_ENV(env1 env2 env3) 
dnl  call at top of configure.in to restore cached environment variables 
dnl  inspected by autoconf macros. Pass in names of variables
AC_DEFUN([GASNET_RESTORE_AUTOCONF_ENV],[
  dnl  pushdef = get a variable prefix variable which won't be cached.
  pushdef([nc_prefix],patsubst(cv_prefix,_cv_,_))
  if test "$nc_prefix[]acenv_list" != ""; then
    AC_MSG_ERROR(_GASNET_RESTORE_AUTOCONF_ENV called more than once with prefix = "cv_prefix")
  fi
  nc_prefix[]acenv_list="$1"
  AC_MSG_CHECKING(for cached autoconf environment settings)
  AC_MSG_RESULT("") 
  for varname in $1; do
    val=`eval echo '$'"cv_prefix[]acenv_$varname"`
    if test "$val" != ""; then
      eval $varname=\"$val\"
      AC_MSG_RESULT([$varname=\"$val\"]) 
    fi
  done
  popdef([nc_prefix])
])

dnl GASNET_SAVE_AUTOCONF_ENV() 
dnl  cache the environment variables inspected by autoconf macros
AC_DEFUN([GASNET_SAVE_AUTOCONF_ENV],[
  for varname in $cv_prefix[]acenv_list; do
    val=`eval echo '$'"$varname"`
    if test "$val" != ""; then
      cachevarname=cv_prefix[]acenv_$varname
      eval $cachevarname=\"$val\"
    fi
  done
])

dnl m4 substr fiasco:
dnl autoconf 2.13 has a working version of the m4 function 'substr', 
dnl  but no m4_substr (and no format or m4_format)
dnl autoconf 2.58 has working versions of m4_substr and m4_format, 
dnl  but no substr or format
dnl This incantation ensures m4_substr works regardless
ifdef([substr],[define([m4_substr], defn([substr]))])

AC_DEFUN([GASNET_OPTION_HELP],[  --$1 ]m4_substr[([                         ],len([$1]))$2])

dnl provide a --with-foo=bar configure option
dnl action-withval runs for a named value in $withval (or withval=yes if named arg missing)
dnl action-without runs for --without-foo or --with-foo=no
dnl action-none runs for no foo arg given
dnl GASNET_WITH(foo, description, action-withval, [action-without], [action-none])
AC_DEFUN([GASNET_WITH],[
AC_ARG_WITH($1,GASNET_OPTION_HELP(with-$1=value,$2), [
  case "$withval" in
    no) :
        $4 ;;
    *)  $3 ;;
  esac
  ],[
   :
   $5
  ])
])

AC_DEFUN([GASNET_IF_ENABLED],[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  '' | no) :
      $4 ;;
  *)  $3 ;;
esac
])

AC_DEFUN([GASNET_IF_DISABLED],[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  '' | yes) :
       $4 ;;
  *)   $3 ;;
esac
])

AC_DEFUN([GASNET_IF_ENABLED_WITH_AUTO],[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,$2))
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  no)  $4 ;;
  yes) $3 ;;
  *)   $5 ;;
esac
])

AC_DEFUN([GASNET_SUBST],[
$1="$2"
AC_SUBST($1)])

AC_DEFUN([GASNET_SUBST_FILE],[
$1="$2"
AC_SUBST_FILE($1)])

AC_DEFUN([GASNET_CHECK_PROGS],[
case "$$1" in
  '') AC_CHECK_PROGS($1,$2)
      ;;
  *) AC_MSG_CHECKING(for $3)
     AC_MSG_RESULT($$1)
      ;;
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac])

AC_DEFUN([GASNET_PATH_PROGS],[
case "$$1" in
  '') AC_PATH_PROGS($1,$2)
      ;;
  *) AC_MSG_CHECKING(for $3)
     AC_MSG_RESULT($$1)
      ;;
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac])

dnl GASNET_GETFULLPATH(var)
dnl var contains a program name, optionally followed by arguments
dnl expand the program name to a fully qualified pathname if not already done
AC_DEFUN([GASNET_GETFULLPATH_CHECK],[
GASNET_IF_DISABLED(full-path-expansion, [Disable expansion of program names to full pathnames], 
                   [cv_prefix[]_gfp_disable=1])
])
AC_DEFUN([GASNET_GETFULLPATH],[
AC_REQUIRE([AC_PROG_AWK])
AC_REQUIRE([GASNET_GETFULLPATH_CHECK])
if test "$cv_prefix[]_gfp_disable" = ""; then
  gasnet_gfp_progname=`echo "$$1" | $AWK -F' ' '{ print [$]1 }'`
  gasnet_gfp_progargs=`echo "$$1" | $AWK -F' ' 'BEGIN { ORS=" "; } { for (i=2;i<=NF;i++) print $i; }'`
  gasnet_gfp_progname0=`echo "$gasnet_gfp_progname" | $AWK '{ print sub[]str([$]0,1,1) }'`
  if test "$gasnet_gfp_progname0" != "/" ; then
    cv_prefix[]_gfp_fullprogname_$1=
    AC_PATH_PROG(cv_prefix[]_gfp_fullprogname_$1, $gasnet_gfp_progname,[])
    AC_MSG_CHECKING(for full path expansion of $1)
    if test "$cv_prefix[]_gfp_fullprogname_$1" != "" ; then
      $1="$cv_prefix[]_gfp_fullprogname_$1 $gasnet_gfp_progargs"
    fi
    AC_MSG_RESULT($$1)
  fi
fi
])

dnl GASNET_CHECK_LIB(library, function, action-if-found, action-if-not-found, other-flags, other-libraries)
AC_DEFUN([GASNET_CHECK_LIB],[
GASNET_check_lib_old_ldflags="$LDFLAGS"
LDFLAGS="$LDFLAGS $5"
AC_CHECK_LIB($1, $2, $3, $4, $6)
LDFLAGS="$GASNET_check_lib_old_ldflags"])

dnl GASNET_TRY_RUNCMD(command, action-success-nooutput, action-success-output, action-error)
dnl run a command, and take action based on the result code and output (in $gasnet_cmd_stdout/$gasnet_cmd_stderr)
AC_DEFUN([GASNET_TRY_RUNCMD],[
  echo \"$1\" >&5
  ( $1 ) > conftest-runcmdout 2> conftest-runcmderr
  gasnet_cmd_result="$?"
  gasnet_cmd_stdout="`cat conftest-runcmdout`"
  gasnet_cmd_stderr="`cat conftest-runcmderr`"
  cat conftest-runcmdout >&5
  cat conftest-runcmderr >&5
  echo gasnet_cmd_result=$gasnet_cmd_result >&5
  rm -rf conftest*
  if test "$gasnet_cmd_result" = "0" ; then 
    if test -z "$gasnet_cmd_stdout$gasnet_cmd_stderr" ; then
      :
      $2 
    else
      :
      $3 
    fi
  else
    :
    $4 
  fi
])

dnl GASNET_TRY_CCOMPILE_WITHWARN(includes, function-body, action-success, action-warning, action-error)
dnl Compile a C program and take different actions based on complete success, error or warning
AC_DEFUN([GASNET_TRY_CCOMPILE_WITHWARN],[
  gasnet_testname=gasnet-conftest
  gasnet_testfile=${gasnet_testname}.c
  gasnet_compile_cmd="${CC-cc} -c $CFLAGS $CPPFLAGS $gasnet_testfile"
  cat > $gasnet_testfile <<EOF
#include "confdefs.h"
$1
int main() {
$2
; return 0; }
EOF
  GASNET_TRY_RUNCMD([$gasnet_compile_cmd], [$3], [
    echo "configure: warned program was:" >&5
    cat $gasnet_testfile >&5
    $4
    ],[
    echo "configure: failed program was:" >&5
    cat $gasnet_testfile >&5
    $5
    ])
  rm -f ${gasnet_testname}.*
])

dnl GASNET_TRY_CXXCOMPILE_WITHWARN(includes, function-body, action-success, action-warning, action-error)
dnl Compile a C++ program and take different actions based on complete success, error or warning
AC_DEFUN([GASNET_TRY_CXXCOMPILE_WITHWARN],[
  gasnet_testname=gasnet-conftest
  gasnet_testfile=${gasnet_testname}.cc
  gasnet_compile_cmd="${CXX-c++} -c $CXXFLAGS $CPPFLAGS $gasnet_testfile"
  cat > $gasnet_testfile <<EOF
#include "confdefs.h"
$1
int main() {
$2
; return 0; }
EOF
  GASNET_TRY_RUNCMD([$gasnet_compile_cmd], [$3], [
    echo "configure: warned program was:" >&5
    cat $gasnet_testfile >&5
    $4
    ],[
    echo "configure: failed program was:" >&5
    cat $gasnet_testfile >&5
    $5
    ])
  rm -f ${gasnet_testname}.*
])

dnl GASNET_TRY_CFLAG(flags, action-if-supported, action-if-not-supported)
AC_DEFUN([GASNET_TRY_CFLAG],[
gasnet_tryflag_oldflags="$CFLAGS"
CFLAGS="$CFLAGS $1"
AC_MSG_CHECKING(for C compiler flag $1)
GASNET_TRY_CCOMPILE_WITHWARN([], [], [
 AC_MSG_RESULT(yes)
 CFLAGS="$gasnet_tryflag_oldflags"
 $2
], [
 AC_MSG_RESULT(no/warning: $gasnet_cmd_stdout$gasnet_cmd_stderr)
 CFLAGS="$gasnet_tryflag_oldflags"
 $3
], [
 AC_MSG_RESULT(no/error: $gasnet_cmd_stdout$gasnet_cmd_stderr)
 CFLAGS="$gasnet_tryflag_oldflags"
 $3
])])

dnl GASNET_TRY_CXXFLAG(flags, action-if-supported, action-if-not-supported)
AC_DEFUN([GASNET_TRY_CXXFLAG],[
gasnet_tryflag_oldflags="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS $1"
AC_MSG_CHECKING(for C++ compiler flag $1)
GASNET_TRY_CXXCOMPILE_WITHWARN([], [], [
 AC_MSG_RESULT(yes)
 CXXFLAGS="$gasnet_tryflag_oldflags"
 $2
], [
 AC_MSG_RESULT(no/warning: $gasnet_cmd_stdout$gasnet_cmd_stderr)
 CXXFLAGS="$gasnet_tryflag_oldflags"
 $3
], [
 AC_MSG_RESULT(no/error: $gasnet_cmd_stdout$gasnet_cmd_stderr)
 CXXFLAGS="$gasnet_tryflag_oldflags"
 $3
])])

dnl GASNET_SET_CHECKED_CFLAGS CCVAR CFLAGSVAR DEFAULT_CFLAGS SAFE_CFLAGS
dnl Set CFLAGSVAR to a values that works with CCVAR 
dnl if CFLAGSVAR is already set, then keep it
dnl otherwise, if DEFAULT_CFLAGS works, then use it
dnl otherwise, use SAFE_CFLAGS
AC_DEFUN([GASNET_SET_CHECKED_CFLAGS],[
if test "$[$2]" != "" ; then
  GASNET_ENV_DEFAULT([$2], []) # user-provided flags
else
  GASNET_ENV_DEFAULT([$2], [$3]) # try DEFAULT_CFLAGS
  oldCC="$CC"
  oldCFLAGS="$CFLAGS"
  CC="$[$1]"
  CFLAGS=""
    GASNET_TRY_CFLAG([$[$2]], [], [
	AC_MSG_WARN([Unable to use default $2="$[$2]" so using "$4" instead. Consider manually seting $2])
        $2="$4"
    ])
  CC="$oldCC"
  CFLAGS="$oldCFLAGS"
fi
])

dnl GASNET_CHECK_OPTIMIZEDDEBUG CCVAR CFLAGSVAR EXTRAARGS INCLUDES 
dnl Ensure the compiler CC doesn't create a conflict between
dnl optimization and debugging.
AC_DEFUN([GASNET_CHECK_OPTIMIZEDDEBUG],[
 if test "$enable_debug" = "yes" ; then
  AC_MSG_CHECKING([$1 for debug vs. optimize compilation conflict])
  AC_LANG_SAVE
  AC_LANG_C
  OLDCC="$CC"
  OLDCFLAGS="$CFLAGS"
  CC="$[$1]"
  CFLAGS="$[$2] $3"
  AC_TRY_COMPILE( [
    $4
    #if defined(__OPTIMIZE__) || defined(NDEBUG)
	choke me
    #endif
  ], [ ], [ AC_MSG_RESULT(no) ], [
    AC_MSG_RESULT([yes])
    GASNET_MSG_ERROR([User requested --enable-debug but $1 or $2 has enabled optimization (-O) or disabled assertions (-DNDEBUG). Try setting $1='$[$1] -O0 -UNDEBUG' or changing $2])
  ])
  CC="$OLDCC"
  CFLAGS="$OLDCFLAGS"
  AC_LANG_RESTORE
 fi
])

dnl Output compilation error information, if available and do a AC_MSG_ERROR
dnl should be used within the failed branch of the compile macro, otherwise
dnl use GASNET_ERR_SAVE() in the failed branch to save the error info
AC_DEFUN([GASNET_MSG_ERROR],[
echo
echo "configure error: $1"
if test "" ; then
if test -f "conftest.$ac_ext" ; then
  errfile=conftest.$ac_ext
else
  errfile=gasnet_errsave_file
fi
if test -f "$errfile" ; then
  echo
  echo " --- Failed program --- "
  cat $errfile
  echo " -----------------------"
fi
fi
if test -f "conftest.err" ; then
  errfile=conftest.err
else
  errfile=gasnet_errsave_err
fi
if test -f "$errfile" ; then
  echo
  echo "Compilation error: "
  echo
  cat $errfile
fi
echo
CONFIG_FILE=`pwd`/config.log
AC_MSG_ERROR(See $CONFIG_FILE for details.)
])

AC_DEFUN([GASNET_ERR_SAVE],[
if test -f "conftest.$ac_ext" ; then
  cp conftest.$ac_ext gasnet_errsave_file
fi
if test -f "conftest.err" ; then
  cp conftest.err gasnet_errsave_err
fi
])

AC_DEFUN([GASNET_ERR_CLEANUP],[
  rm -f gasnet_errsave_file gasnet_errsave_err
])

dnl compile a program for a success/failure
dnl GASNET_TRY_CACHE_CHECK(description,cache_name,includes,program,action-on-success,action-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_CHECK],[
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_COMPILE([$3], [$4], cv_prefix[]$2=yes, cv_prefix[]$2=no))
if test "$cv_prefix[]$2" = yes; then
  :
  $5
else
  :
  $6
fi])


dnl link a program for a success/failure
dnl GASNET_TRY_CACHE_LINK(description,cache_name,includes,program,action-on-success,action-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_LINK],[
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_LINK([$3], [$4], cv_prefix[]$2=yes, cv_prefix[]$2=no))
if test "$cv_prefix[]$2" = yes; then
  :
  $5
else
  :
  $6
fi])

dnl run a program for a success/failure
dnl GASNET_TRY_CACHE_RUN(description,cache_name,program,action-on-success,action-on-failure)
AC_DEFUN([GASNET_TRY_CACHE_RUN],[
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_RUN([$3], cv_prefix[]$2=yes, cv_prefix[]$2=no, AC_MSG_ERROR(no default value for cross compiling)))
if test "$cv_prefix[]$2" = yes; then
  :
  $4
else
  :
  $5
fi])

dnl run a program to extract the value of a runtime expression 
dnl the provided code should set the integer val to the relevant value
dnl GASNET_TRY_CACHE_RUN(description,cache_name,headers,code_to_set_val,result_variable)
AC_DEFUN([GASNET_TRY_CACHE_RUN_EXPR],[
AC_CACHE_CHECK($1, cv_prefix[]$2,
AC_TRY_RUN([
  #include "confdefs.h"
  #include <stdio.h>
  $3
  main() {
    FILE *f=fopen("conftestval", "w");
    int val = 0;
    if (!f) exit(1);
    { $4; }
    fprintf(f, "%d\n", (int)(val));
    exit(0);
  }], cv_prefix[]$2=`cat conftestval`, cv_prefix[]$2=no, AC_MSG_ERROR(no default value for cross compiling)))
if test "$cv_prefix[]$2" != no; then
  :
  $5=$cv_prefix[]$2
fi])

AC_DEFUN([GASNET_PROG_CPP], [
  AC_PROVIDE([$0])
  AC_REQUIRE([AC_PROG_CC])
  AC_REQUIRE([AC_PROG_CPP])
  GASNET_GETFULLPATH(CPP)
  AC_SUBST(CPP)
  AC_SUBST(CPPFLAGS)
  AC_MSG_CHECKING(for working C preprocessor)
  AC_LANG_SAVE
  AC_LANG_C
  gasnet_progcpp_extrainfo=
  dnl deal with preprocessors who foolishly return success exit code even when they saw #error
  if test -n "`$CPP -version 2>&1 < /dev/null | grep MIPSpro`" ; then
    dnl The MIPSPro compiler has a broken preprocessor exit code by default, fix it
    dnl Using this flag is preferable to ensure that #errors encountered during compilation are fatal
    gasnet_progcpp_extrainfo=" (added -diag_error 1035 to deal with broken MIPSPro preprocessor)"
    CFLAGS="$CFLAGS -diag_error 1035"
    CPPFLAGS="$CPPFLAGS -diag_error 1035"    
  fi
  dnl final check
  AC_TRY_CPP([
    # error
  ], [AC_MSG_ERROR(Your C preprocessor is broken - reported success when it should have failed)], [])
  AC_TRY_CPP([], [], [GASNET_MSG_ERROR(Your C preprocessor is broken - reported failure when it should have succeeded)])
  AC_MSG_RESULT(yes$gasnet_progcpp_extrainfo)
  AC_LANG_RESTORE
])

AC_DEFUN([GASNET_PROG_CXXCPP], [
  AC_PROVIDE([$0])
  AC_REQUIRE([AC_PROG_CXX])
  AC_REQUIRE([AC_PROG_CXXCPP])
  GASNET_GETFULLPATH(CXXCPP)
  AC_SUBST(CXXCPP)
  AC_SUBST(CXXCPPFLAGS)
  AC_MSG_CHECKING(for working C++ preprocessor)
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  gasnet_progcxxcpp_extrainfo=
  dnl deal with preprocessors who foolishly return success exit code even when they saw #error
  if test -n "`$CXXCPP -version 2>&1 < /dev/null | grep MIPSpro`" ; then
    dnl The MIPSPro compiler has a broken preprocessor exit code by default, fix it
    dnl Using this flag is preferable to ensure that #errors encountered during compilation are fatal
    gasnet_progcxxcpp_extrainfo=" (added -diag_error 1035 to deal with broken MIPSPro preprocessor)"
    CXXFLAGS="$CXXFLAGS -diag_error 1035"
    CXXCPPFLAGS="$CXXCPPFLAGS -diag_error 1035"    
  fi
  dnl final check
  AC_TRY_CPP([
    # error
  ], [AC_MSG_ERROR(Your C++ preprocessor is broken - reported success when it should have failed)], [])
  AC_TRY_CPP([], [], [GASNET_MSG_ERROR(Your C++ preprocessor is broken - reported failure when it should have succeeded)])
  AC_MSG_RESULT(yes$gasnet_progcxxcpp_extrainfo)
  AC_LANG_RESTORE
])

AC_DEFUN([GASNET_PROG_CC], [
  AC_REQUIRE([GASNET_PROG_CPP])
  GASNET_GETFULLPATH(CC)
  AC_SUBST(CC)
  AC_SUBST(CFLAGS)
  AC_MSG_CHECKING(for working C compiler)
  AC_LANG_SAVE
  AC_LANG_C
  AC_TRY_COMPILE([], [
    fail for me
  ], [AC_MSG_ERROR(Your C compiler is broken - reported success when it should have failed)], [])
  AC_TRY_COMPILE([], [], [], [GASNET_MSG_ERROR(Your C compiler is broken - reported failure when it should have succeeded)])
  AC_TRY_LINK([ extern int some_bogus_nonexistent_symbol(); ], [ int x = some_bogus_nonexistent_symbol(); ],
              [AC_MSG_ERROR(Your C linker is broken - reported success when it should have failed)], [])
  AC_TRY_LINK([], [], [], [GASNET_MSG_ERROR(Your C link is broken - reported failure when it should have succeeded)])
  AC_MSG_RESULT(yes)
  AC_LANG_RESTORE
])

AC_DEFUN([GASNET_PROG_CXX], [
  AC_REQUIRE([GASNET_PROG_CXXCPP])
  GASNET_GETFULLPATH(CXX)
  AC_SUBST(CXX)
  AC_SUBST(CXXFLAGS)
  AC_MSG_CHECKING(for working C++ compiler)
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  AC_TRY_COMPILE([], [
    fail for me
  ], [AC_MSG_ERROR(Your C++ compiler is broken - reported success when it should have failed)], [])
  AC_TRY_COMPILE([], [], [], [GASNET_MSG_ERROR(Your C++ compiler is broken - reported failure when it should have succeeded)])
  AC_TRY_LINK([ extern int some_bogus_nonexistent_symbol(); ], [ int x = some_bogus_nonexistent_symbol(); ],
              [AC_MSG_ERROR(Your C++ linker is broken - reported success when it should have failed)], [])
  AC_TRY_LINK([], [], [], [GASNET_MSG_ERROR(Your C++ link is broken - reported failure when it should have succeeded)])
  AC_MSG_RESULT(yes)
  AC_LANG_RESTORE
])

dnl find working version of perl.  Checks to see if 'bytes' module is available,
dnl and sets GASNET_PERL_BYTESFLAG to either '-Mbytes' or empty string, for
dnl scripts that need to ward off Perl/UTF-8 issues 
AC_DEFUN([GASNET_PROG_PERL],[
  GASNET_PATH_PROGS(PERL, perl5 perl, perl)
  MIN_PERL_VERSION="5.005"
  AC_MSG_CHECKING(for perl version $MIN_PERL_VERSION or later)
  if $PERL -e "require $MIN_PERL_VERSION;" 2>/dev/null; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_ERROR(cannot find perl $MIN_PERL_VERSION or later)
  fi
  if $PERL -Mbytes -e "exit 0" 2>/dev/null; then
    GASNET_PERL_BYTESFLAG="-Mbytes"
  else
    GASNET_PERL_BYTESFLAG=
  fi
  AC_SUBST(GASNET_PERL_BYTESFLAG)
])

AC_DEFUN([GASNET_IFDEF],[
AC_TRY_CPP([
#ifndef $1
# error
#endif], [$2], [$3])])


AC_DEFUN([GASNET_FAMILY_CACHE_CHECK],[
AC_REQUIRE_CPP
AC_CACHE_CHECK(for $1 compiler family, $3, [
  $3=unknown

  GASNET_IFDEF(__GNUC__, $3=GNU)
  GASNET_IFDEF(__PGI, $3=PGI)
  GASNET_IFDEF(__xlC__, $3=XLC)
  GASNET_IFDEF(__KCC, $3=KAI)
  GASNET_IFDEF(__SUNPRO_C, $3=Sun)  # Sun C
  GASNET_IFDEF(__SUNPRO_CC, $3=Sun) # Sun C++
  GASNET_IFDEF(_CRAYC, $3=Cray)
  GASNET_IFDEF(__INTEL_COMPILER, $3=Intel)
  GASNET_IFDEF(__DECC, $3=Compaq) # Compaq C
  GASNET_IFDEF(__DECCXX, $3=Compaq) # Compaq C++
  GASNET_IFDEF(__HP_cc, $3=HP)  # HP C
  GASNET_IFDEF(__HP_aCC, $3=HP) # HP aCC (C++)

  if test "$$3" = "unknown"; then
    GASNET_IFDEF(mips, $3=MIPS)
    GASNET_IFDEF(_SX, $3=NEC)
  fi
])
if test "$$3" != "GNU" ; then
  dnl Some compilers (eg Intel 8.0) define __GNUC__ even though they are definitely not GNU C
  dnl Don't believe their filthy lies
  case $2 in 
    CC) ac_cv_c_compiler_gnu=no
        GCC=""
    ;;
    CXX) ac_cv_cxx_compiler_gnu=no
        GXX=""
    ;;
  esac
fi
$2_FAMILY=$$3
$2_UNWRAPPED=$$2
case $$3 in
  GNU) $2_WRAPPED=$$2 ;;
  *)   $2_WRAPPED="\$(top_builddir)/cc-wrapper \$($2_FAMILY) \$($2_UNWRAPPED)" ;;
esac
AC_SUBST($2_FAMILY)
AC_SUBST($2_UNWRAPPED)
AC_SUBST($2_WRAPPED)
GASNET_SUBST_FILE(cc_wrapper_mk, cc-wrapper.mk)
])


dnl deal with a buggy version of autoconf which assumes alloca returns char *
AC_DEFUN([GASNET_FUNC_ALLOCA_HELPER],[
  patsubst([$*], [p = alloca], [p = (char *)alloca])
])

AC_DEFUN([GASNET_FUNC_ALLOCA],[
  AC_SUBST(ALLOCA)
  GASNET_FUNC_ALLOCA_HELPER(AC_FUNC_ALLOCA)
])

dnl Set command for use in Makefile.am to install various files
dnl This command should remove all the magic used to run from the build
dnl directory, as well as deal with setting of the prefix at install time.
AC_DEFUN([GASNET_SET_INSTALL_CMD],[
GASNET_INSTALL_CMD="sed -e '/###NOINSTALL###/d' -e 's@###INSTALL_PREFIX###@\$(prefix)@g'"
AC_SUBST(GASNET_INSTALL_CMD)
])

dnl pass $1 to all subconfigures invoked recursively from this configure script
AC_DEFUN([GASNET_SUBCONFIGURE_ARG],[
ac_configure_args="$ac_configure_args $1"
])

