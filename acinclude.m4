dnl Terms of use are as specified in license.txt

dnl determine the autoconf version used to build configure script 
AC_DEFUN([GASNET_GET_AUTOCONF_VERSION],[
AC_MSG_CHECKING(autoconf version)
dnl AUTOCONF_VERSION=`cat ${srcdir}/configure | perl -e '{ while (<STDIN>) { if (m/enerated.*utoconf.*([[0-9]]+)\.([[0-9]]+).*/) { print "[$]1.[$]2\n"; exit 0 } } }'`
AUTOCONF_VERSION_STR=`cat ${srcdir}/configure | $AWK '/.*enerated.*utoconf.*([[0-9]]+).([[0-9]]+).*/ { [match]([$]0,"[[0-9]]+.[[0-9]]+"); print [substr]([$]0,RSTART,RLENGTH); exit 0 } '`
AUTOCONF_VERSION=`echo $AUTOCONF_VERSION_STR | $AWK -F. '{ printf("%i%i",[$]1,[$]2); }'`
AC_MSG_RESULT($AUTOCONF_VERSION_STR)
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

dnl add file to list of executable outputs that should be marked +x
dnl would be nice to use AC_CONFIG_COMMANDS() for each file, but autoconf 2.53
dnl  stupidly fails to execute commands having the same tag as a config output file
dnl  on subsequent calls to config.status
AC_DEFUN([GASNET_FIX_EXEC],[
  gasnet_exec_list="$gasnet_exec_list $1"
])

dnl ensure the "default" command is run on every invocation of config.status
AC_DEFUN([GASNET_FIX_EXEC_SETUP],[[
  dnl round-about method ensure autoconf 2.53 picks up depfiles command
  if test "\${config_commands+set}" != set ; then
    config_commands="default"
  fi
  CONFIG_COMMANDS="\$config_commands"
  gasnet_exec_list="$gasnet_exec_list"
]])

AC_DEFUN([GASNET_FIX_EXEC_OUTPUT],[[
  for file in $gasnet_exec_list; do
   case "$CONFIG_FILES" in
     *${file}*) chmod +x ${file} ;;
   esac
  done
]])

AC_DEFUN([GASNET_LIBGCC],[
AC_REQUIRE([AC_PROG_CC])
AC_CACHE_CHECK(for libgcc link flags, gasnet_cv_lib_gcc,
[if test "$GCC" = yes; then
  #LIBGCC="`$CC -v 2>&1 | sed -n 's:^Reading specs from \(.*\)/specs$:-L\1 -lgcc:p'`"
  LIBGCC="-L`$CC -print-libgcc-file-name | xargs dirname` -lgcc"
  if test -z "$LIBGCC"; then
    AC_MSG_ERROR(cannot find libgcc)
  fi
fi
gasnet_cv_lib_gcc="$LIBGCC"])
LIBGCC="$gasnet_cv_lib_gcc"
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
  AC_CACHE_VAL(gasnet_cv_envvar_$1, [
      case "$[$1]" in
	'') if test "$with_[]lowerscorename" != ""; then
	      gasnet_cv_envvar_$1="$with_[]lowerscorename"
	      envval_src_[$1]=given
	    else
	      gasnet_cv_envvar_$1="[$2]"
	      envval_src_[$1]=default
	    fi 
	    ;;
	*)  gasnet_cv_envvar_$1="$[$1]"
	    envval_src_[$1]=given
      esac
  ])

  [$1]="$gasnet_cv_envvar_$1"
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
  if test "$gasnet_acenv_list" != ""; then
    AC_MSG_ERROR(_GASNET_RESTORE_AUTOCONF_ENV called more than once)
  fi
  gasnet_acenv_list="$1"
  AC_MSG_CHECKING(for cached autoconf environment settings)
  AC_MSG_RESULT("") 
  for varname in $1; do
    val=`eval echo '$'"gasnet_cv_acenv_$varname"`
    if test "$val" != ""; then
      eval $varname=\"$val\"
      AC_MSG_RESULT([$varname=\"$val\"]) 
    fi
  done
])

dnl GASNET_SAVE_AUTOCONF_ENV() 
dnl  cache the environment variables inspected by autoconf macros
AC_DEFUN([GASNET_SAVE_AUTOCONF_ENV],[
  for varname in $gasnet_acenv_list; do
    val=`eval echo '$'"$varname"`
    if test "$val" != ""; then
      cachevarname=gasnet_cv_acenv_$varname
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

AC_DEFUN([GASNET_IF_ENABLED],[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(enable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  '' | no) $4 ;;
  *)  $3 ;;
esac
])

AC_DEFUN([GASNET_IF_DISABLED],[
AC_ARG_ENABLE($1,GASNET_OPTION_HELP(disable-$1,$2))
case "$enable_[]patsubst([$1], -, _)" in
  '' | yes) $4 ;;
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
esac
case "$$1" in
  '') AC_MSG_ERROR(cannot find $3)
      ;;
esac])

