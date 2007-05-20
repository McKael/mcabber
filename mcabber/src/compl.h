#ifndef __COMPL_H__
#define __COMPL_H__ 1

#include <glib.h>

#define COMPL_CMD         (1U<<0)
#define COMPL_JID         (1U<<1)
#define COMPL_URLJID      (1U<<2)   // Not implemented yet
#define COMPL_NAME        (1U<<3)   // Not implemented yet
#define COMPL_STATUS      (1U<<4)
#define COMPL_FILENAME    (1U<<5)   // Not implemented yet
#define COMPL_ROSTER      (1U<<6)
#define COMPL_BUFFER      (1U<<7)
#define COMPL_GROUP       (1U<<8)
#define COMPL_GROUPNAME   (1U<<9)
#define COMPL_MULTILINE   (1U<<10)
#define COMPL_ROOM        (1U<<11)
#define COMPL_RESOURCE    (1U<<12)
#define COMPL_AUTH        (1U<<13)
#define COMPL_REQUEST     (1U<<14)
#define COMPL_EVENTS      (1U<<15)
#define COMPL_EVENTSID    (1U<<16)
#define COMPL_PGP         (1U<<17)

void    compl_add_category_word(guint, const char *command);
void    compl_del_category_word(guint categ, const char *word);
GSList *compl_get_category_list(guint cat_flags, guint *dynlist);

guint   new_completion(char *prefix, GSList *compl_cat);
void    done_completion(void);
guint   cancel_completion(void);
const char *complete(void);

#endif /* __COMPL_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
