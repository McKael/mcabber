#ifndef __ROSTER_H__
#define __ROSTER_H__ 1

#include <glib.h>

#ifdef MCABBER_TESTUNIT
# include "test_roster_main.h"
#else
# include "jabglue.h"
#endif

enum findwhat {
  jidsearch,
  namesearch
};

// Roster_type is a set of flags, so values should be 2^n
#define ROSTER_TYPE_USER    1
#define ROSTER_TYPE_GROUP   2
#define ROSTER_TYPE_AGENT   4

// Flags:
#define ROSTER_FLAG_MSG     1   // Message not read
#define ROSTER_FLAG_HIDE    2   // Group hidden (or buddy window closed)
#define ROSTER_FLAG_LOCK    4   // Node should not be removed from buddylist
// ROSTER_FLAG_LOCAL   8   // Buddy not on server's roster  (??)

extern GList *buddylist;
extern GList *current_buddy;

// Macros...

#define BUDDATA(glist_node) ((glist_node)->data)
#define CURRENT_JID         buddy_getjid(BUDDATA(current_buddy))

// Prototypes...
GSList *roster_add_group(const char *name);
GSList *roster_add_user(const char *jid, const char *name, const char *group,
        guint type);
void    roster_del_user(const char *jid);
void    roster_setstatus(const char *jid, enum imstatus bstat);
void    roster_setflags(const char *jid, guint flags, guint value);

void buddylist_hide_offline_buddies(int hide);
void buddy_hide_group(gpointer rosterdata, int hide);
void buddylist_build(void);
const char *buddy_getjid(gpointer rosterdata);
const char *buddy_getname(gpointer rosterdata);
guint buddy_gettype(gpointer rosterdata);
enum imstatus buddy_getstatus(gpointer rosterdata);
guint buddy_getflags(gpointer rosterdata);

#endif /* __ROSTER_H__ */
