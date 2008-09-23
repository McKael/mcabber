#ifndef __CAPS_H__
#define __CAPS_H__ 1

#include <glib.h>

void  caps_init(void);
void  caps_free(void);
void  caps_add(char *hash);
int   caps_has_hash(const char *hash);
void  caps_set_identity(char *hash,
                        const char *category,
                        const char *name,
                        const char *type);
void  caps_add_feature(char *hash, const char *feature);
int   caps_has_feature(char *hash, char *feature);
void  caps_foreach_feature(const char *hash, GFunc func, gpointer user_data);

char *caps_generate(void);

#endif /* __CAPS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
