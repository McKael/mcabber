#ifndef __UTILS_H__
#define __UTILS_H__ 1

#include <config.h>

extern char *LocaleCharSet;

#define to_utf8(s)   ((s) ? g_locale_to_utf8((s),   -1, NULL,NULL,NULL) : NULL)
#define from_utf8(s) ((s) ? g_convert_with_fallback((s), -1, LocaleCharSet, \
                                        "UTF-8", NULL,NULL,NULL,NULL) : NULL)

#define JID_RESOURCE_SEPARATOR      '/'
#define JID_RESOURCE_SEPARATORSTR   "/"
#define JID_DOMAIN_SEPARATOR        '@'
#define JID_DOMAIN_SEPARATORSTR     "@"

void ut_InitDebug(int level, const char *file);
void ut_WriteLog(unsigned int flag, const char *data);

char *expand_filename(const char *fname);

int checkset_perm(const char *name, unsigned int setmode);

const char *ut_get_tmpdir(void);

int    to_iso8601(char *dststr, time_t timestamp);
time_t from_iso8601(const char *timestamp, int utc);

inline void safe_usleep(unsigned int usec); /* Only for delays < 1s */

int check_jid_syntax(char *fjid);

inline void mc_strtolower(char *str);

void strip_arg_special_chars(char *s);
char **split_arg(const char *arg, unsigned int n, int dontstriplast);
void free_arg_lst(char **arglst);

void replace_nl_with_dots(char *bufstr);
char *ut_expand_tabs(const char *text);

#if !defined (HAVE_STRCASESTR)
char *strcasestr(const char *haystack, const char *needle);
#endif

int startswith(const char *str, const char *word);

#endif // __UTILS_H__

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
