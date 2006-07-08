#ifndef __UTF8_H__
#define __UTF8_H__ 1

#include <config.h>

#ifdef HAVE_WCHAR_H
# include <wchar.h>
# define UNICODE
# define get_char_width(c) (utf8_mode ? wcwidth(get_char(c)) : 1)
#else
# define wcwidth(c) 1
# define get_char_width(c) 1
#endif

#ifdef HAVE_WCTYPE_H
# include <wctype.h>
#else
# define iswblank(c) (c == ' ')
# define iswalnum(c) isalnum(c)
# define iswprint(c) isprint(c)
# undef UNICODE
#endif

#ifndef HAVE_NCURSESW_NCURSES_H
# undef UNICODE
#endif

extern int utf8_mode;

char *prev_char(char *str, const char *limit);
char *next_char(char *str);
unsigned get_char(const char *str);
char *put_char(char *str, unsigned c);

#endif /* __UTF8_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
