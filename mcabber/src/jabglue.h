#ifndef __JABGLUE_H__
#define __JABGLUE_H__ 1

#include "../libjabber/jabber.h"

extern jconn jc;

enum imstatus {
    offline = 0,
    available,
    invisible,
    freeforchat,
    dontdisturb,
    occupied,
    notavail,
    away,
    imstatus_size
};

static char imstatus2char[imstatus_size] = {
    '_', 'o', 'i', 'f', 'd', 'c', 'n', 'a'
};

jconn jb_connect(const char *jid, unsigned int port, int ssl, const char *pass);
void jb_disconnect(void);
void jb_keepalive();
void jb_main();
void jb_send_msg(const char *, const char *);

#endif /* __JABGLUE_H__ */
