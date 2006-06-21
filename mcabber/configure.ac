#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([mcabber],[0.7.7],[mcabber@lilotux.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src])
AM_CONFIG_HEADER(config.h)

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
fi

# Checks for libraries.
AC_CHECK_LIB([ncurses], [initscr], , AC_MSG_ERROR([NCURSES library needed]))
AC_CHECK_LIB([panel], [new_panel])

AC_CHECK_DECLS([strptime],,,
[#define _GNU_SOURCE
#include <time.h>])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h locale.h netdb.h netinet/in.h stddef.h \
                  stdlib.h string.h strings.h sys/socket.h sys/time.h \
                  syslog.h termios.h unistd.h getopt.h])
AC_CHECK_HEADERS([ncurses.h panel.h getopt.h], ,
                 AC_MSG_ERROR([Missing header file]))
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
AC_CHECK_FUNCS([alarm bzero gethostbyname gethostname inet_ntoa isascii \
                memmove memset modf select setlocale socket strcasecmp \
                strchr strdup strncasecmp strrchr strstr strcasestr])

# Check for tm_gmtoff
MC_TM_GMTOFF

# Check for glib
AM_PATH_GLIB_2_0(2.0.0, , AC_MSG_ERROR([glib is required]),[g_list_append])

AC_ARG_WITH(ssl, [  --with-ssl              enable SSL secured connections using either OpenSSL
                          or GnuTLS],
        [with_ssl=$withval])

if test "$with_ssl" != "no"; then
    AC_ARG_WITH(openssl,
        [  --with-openssl=[DIR]      enable SSL secured connections using the OpenSSL
                          library in DIR (optional)],
        [with_openssl=$withval])

    if test -z "$with_openssl"; then
        for ac_dir in /usr/local /usr; do
            if test -f "$ac_dir/include/openssl/ssl.h"; then
                with_openssl=$ac_dir
                break;
            fi
        done
    fi

    AC_MSG_CHECKING(for OpenSSL)

    if test -n "$with_openssl" -a "$with_openssl" != "no"; then
        if test "$with_openssl" = "yes"; then with_openssl="/usr"; fi
        CFLAGS="$CFLAGS -I${with_openssl}"
        AC_DEFINE([HAVE_OPENSSL], [], [OpenSSL])
        AC_MSG_RESULT([found in $with_openssl])
        AC_CHECK_LIB(crypto, main, [
                AC_CHECK_LIB(crypto, SSLeay_add_all_algorithms, [
                    AC_DEFINE([HAVE_SSLEAY], [], [SSLeay])
                ])
            ])
        AC_CHECK_LIB(ssl, main)
    else
        AC_MSG_RESULT([not found or disabled])

        # AM_PATH_LIBGNUTLS_EXTRA(0.0.1, [
        #     CXXFLAGS="$CXXFLAGS $LIBGNUTLS_CFLAGS"
        #     LIBS="$LIBS $LIBGNUTLS_LIBS -lgnutls-extra -lgnutls-openssl"
        #     AC_DEFINE([HAVE_GNUTLS], [], [GNU TLS])
        # ])
    fi
fi

AC_DEFINE(BUILD_JABBER, 1, [build with jabber support])

# Export $datadir to the source tree.
if test x"${datadir}" != x""; then
    AC_DEFINE_DIR(DATA_DIR, "${datadir}", [Data files directory])
fi

AC_ARG_ENABLE(debug,
    [AC_HELP_STRING(--enable-debug, Add development compilation options)],
    debug=$enableval, debug="")
if test "${debug}" = "yes"; then
    AC_DEFINE_UNQUOTED([DEBUG_ENABLE],[1],[Devel compilation options])
    if test "x$GCC" = "xyes"; then
        if test "$gccvernum" -ge "400"; then
            CFLAGS="$CFLAGS -Wextra"
        else
            CFLAGS="$CFLAGS -W"
        fi
        CFLAGS="$CFLAGS -Wno-unused-parameter -pedantic -std=gnu99 -O0"
    fi # gcc
    CFLAGS="$CFLAGS -g"
else
    CFLAGS="$CFLAGS -O2"
fi

AC_ARG_ENABLE(hgcset,
    [AC_HELP_STRING(--enable-hgcset, Add Mercurial changeset)],
    hgcset=$enableval, hgcset="")
AM_CONDITIONAL(HGCSET, test x$hgcset = xyes)
if test "${hgcset}" = "yes"; then
    AC_DEFINE(ENABLE_HGCSET, 1, [Use Mercurial changeset])
fi

AC_CONFIG_FILES([connwrap/Makefile
                 libjabber/Makefile
                 src/Makefile
                 doc/Makefile
                 doc/help/Makefile
                 Makefile])
AC_OUTPUT
