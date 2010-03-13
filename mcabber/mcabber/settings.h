#ifndef __MCABBER_SETTINGS_H__
#define __MCABBER_SETTINGS_H__ 1

#include <ctype.h>
#include <glib.h>

#include <mcabber/roster.h>
#include <mcabber/config.h>

#ifndef isblank
# define isblank(c)  ((c) == 0x20 || (c) == 0x09)
#endif


#define SETTINGS_TYPE_OPTION    1
#define SETTINGS_TYPE_ALIAS     2
#define SETTINGS_TYPE_BINDING   3
#ifdef HAVE_LIBOTR
#define SETTINGS_TYPE_OTR       4
#endif

#define COMMAND_CHAR    '/'
#define COMMAND_CHARSTR "/"

#define settings_opt_get(k)     settings_get(SETTINGS_TYPE_OPTION, k)
#define settings_opt_get_int(k) settings_get_int(SETTINGS_TYPE_OPTION, k)

#define mkcmdstr(cmd) COMMAND_CHARSTR cmd

typedef gchar *(*settings_guard_t)(const gchar *key, const gchar *new_value);

void    settings_init(void);
int     cfg_read_file(char *filename, guint mainfile);
guint   parse_assigment(gchar *assignment, gchar **pkey, gchar **pval);
gboolean settings_set_guard(const gchar *key, settings_guard_t guard);
void    settings_del_guard(const gchar *key);
void    settings_opt_set_raw(const gchar *key, const gchar *value);
void    settings_set(guint type, const gchar *key, const gchar *value);
void    settings_del(guint type, const gchar *key);
const gchar *settings_get(guint type, const gchar *key);
int     settings_get_int(guint type, const gchar *key);
const gchar *settings_get_status_msg(enum imstatus status);
void    settings_foreach(guint type,
                         void (*pfunc)(char *k, char *v, void *param),
                         void *param);

void    settings_pgp_setdisabled(const char *bjid, guint value);
guint   settings_pgp_getdisabled(const char *bjid);
void    settings_pgp_setforce(const char *bjid, guint value);
guint   settings_pgp_getforce(const char *bjid);
void    settings_pgp_setkeyid(const char *bjid, const char *keyid);
const char *settings_pgp_getkeyid(const char *bjid);

#ifdef HAVE_LIBOTR
guint   settings_otr_getpolicy(const char *bjid);
void    settings_otr_setpolicy(const char *bjid, guint value);
#endif

guint get_max_history_blocks(void);

char *default_muc_nickname(const char *roomid);

const gchar *isbound(int key);

#endif /* __MCABBER_SETTINGS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
