#ifndef __JABGLUE_H__
#define __JABGLUE_H__ 1

#include "../libjabber/jabber.h"

extern jconn jc;

extern char imstatus2char[];
// Status chars: '_', 'o', 'i', 'f', 'd', 'c', 'n', 'a'

enum imstatus {
    offline,
    available,
    invisible,
    freeforchat,
    dontdisturb,
    occupied,
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

jconn jb_connect(const char *jid, unsigned int port, int ssl, const char *pass);
void jb_disconnect(void);
void jb_main();
void jb_send_msg(const char *, const char *);
void jb_keepalive();
inline void jb_reset_keepalive();
void jb_set_keepalive_delay(unsigned int delay);

#endif /* __JABGLUE_H__ */
