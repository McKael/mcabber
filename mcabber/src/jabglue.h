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

jconn jb_connect(const char *servername, unsigned int port, int ssl,
                 const char *jid, const char *pass,
                 const char *resource);
void jb_disconnect(void);
void jb_keepalive();
void jb_main();
//int  jb_status();

#endif /* __JABGLUE_H__ */
