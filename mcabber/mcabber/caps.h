#ifndef __MCABBER_CAPS_H__
#define __MCABBER_CAPS_H__ 1

#include <glib.h>

void  caps_init(void);
void  caps_free(void);
void  caps_add(const char *hash);
void  caps_remove(const char *hash);
void  caps_move_to_local(const char *hash, char *bjid);
int   caps_has_hash(const char *hash, const char *bjid);
void  caps_add_identity(const char *hash,
                        const char *category,
                        const char *name,
                        const char *type,
                        const char *lang);
void  caps_set_identity(char *hash,
                        const char *category,
                        const char *name,
                        const char *type);
void  caps_add_dataform(const char *hash, const char *formtype);
void  caps_add_dataform_field(const char *hash, const char *formtype,
                              const char *field, const char *value);
void  caps_add_feature(char *hash, const char *feature);
int   caps_has_feature(char *hash, char *feature, char *bjid);
void  caps_foreach_feature(const char *hash, GFunc func, gpointer user_data);

char *caps_generate(void);
gboolean caps_verify(const char *hash, char *function);
void  caps_copy_to_persistent(const char *hash, char *xml);
gboolean caps_restore_from_persistent(const char *hash);

#endif /* __MCABBER_CAPS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
