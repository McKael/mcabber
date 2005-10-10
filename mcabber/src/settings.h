#ifndef __SETTINGS_H__
#define __SETTINGS_H__ 1

#include <ctype.h>
#include <glib.h>

#include "jabglue.h"

#ifndef isblank
# define isblank(c)  ((c) == 0x20 || (c) == 0x09)
#endif


/* Default status messages */
#define MSG_AVAIL     "I'm here!"
#define MSG_FREE      "Free for chat"
#define MSG_DND       "Busy"
#define MSG_NOTAVAIL  "Not available"
#define MSG_AWAY      "Away"
#define MSG_AUTOAWAY  "Auto away status (idle)"


#define SETTINGS_TYPE_OPTION    1
#define SETTINGS_TYPE_ALIAS     2
#define SETTINGS_TYPE_BINDING   3

#define settings_opt_get(k)     settings_get(SETTINGS_TYPE_OPTION, k)
#define settings_opt_get_int(k) settings_get_int(SETTINGS_TYPE_OPTION, k)

int     cfg_read_file(char *filename);
guint   parse_assigment(gchar *assignment,
                        const gchar **pkey, const gchar **pval);
void    settings_set(guint type, const gchar *key, const gchar *value);
void    settings_del(guint type, const gchar *key);
const gchar *settings_get(guint type, const gchar *key);
int     settings_get_int(guint type, const gchar *key);
const gchar *settings_get_status_msg(enum imstatus status);

const gchar *isbound(int key);

#endif /* __SETTINGS_H__ */

