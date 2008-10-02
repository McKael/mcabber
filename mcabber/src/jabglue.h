#ifndef __JABGLUE_H__
#define __JABGLUE_H__ 1

#include <glib.h>

#include "roster.h"
#include "../libjabber/jabber.h"

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if ! HAVE_DECL_STRPTIME
 extern char *strptime ();
#endif

extern jconn jc;
extern guint AutoConnection;

extern char imstatus2char[];
// Status chars: '_', 'o', 'i', 'f', 'd', 'n', 'a'

enum agtype {
  unknown,
  groupchat,
  transport,
  search
};

enum iqreq_type {
  iqreq_none,
  iqreq_version,
  iqreq_time,
  iqreq_last,
  iqreq_vcard
};

struct annotation {
  time_t cdate;
  time_t mdate;
  gchar *jid;
  gchar *text;
};

struct bookmark {
  gchar *roomjid;
  gchar *name;
  gchar *nick;
  guint autojoin;
  /* enum room_printstatus pstatus; */
  /* enum room_autowhois awhois; */
};

char *jidtodisp(const char *fjid);
char *compose_jid(const char *username, const char *servername,
                  const char *resource);
jconn jb_connect(const char *fjid, const char *server, unsigned int port,
                 int ssl, const char *pass);
unsigned char jb_getonline(void);
void jb_disconnect(void);
void jb_main(void);
void jb_subscr_send_auth(const char *bjid);
void jb_subscr_cancel_auth(const char *bjid);
void jb_subscr_request_auth(const char *bjid);
void jb_subscr_request_cancel(const char *bjid);
void jb_addbuddy(const char *bjid, const char *name, const char *group);
void jb_delbuddy(const char *bjid);
void jb_updatebuddy(const char *bjid, const char *name, const char *group);
enum imstatus jb_getstatus(void);
const char *jb_getstatusmsg(void);
void jb_setstatus(enum imstatus st, const char *recipient, const char *msg,
                  int do_not_sign);
void jb_setprevstatus(void);
void jb_send_msg(const char *fjid, const char *text, int type,
                 const char *subject, const char *id, gint *encrypted,
                 const char *type_overwrite);
void jb_send_raw(const char *str);
void jb_send_chatstate(gpointer buddy, guint chatstate);
void jb_keepalive(void);
void jb_reset_keepalive(void);
void jb_set_keepalive_delay(unsigned int delay);
void jb_room_join(const char *room, const char *nickname, const char *passwd);
void jb_room_unlock(const char *room);
void jb_room_destroy(const char *room, const char *venue, const char *reason);
void jb_room_invite(const char *room, const char *fjid, const char *reason);
int  jb_room_setattrib(const char *roomid, const char *fjid, const char *nick,
                       struct role_affil ra, const char *reason);
void jb_iqs_display_list(void);
void jb_request(const char *fjid, enum iqreq_type reqtype);
guint jb_is_bookmarked(const char *bjid);
const char *jb_get_bookmark_nick(const char *bjid);
GSList *jb_get_all_storage_bookmarks(void);
void jb_set_storage_bookmark(const char *roomid, const char *name,
                             const char *nick, const char *passwd,
                             int autojoin, enum room_printstatus pstatus,
                             enum room_autowhois awhois);
struct annotation *jb_get_storage_rosternotes(const char *barejid, int silent);
GSList *jb_get_all_storage_rosternotes(void);
void jb_set_storage_rosternotes(const char *barejid, const char *note);

#endif /* __JABGLUE_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
