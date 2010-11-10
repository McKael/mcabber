#ifndef __MCABBER_COMPL_H__
#define __MCABBER_COMPL_H__ 1

#include <glib.h>

#include <mcabber/config.h>

#define COMPL_CMD         1
#define COMPL_JID         2
#define COMPL_URLJID      3   // Not implemented yet
#define COMPL_NAME        4   // Not implemented yet
#define COMPL_STATUS      5
#define COMPL_FILENAME    6   // Not implemented yet
#define COMPL_ROSTER      7
#define COMPL_BUFFER      8
#define COMPL_GROUP       9
#define COMPL_GROUPNAME   10
#define COMPL_MULTILINE   11
#define COMPL_ROOM        12
#define COMPL_RESOURCE    13
#define COMPL_AUTH        14
#define COMPL_REQUEST     15
#define COMPL_EVENTS      16
#define COMPL_EVENTSID    17
#define COMPL_PGP         18
#define COMPL_COLOR       19
#define COMPL_OTR         20
#define COMPL_OTRPOLICY   21

#ifdef MODULES_ENABLE
void  compl_init_system(void);
guint compl_new_category(void);
void  compl_del_category(guint id);
#endif

void    compl_add_category_word(guint categ, const gchar *command);
void    compl_del_category_word(guint categ, const gchar *word);
GSList *compl_get_category_list(guint categ, guint *dynlist);

guint   new_completion(const gchar *prefix, GSList *compl_cat,
                       const gchar *suffix);
void    done_completion(void);
guint   cancel_completion(void);
const char *complete(void);

#endif /* __MCABBER_COMPL_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
