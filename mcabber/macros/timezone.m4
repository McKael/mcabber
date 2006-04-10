dnl ac_var_timeszone_externals.m4

# Define 'timezone', 'altzone' and 'daylight'
# http://www.gnu.org/software/ac-archive/Miscellaneous/ac_var_timezone_externals.html
# Use instead of 'AC_STRUCT_TIMEZONE' to determine whether the
# the external timezone variables 'timezone', 'altzone' and 'daylight' exist,
# defining 'HAVE_TIMEZONE', 'HAVE_ALTZONE' and 'HAVE_DAYLIGHT' respectively
# (as well as gaining the macros defined by 'AC_STRUCT_TIMEZONE').
# Mark R.Bannister <markb@freedomware.co.uk>
AC_DEFUN([AC_VAR_TIMEZONE_EXTERNALS],
[  AC_REQUIRE([AC_STRUCT_TIMEZONE])dnl
   AC_CACHE_CHECK(for timezone external, mb_cv_var_timezone,
   [  AC_TRY_LINK([#include <time.h>], [return (int)timezone;],
         mb_cv_var_timezone=yes,
         mb_cv_var_timezone=no)
   ])
   AC_CACHE_CHECK(for altzone external, mb_cv_var_altzone,
   [  AC_TRY_LINK([#include <time.h>], [return (int)altzone;],
         mb_cv_var_altzone=yes,
         mb_cv_var_altzone=no)
   ])
   AC_CACHE_CHECK(for daylight external, mb_cv_var_daylight,
   [  AC_TRY_LINK([#include <time.h>], [return (int)daylight;],
         mb_cv_var_daylight=yes,
         mb_cv_var_daylight=no)
   ])
   if test $mb_cv_var_timezone = yes; then
      AC_DEFINE([HAVE_TIMEZONE], 1,
              [Define if you have the external 'timezone' variable.])
   fi
   if test $mb_cv_var_altzone = yes; then
      AC_DEFINE([HAVE_ALTZONE], 1,
              [Define if you have the external 'altzone' variable.])
   fi
   if test $mb_cv_var_daylight = yes; then
      AC_DEFINE([HAVE_DAYLIGHT], 1,
              [Define if you have the external 'daylight' variable.])
   fi
])
