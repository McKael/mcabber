dnl aclocal.m4 generated automatically by aclocal 1.4-p6

dnl Copyright (C) 1994, 1995-8, 1999, 2001 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY, to the extent permitted by law; without
dnl even the implied warranty of MERCHANTABILITY or FITNESS FOR A
dnl PARTICULAR PURPOSE.

# Do all the work for Automake.  This macro actually does too much --
# some checks are only needed if your package does certain things.
# But this isn't really a big deal.

# serial 1

dnl Usage:
dnl AM_INIT_AUTOMAKE(package,version, [no-define])

AC_DEFUN([AM_INIT_AUTOMAKE],
[AC_REQUIRE([AM_SET_CURRENT_AUTOMAKE_VERSION])dnl
AC_REQUIRE([AC_PROG_INSTALL])
PACKAGE=[$1]
AC_SUBST(PACKAGE)
VERSION=[$2]
AC_SUBST(VERSION)
dnl test to see if srcdir already configured
if test "`cd $srcdir && pwd`" != "`pwd`" && test -f $srcdir/config.status; then
  AC_MSG_ERROR([source directory already configured; run "make distclean" there first])
fi
ifelse([$3],,
AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE", [Name of package])
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Version number of package]))
AC_REQUIRE([AM_SANITY_CHECK])
AC_REQUIRE([AC_ARG_PROGRAM])
dnl FIXME This is truly gross.
missing_dir=`cd $ac_aux_dir && pwd`
AM_MISSING_PROG(ACLOCAL, aclocal-${am__api_version}, $missing_dir)
AM_MISSING_PROG(AUTOCONF, autoconf, $missing_dir)
AM_MISSING_PROG(AUTOMAKE, automake-${am__api_version}, $missing_dir)
AM_MISSING_PROG(AUTOHEADER, autoheader, $missing_dir)
AM_MISSING_PROG(MAKEINFO, makeinfo, $missing_dir)
AC_REQUIRE([AC_PROG_MAKE_SET])])

# Copyright 2002  Free Software Foundation, Inc.

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA

# AM_AUTOMAKE_VERSION(VERSION)
# ----------------------------
# Automake X.Y traces this macro to ensure aclocal.m4 has been
# generated from the m4 files accompanying Automake X.Y.
AC_DEFUN([AM_AUTOMAKE_VERSION],[am__api_version="1.4"])

# AM_SET_CURRENT_AUTOMAKE_VERSION
# -------------------------------
# Call AM_AUTOMAKE_VERSION so it can be traced.
# This function is AC_REQUIREd by AC_INIT_AUTOMAKE.
AC_DEFUN([AM_SET_CURRENT_AUTOMAKE_VERSION],
	 [AM_AUTOMAKE_VERSION([1.4-p6])])

#
# Check to make sure that the build environment is sane.
#

AC_DEFUN([AM_SANITY_CHECK],
[AC_MSG_CHECKING([whether build environment is sane])
# Just in case
sleep 1
echo timestamp > conftestfile
# Do `set' in a subshell so we don't clobber the current shell's
# arguments.  Must try -L first in case configure is actually a
# symlink; some systems play weird games with the mod time of symlinks
# (eg FreeBSD returns the mod time of the symlink's containing
# directory).
if (
   set X `ls -Lt $srcdir/configure conftestfile 2> /dev/null`
   if test "[$]*" = "X"; then
      # -L didn't work.
      set X `ls -t $srcdir/configure conftestfile`
   fi
   if test "[$]*" != "X $srcdir/configure conftestfile" \
      && test "[$]*" != "X conftestfile $srcdir/configure"; then

      # If neither matched, then we have a broken ls.  This can happen
      # if, for instance, CONFIG_SHELL is bash and it inherits a
      # broken ls alias from the environment.  This has actually
      # happened.  Such a system could not be considered "sane".
      AC_MSG_ERROR([ls -t appears to fail.  Make sure there is not a broken
alias in your environment])
   fi

   test "[$]2" = conftestfile
   )
then
   # Ok.
   :
else
   AC_MSG_ERROR([newly created file is older than distributed files!
Check your system clock])
fi
rm -f conftest*
AC_MSG_RESULT(yes)])

