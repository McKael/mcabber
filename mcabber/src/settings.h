#ifndef __SETTINGS_H__
#define __SETTINGS_H__ 1

#include <glib.h>

#define SETTINGS_TYPE_OPTION    1
#define SETTINGS_TYPE_ALIAS     2
#define SETTINGS_TYPE_BINDING   3

#define settings_opt_get(k)     settings_get(SETTINGS_TYPE_OPTION, k)
#define settings_opt_get_int(k) settings_get_int(SETTINGS_TYPE_OPTION, k)

void    settings_set(guint type, gchar *key, gchar *value);
void    settings_del(guint type, gchar *key);
gchar  *settings_get(guint type, gchar *key);
int     settings_get_int(guint type, gchar *key);

#endif /* __SETTINGS_H__ */

