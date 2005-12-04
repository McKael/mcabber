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

extern char imstatus2char[];
// Status chars: '_', 'o', 'i', 'f', 'd', 'n', 'a'

enum agtype {
    unknown,
    groupchat,
    transport,
    search
};

char *compose_jid(const char *username, const char *servername,
                  const char *resource);
jconn jb_connect(const char *jid, const char *server, unsigned int port,
                 int ssl, const char *pass);
inline unsigned char jb_getonline(void);
void jb_disconnect(void);
void jb_main();
void jb_addbuddy(const char *jid, const char *name, const char *group);
void jb_delbuddy(const char *jid);
void jb_updatebuddy(const char *jid, const char *name, const char *group);
inline enum imstatus jb_getstatus();
inline const char *jb_getstatusmsg();
void jb_setstatus(enum imstatus st, const char *recipient, const char *msg);
void jb_send_msg(const char *jid, const char *text, int type,
                 const char *subject);
void jb_send_raw(const char *str);
void jb_keepalive();
inline void jb_reset_keepalive();
void jb_set_keepalive_delay(unsigned int delay);
void jb_room_join(const char *room, const char *nickname);
void jb_room_unlock(const char *room);
void jb_room_invite(const char *room, const char *jid, const char *reason);
int  jb_room_setaffil(const char *roomid, const char *jid, const char *nick,
                      enum imaffiliation, const char *reason);

#endif /* __JABGLUE_H__ */

/* vim: set expandtab cindent cinoptions=>2:2(0:  For Vim users... */