dnl AM_MISSING_PROG(NAME, PROGRAM, DIRECTORY)
dnl The program must properly implement --version.
AC_DEFUN([AM_MISSING_PROG],
[AC_MSG_CHECKING(for working $2)
# Run test in a subshell; some versions of sh will print an error if
# an executable is not found, even if stderr is redirected.
# Redirect stdin to placate older versions of autoconf.  Sigh.
if ($2 --version) < /dev/null > /dev/null 2>&1; then
   $1=$2
   AC_MSG_RESULT(found)
else
   $1="$3/missing $2"
   AC_MSG_RESULT(missing)
fi
AC_SUBST($1)])

dnl Autoconf macros for libgnutls-extra
dnl $id$

# Modified for LIBGNUTLS_EXTRA -- nmav
# Configure paths for LIBGCRYPT
# Shamelessly stolen from the one of XDELTA by Owen Taylor
# Werner Koch   99-12-09

dnl AM_PATH_LIBGNUTLS_EXTRA([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND ]]])
dnl Test for libgnutls-extra, and define LIBGNUTLS_EXTRA_CFLAGS and LIBGNUTLS_EXTRA_LIBS
dnl
AC_DEFUN(AM_PATH_LIBGNUTLS_EXTRA,
[dnl
dnl Get the cflags and libraries from the libgnutls-extra-config script
dnl
AC_ARG_WITH(libgnutls-extra-prefix,
          [  --with-libgnutls-extra-prefix=PFX   Prefix where libgnutls-extra is installed (optional)],
          libgnutls_extra_config_prefix="$withval", libgnutls_extra_config_prefix="")

  if test x$libgnutls_extra_config_prefix != x ; then
     libgnutls_extra_config_args="$libgnutls_extra_config_args --prefix=$libgnutls_extra_config_prefix"
     if test x${LIBGNUTLS_EXTRA_CONFIG+set} != xset ; then
        LIBGNUTLS_EXTRA_CONFIG=$libgnutls_extra_config_prefix/bin/libgnutls-extra-config
     fi
  fi

  AC_PATH_PROG(LIBGNUTLS_EXTRA_CONFIG, libgnutls-extra-config, no)
  min_libgnutls_version=ifelse([$1], ,0.1.0,$1)
  AC_MSG_CHECKING(for libgnutls - version >= $min_libgnutls_version)
  no_libgnutls=""
  if test "$LIBGNUTLS_EXTRA_CONFIG" = "no" ; then
    no_libgnutls=yes
  else
    LIBGNUTLS_EXTRA_CFLAGS=`$LIBGNUTLS_EXTRA_CONFIG $libgnutls_extra_config_args --cflags`
    LIBGNUTLS_EXTRA_LIBS=`$LIBGNUTLS_EXTRA_CONFIG $libgnutls_extra_config_args --libs`
    libgnutls_extra_config_version=`$LIBGNUTLS_EXTRA_CONFIG $libgnutls_extra_config_args --version`


      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $LIBGNUTLS_EXTRA_CFLAGS"
      LIBS="$LIBS $LIBGNUTLS_EXTRA_LIBS"
dnl
dnl Now check if the installed libgnutls is sufficiently new. Also sanity
dnl checks the results of libgnutls-extra-config to some extent
dnl
      rm -f conf.libgnutlstest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gnutls/extra.h>

int
main ()
{
    system ("touch conf.libgnutlstest");

    if( strcmp( gnutls_extra_check_version(NULL), "$libgnutls_extra_config_version" ) )
    {
      printf("\n*** 'libgnutls-extra-config --version' returned %s, but LIBGNUTLS_EXTRA (%s)\n",
             "$libgnutls_extra_config_version", gnutls_extra_check_version(NULL) );
      printf("*** was found! If libgnutls-extra-config was correct, then it is best\n");
      printf("*** to remove the old version of LIBGNUTLS_EXTRA. You may also be able to fix the error\n");
      printf("*** by modifying your LD_LIBRARY_PATH enviroment variable, or by editing\n");
      printf("*** /etc/ld.so.conf. Make sure you have run ldconfig if that is\n");
      printf("*** required on your system.\n");
      printf("*** If libgnutls-extra-config was wrong, set the environment variable LIBGNUTLS_EXTRA_CONFIG\n");
      printf("*** to point to the correct copy of libgnutls-extra-config, and remove the file config.cache\n");
      printf("*** before re-running configure\n");
    }
    else if ( strcmp(gnutls_extra_check_version(NULL), LIBGNUTLS_EXTRA_VERSION ) )
    {
      printf("\n*** LIBGNUTLS_EXTRA header file (version %s) does not match\n", LIBGNUTLS_EXTRA_VERSION);
      printf("*** library (version %s). This is may be due to a different version of gnutls\n", gnutls_extra_check_version(NULL) );
      printf("*** and gnutls-extra.\n");
    }
    else
    {
      if ( gnutls_extra_check_version( "$min_libgnutls_version" ) )
      {
        return 0;
      }
     else
      {
        printf("no\n*** An old version of LIBGNUTLS_EXTRA (%s) was found.\n",
                gnutls_extra_check_version(NULL) );
        printf("*** You need a version of LIBGNUTLS_EXTRA newer than %s. The latest version of\n",
               "$min_libgnutls_version" );
        printf("*** LIBGNUTLS_EXTRA is always available from ftp://gnutls.hellug.gr/pub/gnutls.\n");
        printf("*** \n");
        printf("*** If you have already installed a sufficiently new version, this error\n");
        printf("*** probably means that the wrong copy of the libgnutls-extra-config shell script is\n");
        printf("*** being found. The easiest way to fix this is to remove the old version\n");
        printf("*** of LIBGNUTLS_EXTRA, but you can also set the LIBGNUTLS_EXTRA_CONFIG environment to point to the\n");
        printf("*** correct copy of libgnutls-extra-config. (In this case, you will have to\n");
        printf("*** modify your LD_LIBRARY_PATH enviroment variable, or edit /etc/ld.so.conf\n");
        printf("*** so that the correct libraries are found at run-time))\n");
      }
    }
  return 1;
}
],, no_libgnutls=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_libgnutls" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$2], , :, [$2])
  else
     if test -f conf.libgnutlstest ; then
        :
     else
        AC_MSG_RESULT(no)
     fi
     if test "$LIBGNUTLS_EXTRA_CONFIG" = "no" ; then
       echo "*** The libgnutls-extra-config script installed by LIBGNUTLS_EXTRA could not be found"
       echo "*** If LIBGNUTLS_EXTRA was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the LIBGNUTLS_EXTRA_CONFIG environment variable to the"
       echo "*** full path to libgnutls-extra-config."
     else
       if test -f conf.libgnutlstest ; then
        :
       else
          echo "*** Could not run libgnutls test program, checking why..."
          CFLAGS="$CFLAGS $LIBGNUTLS_EXTRA_CFLAGS"
          LIBS="$LIBS $LIBGNUTLS_EXTRA_LIBS"
          AC_TRY_LINK([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gnutls/extra.h>
],      [ return !!gnutls_extra_check_version(NULL); ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding LIBGNUTLS_EXTRA or finding the wrong"
          echo "*** version of LIBGNUTLS_EXTRA. If it is not finding LIBGNUTLS_EXTRA, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
          echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"
          echo "***" ],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means LIBGNUTLS_EXTRA was incorrectly installed"
          echo "*** or that you have moved LIBGNUTLS_EXTRA since it was installed. In the latter case, you"
          echo "*** may want to edit the libgnutls-extra-config script: $LIBGNUTLS_EXTRA_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
       fi
     fi
     LIBGNUTLS_EXTRA_CFLAGS=""
     LIBGNUTLS_EXTRA_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  rm -f conf.libgnutlstest
  AC_SUBST(LIBGNUTLS_EXTRA_CFLAGS)
  AC_SUBST(LIBGNUTLS_EXTRA_LIBS)
])

dnl *-*wedit:notab*-*  Please keep this as the last line.

