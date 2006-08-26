#ifndef __ROSTER_H__
#define __ROSTER_H__ 1

#include <glib.h>
#include <time.h>

#define SPECIAL_BUFFER_STATUS_ID  "[status]"

enum imstatus {
    offline,
    available,
    invisible,
    freeforchat,
    dontdisturb,
    notavail,
    away,
    imstatus_size
};

enum imrole {
  role_none,
  role_moderator,
  role_participant,
  role_visitor,
  imrole_size
};

extern char *strrole[]; // Should match enum above

enum imaffiliation {
  affil_none,
  affil_owner,
  affil_admin,
  affil_member,
  affil_outcast,
  imaffiliation_size
};

extern char *straffil[]; // Should match enum above

enum subscr {
  sub_none    = 0,
  sub_pending = 1,
  sub_to      = 1 << 2,
  sub_from    = 1 << 3,
  sub_both    = sub_to|sub_from,
  sub_remove  = 1 << 4
};

enum findwhat {
  jidsearch,
  namesearch
};

struct role_affil {
  enum { type_role, type_affil } type;
  union {
    enum imrole role;
    enum imaffiliation affil;
  } val;
};

// Roster_type is a set of flags, so values should be 2^n
#define ROSTER_TYPE_USER    1
#define ROSTER_TYPE_GROUP   2
#define ROSTER_TYPE_AGENT   4
#define ROSTER_TYPE_ROOM    8
#define ROSTER_TYPE_SPECIAL 16

// Flags:
#define ROSTER_FLAG_MSG     1   // Message not read
#define ROSTER_FLAG_HIDE    2   // Group hidden (or buddy window closed)
#define ROSTER_FLAG_LOCK    4   // Node should not be removed from buddylist
// ROSTER_FLAG_LOCAL   8   // Buddy not on server's roster  (??)

extern GList *buddylist;
extern GList *current_buddy;
extern GList *alternate_buddy;

// Macros...

#define BUDDATA(glist_node) ((glist_node)->data)
#define CURRENT_JID         buddy_getjid(BUDDATA(current_buddy))

// Prototypes...
void    roster_init(void);
GSList *roster_add_group(const char *name);
GSList *roster_add_user(const char *jid, const char *name, const char *group,
                        guint type, enum subscr esub);
GSList *roster_find(const char *jidname, enum findwhat type, guint roster_type);
void    roster_del_user(const char *jid);
void    roster_free(void);
void    roster_setstatus(const char *jid, const char *resname, gchar prio,
                         enum imstatus bstat, const char *status_msg,
                         time_t timestamp,
                         enum imrole role, enum imaffiliation affil,
                         const char *realjid);
void    roster_setflags(const char *jid, guint flags, guint value);
void    roster_msg_setflag(const char *jid, guint special, guint value);
const char *roster_getname(const char *jid);
const char *roster_getnickname(const char *jid);
void    roster_settype(const char *jid, guint type);
enum imstatus roster_getstatus(const char *jid, const char *resname);
const char   *roster_getstatusmsg(const char *jid, const char *resname);
guint   roster_gettype(const char *jid);
guint   roster_getsubscription(const char *jid);
void    roster_unsubscribed(const char *jid);

void    buddylist_build(void);
void    buddy_hide_group(gpointer rosterdata, int hide);
void    buddylist_set_hide_offline_buddies(int hide);
inline int buddylist_get_hide_offline_buddies(void);
const char *buddy_getjid(gpointer rosterdata);
void        buddy_setname(gpointer rosterdata, char *newname);
const char *buddy_getname(gpointer rosterdata);
void        buddy_setnickname(gpointer rosterdata, const char *newname);
const char *buddy_getnickname(gpointer rosterdata);
void        buddy_setinsideroom(gpointer rosterdata, guint inside);
guint       buddy_getinsideroom(gpointer rosterdata);
void        buddy_settopic(gpointer rosterdata, const char *newtopic);
const char *buddy_gettopic(gpointer rosterdata);
void    buddy_settype(gpointer rosterdata, guint type);
guint   buddy_gettype(gpointer rosterdata);
guint   buddy_getsubscription(gpointer rosterdata);
void    buddy_setgroup(gpointer rosterdata, char *newgroupname);
const char *buddy_getgroupname(gpointer rosterdata);
gpointer buddy_getgroup(gpointer rosterdata);
enum imstatus buddy_getstatus(gpointer rosterdata, const char *resname);
const char *buddy_getstatusmsg(gpointer rosterdata, const char *resname);
time_t  buddy_getstatustime(gpointer rosterdata, const char *resname);
gchar   buddy_getresourceprio(gpointer rosterdata, const char *resname);
//int   buddy_isresource(gpointer rosterdata);
GSList *buddy_getresources(gpointer rosterdata);
GSList *buddy_getresources_locale(gpointer rosterdata);
void    buddy_resource_setname(gpointer rosterdata, const char *resname,
                               const char *newname);
enum imrole buddy_getrole(gpointer rosterdata, const char *resname);
enum imaffiliation buddy_getaffil(gpointer rosterdata, const char *resname);
const char *buddy_getrjid(gpointer rosterdata, const char *resname);
void    buddy_del_all_resources(gpointer rosterdata);
void    buddy_setflags(gpointer rosterdata, guint flags, guint value);
guint   buddy_getflags(gpointer rosterdata);
GList  *buddy_search_jid(char *jid);
GList  *buddy_search(char *string);
void    foreach_buddy(guint roster_type,
                      void (*pfunc)(gpointer rosterdata, void *param),
                      void *param);
void    foreach_group_member(gpointer groupdata,
                             void (*pfunc)(gpointer rosterdata, void *param),
                             void *param);
gpointer unread_msg(gpointer rosterdata);

GSList *compl_list(guint type);

#endif /* __ROSTER_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
