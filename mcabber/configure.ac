#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([mcabber],[0.10.0-dev],[mcabber@lilotux.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([mcabber])
AM_CONFIG_HEADER(mcabber/config.h)
AC_CONFIG_HEADER(include/config.h)

AC_PROG_LIBTOOL
AC_PROG_RANLIB

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

if test "x$GCC" = "xyes"; then
    dnl Get gcc version
    AC_MSG_CHECKING([gcc version])
        gccver=$($CC -dumpversion)
        gccvermajor=$(echo $gccver | cut -d . -f1)
        gccverminor=$(echo $gccver | cut -d . -f2)
        gccvernum=$(expr $gccvermajor "*" 100 + $gccverminor)
    AC_MSG_RESULT($gccver)

    CFLAGS="$CFLAGS -Wall"
    if test x"$gccvermajor" = x"4" -a x"$gccverminor" = x"2"; then
        DBGCFLAGS="-fgnu89-inline"
    fi
fi

AC_SYS_LARGEFILE

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h locale.h netdb.h netinet/in.h stddef.h \
                  stdlib.h string.h strings.h sys/socket.h sys/time.h \
                  syslog.h termios.h wchar.h wctype.h localcharset.h])
AC_CHECK_HEADERS([unistd.h], , AC_MSG_ERROR([Missing header file]))
AC_VAR_TIMEZONE_EXTERNALS

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([alarm arc4random bzero gethostbyname gethostname inet_ntoa \
                isascii memmove memset modf select setlocale socket strcasecmp \
                strchr strdup strncasecmp strrchr strstr strcasestr vsnprintf \
                iswblank])