dnl GASNET_CHECK_LIB(library, function, action-if-found, action-if-not-found, other-flags, other-libraries)
AC_DEFUN([GASNET_CHECK_LIB],[
GASNET_check_lib_old_ldflags="$LDFLAGS"
LDFLAGS="$LD_FLAGS $5"
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

AC_DEFUN([GASNET_TRY_CACHE_CHECK],[
AC_CACHE_CHECK($1, gasnet_cv_$2,
AC_TRY_COMPILE([$3], [$4], gasnet_cv_$2=yes, gasnet_cv_$2=no))
if test "$gasnet_cv_$2" = yes; then
  :
  $5
fi])


AC_DEFUN([GASNET_TRY_CACHE_LINK],[
AC_CACHE_CHECK($1, gasnet_cv_$2,
AC_TRY_LINK([$3], [$4], gasnet_cv_$2=yes, gasnet_cv_$2=no))
if test "$gasnet_cv_$2" = yes; then
  :
  $5
fi])

dnl run a program for a success/failure
dnl GASNET_TRY_CACHE_RUN(description,cache_name,program,action-on-success)
AC_DEFUN([GASNET_TRY_CACHE_RUN],[
AC_CACHE_CHECK($1, gasnet_cv_$2,
AC_TRY_RUN([$3], gasnet_cv_$2=yes, gasnet_cv_$2=no, AC_MSG_ERROR(no default value for cross compiling)))
if test "$gasnet_cv_$2" = yes; then
  :
  $4
fi])

dnl run a program to extract the value of a runtime expression
dnl GASNET_TRY_CACHE_RUN(description,cache_name,headers,expression,result_variable)
AC_DEFUN([GASNET_TRY_CACHE_RUN_EXPR],[
AC_CACHE_CHECK($1, gasnet_cv_$2,
AC_TRY_RUN([
  #include "confdefs.h"
  #include <stdio.h>
  $3
  main() {
    FILE *f=fopen("conftestval", "w");
    if (!f) exit(1);
    fprintf(f, "%d\n", (int)($4));
    exit(0);
  }], gasnet_cv_$2=`cat conftestval`, gasnet_cv_$2=no, AC_MSG_ERROR(no default value for cross compiling)))
if test "$gasnet_cv_$2" != no; then
  :
  $5=$gasnet_cv_$2
fi])


AC_DEFUN([GASNET_IFDEF],[
AC_TRY_CPP([
#ifndef $1
# error
#endif], $2, $3)])


AC_DEFUN([GASNET_FAMILY_CACHE_CHECK],[
AC_REQUIRE_CPP
AC_CACHE_CHECK(for $1 compiler family, $3, [
  $3=unknown
  GASNET_IFDEF(__GNUC__, $3=GNU)
  GASNET_IFDEF(__PGI, $3=PGI)
  GASNET_IFDEF(__xlC__, $3=XLC)
  GASNET_IFDEF(__KCC, $3=KAI)
  GASNET_IFDEF(__SUNPRO_C, $3=Sun)
  GASNET_IFDEF(_CRAYC, $3=Cray)
  GASNET_IFDEF(__INTEL_COMPILER, $3=Intel)
  GASNET_IFDEF(__DECC, $3=Compaq)
  if test "$$3" = "unknown"; then
    GASNET_IFDEF(mips, $3=MIPS)
    GASNET_IFDEF(__hpux, $3=HP)
  fi
])
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


AC_DEFUN([GASNET_FUNC_ALLOCA],[
  AC_SUBST(ALLOCA)
  patsubst(AC_FUNC_ALLOCA, [p = alloca], [p = (char *) alloca])
])

dnl Set command for use in Makefile.am to install various files
dnl This command should remove all the magic used to run from the build
dnl directory, as well as deal with setting of the prefix at install time.
AC_DEFUN([GASNET_SET_INSTALL_CMD],[
GASNET_INSTALL_CMD="sed -e '/###NOINSTALL###/d' -e 's@###INSTALL_PREFIX###@\$(prefix)@g'"
AC_SUBST(GASNET_INSTALL_CMD)
])

