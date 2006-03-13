#ifndef __COMPL_H__
#define __COMPL_H__ 1

#include <glib.h>

#define COMPL_CMD         (1<<0)
#define COMPL_JID         (1<<2)
#define COMPL_URLJID      (1<<3)  // Not implemented yet
#define COMPL_NAME        (1<<4)  // Not implemented yet
#define COMPL_STATUS      (1<<5)
#define COMPL_FILENAME    (1<<6)  // Not implemented yet
#define COMPL_ROSTER      (1<<7)
#define COMPL_BUFFER      (1<<8)
#define COMPL_GROUP       (1<<9)
#define COMPL_GROUPNAME   (1<<10)
#define COMPL_MULTILINE   (1<<11)
#define COMPL_ROOM        (1<<12)
#define COMPL_RESOURCE    (1<<13)
#define COMPL_AUTH        (1<<14)
#define COMPL_REQUEST     (1<<15)
#define COMPL_EVENTS      (1<<16)

void    compl_add_category_word(guint, const char *command);
void    compl_del_category_word(guint categ, const char *word);
GSList *compl_get_category_list(guint cat_flags);

void    new_completion(char *prefix, GSList *compl_cat);
void    done_completion(void);
guint   cancel_completion(void);
const char *complete(void);

#endif /* __COMPL_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
