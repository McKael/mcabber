#ifndef __COMPL_H__
#define __COMPL_H__ 1

#include <glib.h>

#define COMPL_CMD        1      // Not implemented yet
#define COMPL_JID        2      // Not implemented yet
#define COMPL_URLJID     4      // Not implemented yet
#define COMPL_NAME       8      // Not implemented yet
#define COMPL_STATUS    16      // Not implemented yet
#define COMPL_FILENAME  32      // Not implemented yet

void compl_add_category_word(guint, const char *command);

void new_completion(char *prefix, GSList *compl_cat);
void done_completion(void);
guint cancel_completion(void);
const char *complete(void);

#endif /* __COMPL_H__ */
