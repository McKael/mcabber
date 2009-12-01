#ifndef __XMPP_H__
#define __XMPP_H__ 1

#include <loudmouth/loudmouth.h>
#include "roster.h"

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

extern LmConnection* lconnection;
extern LmSSL* lssl;

void xmpp_connect(void);
void xmpp_disconnect(void);

void xmpp_room_join(const char *room, const char *nickname, const char *passwd);
int xmpp_room_setattrib(const char *roomid, const char *fjid,
                        const char *nick, struct role_affil ra,
                        const char *reason);
void xmpp_room_invite(const char *room, const char *fjid, const char *reason);
void xmpp_room_unlock(const char *room);
void xmpp_room_destroy(const char *room, const char *venue, const char *reason);

void xmpp_addbuddy(const char *bjid, const char *name, const char *group);
void xmpp_updatebuddy(const char *bjid, const char *name, const char *group);
void xmpp_delbuddy(const char *bjid);

void xmpp_send_msg(const char *fjid, const char *text, int type,
                   const char *subject, gboolean otrinject, gint *encrypted,
                   LmMessageSubType type_overwrite, gpointer *xep184);

void xmpp_send_s10n(const char *bjid, LmMessageSubType type);

enum imstatus xmpp_getstatus(void);
const char *xmpp_getstatusmsg(void);
void xmpp_setprevstatus(void);

void xmpp_setstatus(enum imstatus st, const char *recipient,
                    const char *msg, int do_not_sign);

void xmpp_send_chatstate(gpointer buddy, guint chatstate);

GSList *xmpp_get_all_storage_bookmarks(void);
GSList *xmpp_get_all_storage_rosternotes(void);
void xmpp_set_storage_bookmark(const char *roomid, const char *name,
                               const char *nick, const char *passwd,
                               int autojoin, enum room_printstatus pstatus,
                               enum room_autowhois awhois);
struct annotation *xmpp_get_storage_rosternotes(const char *barejid,
                                                int silent);
void xmpp_set_storage_rosternotes(const char *barejid, const char *note);
guint xmpp_is_bookmarked(const char *bjid);
const char *xmpp_get_bookmark_nick(const char *bjid);

void xmpp_request(const char *fjid, enum iqreq_type reqtype);
void request_vcard(const char *bjid);
void xmpp_request_storage(const gchar *storage);

#endif /* __XMPP_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
