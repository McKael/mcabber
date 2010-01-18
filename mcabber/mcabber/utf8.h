#ifndef __MCABBER_UTF8_H__
#define __MCABBER_UTF8_H__ 1

#include <mcabber/config.h>

#if defined HAVE_UNICODE && defined HAVE_WCHAR_H && defined HAVE_WCTYPE_H
# define UNICODE
#endif

#ifdef HAVE_WCHAR_H
# include <wchar.h>
# define get_char_width(c) (utf8_mode ? wcwidth(get_char(c)) : 1)
#else
# define wcwidth(c) 1
# define get_char_width(c) 1
#endif

#ifdef HAVE_WCTYPE_H
# include <wctype.h>

/* The following bit is a hack for Solaris 8&9 systems that don't have
 * iswblank().
 * For now i made sure it comes after wctype.h so it doesn't create
 * problems (wctype.h has calls to iswblank() before wctype() is declared).
 * (Sebastian Kayser)
 */
# ifndef HAVE_ISWBLANK
#  define iswblank(wc) iswctype(wc, wctype("blank"))
# endif

#else
# define iswblank(c) (c == ' ')
# define iswalnum(c) isalnum(c)
# define iswprint(c) isprint(c)
# define towupper(c) toupper(c)
# define towlower(c) tolower(c)
# define iswalpha(c) isalpha(c)
#endif

extern int utf8_mode;

char *prev_char(char *str, const char *limit);
char *next_char(char *str);
unsigned get_char(const char *str);
char *put_char(char *str, unsigned c);

#endif /* __MCABBER_UTF8_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
