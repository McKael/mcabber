#ifndef __ROSTER_H__
#define __ROSTER_H__ 1

#include <glib.h>
#include <time.h>

#include "pgp.h"

#define SPECIAL_BUFFER_STATUS_ID  "[status]"

enum imstatus {
    offline,
    available,
    freeforchat,
    dontdisturb,
    notavail,
    away,
    invisible,
    imstatus_size
};

extern char imstatus2char[]; // Should match enum above

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
  sub_to      = 1 << 1,
  sub_from    = 1 << 2,
  sub_both    = sub_to|sub_from,
  sub_remove  = 1 << 3
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
#define ROSTER_TYPE_USER    1U
#define ROSTER_TYPE_GROUP   (1U<<1)
#define ROSTER_TYPE_AGENT   (1U<<2)
#define ROSTER_TYPE_ROOM    (1U<<3)
#define ROSTER_TYPE_SPECIAL (1U<<4)

// Flags:
#define ROSTER_FLAG_MSG     1U      // Message not read
#define ROSTER_FLAG_HIDE    (1U<<1) // Group hidden (or buddy window closed)
#define ROSTER_FLAG_LOCK    (1U<<2) // Node should not be removed from buddylist
#define ROSTER_FLAG_USRLOCK (1U<<3) // Node should not be removed from buddylist
// ROSTER_FLAG_LOCAL   (1U<<4) // Buddy not on server's roster  (??)

#define JEP0022
#define JEP0085

struct jep0022 {
  guint support;
  guint last_state_sent;
  gchar *last_msgid_sent;
  guint last_state_rcvd;
  gchar *last_msgid_rcvd;
};
struct jep0085 {
  guint support;
  guint last_state_sent;
  guint last_state_rcvd;
};

enum chatstate_support {
  CHATSTATES_SUPPORT_UNKNOWN = 0,
  CHATSTATES_SUPPORT_PROBED,
  CHATSTATES_SUPPORT_NONE,
  CHATSTATES_SUPPORT_OK
};

struct pgp_data {
  gchar *sign_keyid;  // KeyId used by the contact to sign their presence/msg
#ifdef HAVE_GPGME
  gpgme_sigsum_t last_sigsum; // Last signature summary
#endif
};

/* Message event and chat state flags */
#define ROSTER_EVENT_NONE      0U
/* JEP-22 Message Events */
#define ROSTER_EVENT_OFFLINE   (1U<<0)
#define ROSTER_EVENT_DELIVERED (1U<<1)
#define ROSTER_EVENT_DISPLAYED (1U<<2)
/* JEP-22 & JEP-85 */
#define ROSTER_EVENT_COMPOSING (1U<<3)
/* JEP-85 Chat State Notifications */
#define ROSTER_EVENT_ACTIVE    (1U<<4)
#define ROSTER_EVENT_PAUSED    (1U<<5)
#define ROSTER_EVENT_INACTIVE  (1U<<6)
#define ROSTER_EVENT_GONE      (1U<<7)

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
                        guint type, enum subscr esub, gint on_server);
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
inline int buddylist_isset_filter(void);
void    buddylist_set_filter(guchar);
guchar  buddylist_get_filter(void);
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
void    buddy_resource_setevents(gpointer rosterdata, const char *resname,
                                 guint event);
guint   buddy_resource_getevents(gpointer rosterdata, const char *resname);
struct jep0022 *buddy_resource_jep22(gpointer rosterdata, const char *resname);
struct jep0085 *buddy_resource_jep85(gpointer rosterdata, const char *resname);
struct pgp_data *buddy_resource_pgp(gpointer rosterdata, const char *resname);
enum imrole buddy_getrole(gpointer rosterdata, const char *resname);
enum imaffiliation buddy_getaffil(gpointer rosterdata, const char *resname);
const char *buddy_getrjid(gpointer rosterdata, const char *resname);
void    buddy_del_all_resources(gpointer rosterdata);
void    buddy_setflags(gpointer rosterdata, guint flags, guint value);
guint   buddy_getflags(gpointer rosterdata);
void    buddy_setonserverflag(gpointer rosterdata, guint onserver);
guint   buddy_getonserverflag(gpointer rosterdata);
GList  *buddy_search_jid(const char *jid);
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
