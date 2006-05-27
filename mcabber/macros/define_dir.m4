dnl define_dir.m4
dnl http://autoconf-archive.cryp.to/ac_define_dir.html

#  AC_DEFINE_DIR(VARNAME, DIR [, DESCRIPTION])
# This macro sets VARNAME to the expansion of the DIR variable, taking care
# of fixing up ${prefix} and such.
# VARNAME is then offered as both an output variable and a C preprocessor
# symbol.

# Authors
# Stepan Kasal <kasal@ucw.cz>, Andreas Schwab <schwab@suse.de>,
# Guido Draheim <guidod@gmx.de>, Alexandre Oliva
# (Slightly modified -- Mikael Berthe)

AC_DEFUN([AC_DEFINE_DIR], [
  prefix_NONE=
  exec_prefix_NONE=
  test "x$prefix" = xNONE && prefix_NONE=yes && prefix=$ac_default_prefix
  test "x$exec_prefix" = xNONE && exec_prefix_NONE=yes && exec_prefix=$prefix
dnl In Autoconf 2.60, ${datadir} refers to ${datarootdir}, which in turn
dnl refers to ${prefix}.  Thus we have to use `eval' twice.
  ac_define_dir=`eval echo [$]$2`
  ac_define_dir=`eval echo [$]ac_define_dir`
  AC_SUBST($1, "$ac_define_dir")
  ifelse($3, ,
    AC_DEFINE_UNQUOTED($1, "$ac_define_dir"),
    AC_DEFINE_UNQUOTED($1, "$ac_define_dir", $3))
  test "$prefix_NONE" && prefix=NONE
  test "$exec_prefix_NONE" && exec_prefix=NONE
])
