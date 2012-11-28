#ifndef __MCABBER_UTILS_H__
#define __MCABBER_UTILS_H__ 1

#include <mcabber/config.h>

extern const char *LocaleCharSet;

#define to_utf8(s)   ((s) ? g_locale_to_utf8((s),   -1, NULL,NULL,NULL) : NULL)
#define from_utf8(s) ((s) ? g_convert_with_fallback((s), -1, LocaleCharSet, \
                                        "UTF-8", NULL,NULL,NULL,NULL) : NULL)

#define JID_RESOURCE_SEPARATOR      '/'
#define JID_RESOURCE_SEPARATORSTR   "/"
#define JID_DOMAIN_SEPARATOR        '@'
#define JID_DOMAIN_SEPARATORSTR     "@"

char *jidtodisp(const char *fjid);
char *jid_get_username(const char *fjid);
char *compose_jid(const char *username, const char *servername,
                  const char *resource);
gboolean jid_equal(const char *jid1, const char *jid2);

void fingerprint_to_hex(const unsigned char *fpr, char hex[49]);
gboolean hex_to_fingerprint(const char * hex, char fpr[16]);

void ut_init_debug(void);
void ut_write_log(unsigned int flag, const char *data);

char *expand_filename(const char *fname);

int checkset_perm(const char *name, unsigned int setmode);

const char *ut_get_tmpdir(void);

int    to_iso8601(char *dststr, time_t timestamp);
time_t from_iso8601(const char *timestamp, int utc);

int check_jid_syntax(const char *fjid);

void mc_strtolower(char *str);

void strip_arg_special_chars(char *s);
char **split_arg(const char *arg, unsigned int n, int dontstriplast);
void free_arg_lst(char **arglst);

void replace_nl_with_dots(char *bufstr);
char *ut_expand_tabs(const char *text);
char *ut_unescape_tabs_cr(const char *text);

#if !defined (HAVE_STRCASESTR)
char *strcasestr(const char *haystack, const char *needle);
#endif

int startswith(const char *str, const char *word, guint ignore_case);

#endif // __MCABBER_UTILS_H__

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
