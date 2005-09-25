#ifndef __JABGLUE_H__
#define __JABGLUE_H__ 1

#include <glib.h>

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
void jb_disconnect(void);
void jb_main();
void jb_addbuddy(const char *jid, const char *name, const char *group);
void jb_delbuddy(const char *jid);
void jb_updatebuddy(const char *jid, const char *name, const char *group);
inline enum imstatus jb_getstatus();
void jb_setstatus(enum imstatus st, const char *recipient, const char *msg);
void jb_send_msg(const char *jid, const char *text, int type);
void jb_send_raw(const char *str);
void jb_keepalive();
inline void jb_reset_keepalive();
void jb_set_keepalive_delay(unsigned int delay);
inline void jb_set_priority(unsigned int priority);
void jb_room_join(const char *room);
void jb_room_unlock(const char *room);

#endif /* __JABGLUE_H__ */