AC_CHECK_DECLS([strptime],,,
[#define _GNU_SOURCE
#include <time.h>])

# Check for tm_gmtoff
MC_TM_GMTOFF

# Check if we must provide a SIGWINCH handler
AC_ARG_ENABLE(sigwinch,
        [  --enable-sigwinch       compile with SIGWINCH handler],
        [with_sigwinch=$enableval],
        [with_sigwinch=$with_ext_funcs])
AC_MSG_RESULT($with_sigwinch)
if test "$with_sigwinch" = yes; then
    AC_DEFINE([USE_SIGWINCH], [], [Provide own SIGWINCH handler])
fi

# Checks for libraries.

AC_CHECK_LIB(charset, locale_charset)

AC_CHECK_FUNC(initscr,,
[
    cf_ncurses="ncurses"
    for lib in ncursesw ncurses
    do
        AC_CHECK_LIB($lib, waddnwstr,
            [cf_ncurses="$lib"; cf_ncurses_unicode="yes"; break])
    done
    AC_CHECK_LIB($cf_ncurses, initscr,
        [LIBS="$LIBS -l$cf_ncurses"
         if test "$cf_ncurses" = ncursesw; then
            AC_CHECK_HEADERS([ncursesw/ncurses.h ncursesw/panel.h],,
                 [AC_CHECK_HEADERS([ncurses.h panel.h],,
                                   AC_MSG_ERROR([Missing header file]))])
         else
             AC_CHECK_HEADERS([ncurses/ncurses.h ncurses/panel.h],,
                 [AC_CHECK_HEADERS([ncurses.h panel.h],,
                                   AC_MSG_ERROR([Missing header file]))])
         fi
        ],
        [CF_CURSES_LIBS])
])

AC_CHECK_LIB([panelw], [new_panel],,
             AC_CHECK_LIB([panel], [new_panel])
            )

if test x"$cf_ncurses_unicode" = x"yes"; then
    AC_DEFINE([HAVE_UNICODE], [], [Define if ncurses have unicode support])
else
    AC_MSG_WARN([Your ncurses installation does not support unicode])
fi

AC_CACHE_CHECK([for ESCDELAY variable],
               [mc_cv_ncurses_escdelay],
               [AC_TRY_LINK([], [
                            extern int ESCDELAY;
                            ESCDELAY = 0;
                            ],
                            [mc_cv_ncurses_escdelay=yes],
                            [mc_cv_ncurses_escdelay=no])
               ])
if test "$mc_cv_ncurses_escdelay" = yes; then
    AC_DEFINE([HAVE_ESCDELAY], 1,
              [Define if ncurses has ESCDELAY variable])
fi

AC_ARG_ENABLE(modules, [  --enable-modules         enable dynamic modules loading],
              enable_modules=$enableval)
if test "x$enable_modules" = "xyes"; then
  AC_DEFINE(MODULES_ENABLE, 1, [Define if you want dynamic modules loading])
  gmodule_module=gmodule
else
  gmodule_module=''
fi

# Check for glib
AM_PATH_GLIB_2_0(2.14.0,
                 [AC_DEFINE([HAVE_GLIB_REGEX], 1,
                            [Define if GLib has regex support])],
                 [AM_PATH_GLIB_2_0(2.0.0, , AC_MSG_ERROR([glib is required]),
                                  [g_list_append], ["$gmodule_module"])],
                 [g_regex_new "$gmodule_module"])

# Check for loudmouth
PKG_CHECK_MODULES(LOUDMOUTH, loudmouth-1.0 >= 1.4.2)
AC_SUBST(LOUDMOUTH_CFLAGS)
AC_SUBST(LOUDMOUTH_LIBS)

# Check for libidn
AC_ARG_WITH(libidn, AC_HELP_STRING([--with-libidn=[DIR]],
                                   [Support IDN (needs GNU Libidn)]),
            libidn=$withval, libidn=yes)
if test "$libidn" != "no" ; then
  PKG_CHECK_MODULES(LIBIDN, libidn >= 0.0.0, [libidn=yes], [libidn=no])
  if test "$libidn" != "yes" ; then
    libidn=no
    AC_MSG_WARN([Libidn not found])
  else
    libidn=yes
    AC_DEFINE(HAVE_LIBIDN, 1, [Define to 1 if you want Libidn.])
  fi
fi

# Check for gpgme
AC_ARG_ENABLE(gpgme, AC_HELP_STRING([--disable-gpgme], [disable GPGME support]),
    [ if test x"$enableval" = x"no"; then
        enable_gpgme=no
      fi
    ])

if test x"${enable_gpgme}" != x"no"; then
    AM_PATH_GPGME(1.0.0, AC_DEFINE([HAVE_GPGME], 1,
        [Define if you use GPGME to support OpenPGP]))
fi

# Check for otr
AC_ARG_ENABLE(otr, [  --enable-otr            enable OTR (Off-the-Record) messaging support],
              enable_otr=$enableval, otr="")
if test "x$enable_otr" = "xyes"; then
  # Look for libgcrypt and libotr
  AM_PATH_LIBGCRYPT(1.2.2, [
          AM_PATH_LIBOTR(3.1.0, ,
                         AC_MSG_ERROR(libotr 3.1.0 or newer is required.))
      ], AC_MSG_ERROR(libgcrypt 1.2.2 or newer is required.)
  )
fi

# Check for Enchant stuff
AC_ARG_ENABLE(enchant, [  --enable-enchant       enable enchant support],
              enable_enchant=$enableval, enchant="")
# Check for Aspell stuff
AC_ARG_ENABLE(aspell, [  --enable-aspell         enable aspell support],
              enable_aspell=$enableval, aspell="")

if test "x$enable_enchant" = "xyes"; then
 PKG_CHECK_MODULES(ENCHANT, [enchant],
   AC_DEFINE(WITH_ENCHANT, 1, [Define if you want enchant support])
 )
else
 if test "x$enable_aspell" = "xyes"; then
    AC_CHECK_HEADERS(aspell.h, [ have_aspell_includes=yes ])
    if test "x$have_aspell_includes" = "xyes"; then
        AC_CHECK_LIB(aspell, new_aspell_config, [ have_aspell_libs=yes ])
        if test "x$have_aspell_libs" = "xyes"; then
            AC_DEFINE([WITH_ASPELL], 1, [Define if you want aspell support])
            LIBS="$LIBS -laspell"
        else
            enable_aspell=no
        fi
    else
        enable_aspell=no
    fi
 fi
fi

AC_DEFINE([BUILD_JABBER], 1, [build with jabber support])

# Export $datadir to the source tree.
if test x"${datadir}" != x""; then
    AC_DEFINE_DIR(DATA_DIR, datadir, [Data files directory])
fi

AC_ARG_ENABLE(debug,
    [AC_HELP_STRING(--enable-debug, add development compilation options)],
    debug=$enableval, debug="")
if test x"${debug}" = x"yes"; then
    AC_DEFINE_UNQUOTED([ENABLE_DEBUG],[1],[Devel compilation options])
    if test "x$GCC" = "xyes"; then
        if test "$gccvernum" -ge "400"; then
            CFLAGS="$CFLAGS -Wextra"
        else
            CFLAGS="$CFLAGS -W"
        fi
        CFLAGS="$CFLAGS -Wno-unused-parameter -pedantic -std=gnu99 $DBGCFLAGS -O0"
    fi # gcc
    CFLAGS="$CFLAGS -g"
else
    CFLAGS="-O2 $CFLAGS"
fi

AC_ARG_ENABLE(hgcset,
    [AC_HELP_STRING(--disable-hgcset, do not use Mercurial changeset value)],
    hgcset=$enableval, hgcset="yes")
AM_CONDITIONAL(HGCSET, [test x$hgcset = xyes])
if test "${hgcset}" = "yes"; then
    AC_DEFINE([ENABLE_HGCSET], 1, [Use Mercurial changeset])
fi

AM_CONDITIONAL([OTR], [test x$libotr_found = xyes])
AM_CONDITIONAL([INSTALL_HEADERS], [test x$enable_modules = xyes])

# We need _GNU_SOURCE for strptime() and strcasestr()
CFLAGS="$CFLAGS -D_GNU_SOURCE"

AC_CONFIG_FILES([mcabber/Makefile
                 modules/Makefile
                 modules/beep/Makefile
                 doc/Makefile
                 doc/guide/Makefile
                 doc/help/Makefile
                 Makefile])
AC_OUTPUT
