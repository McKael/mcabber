/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Jabber
 *  Copyright (C) 1998-1999 The Jabber Team http://jabber.org/
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>

#include "libxode.h"
#include "../connwrap/connwrap.h"

#ifndef INCL_JABBER_H
#define INCL_JABBER_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------- */
/*                                                           */
/* JID structures & constants                                */
/*                                                           */
/* --------------------------------------------------------- */
#define JID_RESOURCE 1
#define JID_USER     2
#define JID_SERVER   4

typedef struct jid_struct
{
    pool               p;
    char*              resource;
    char*              user;
    char*              server;
    char*              full;
    struct jid_struct *next; /* for lists of jids */
} *jid;

jid     jid_new(pool p, char *idstr);          /* Creates a jabber id from the idstr */

void    jid_set(jid id, char *str, int item);  /* Individually sets jid components */
char*   jid_full(jid id);                      /* Builds a string type=user/resource@server from the jid data */
int     jid_cmp(jid a, jid b);                 /* Compares two jid's, returns 0 for perfect match */
int     jid_cmpx(jid a, jid b, int parts);     /* Compares just the parts specified as JID_|JID_ */
jid     jid_append(jid a, jid b);              /* Appending b to a (list), no dups */
xmlnode jid_xres(jid id);                      /* Returns xmlnode representation of the resource?query=string */
xmlnode jid_nodescan(jid id, xmlnode x);       /* Scans the children of the node for a matching jid attribute */
jid     jid_user(jid a);                       /* returns the same jid but just of the user@host part */


/* --------------------------------------------------------- */
/*                                                           */
/* JPacket structures & constants                            */
/*                                                           */
/* --------------------------------------------------------- */
#define JPACKET_UNKNOWN   0x00
#define JPACKET_MESSAGE   0x01
#define JPACKET_PRESENCE  0x02
#define JPACKET_IQ        0x04
#define JPACKET_S10N      0x08

#define JPACKET__UNKNOWN      0
#define JPACKET__NONE         1
#define JPACKET__ERROR        2
#define JPACKET__CHAT         3
#define JPACKET__GROUPCHAT    4
#define JPACKET__GET          5
#define JPACKET__SET          6
#define JPACKET__RESULT       7
#define JPACKET__SUBSCRIBE    8
#define JPACKET__SUBSCRIBED   9
#define JPACKET__UNSUBSCRIBE  10
#define JPACKET__UNSUBSCRIBED 11
#define JPACKET__AVAILABLE    12
#define JPACKET__UNAVAILABLE  13
#define JPACKET__PROBE        14
#define JPACKET__HEADLINE     15
#define JPACKET__INVISIBLE    16

typedef struct jpacket_struct
{
    unsigned char type;             /**< stanza type (JPACKET_*) */
    int           subtype;          /**< subtype of a stanza */
    int           flag;             /**< used by the session manager to flag messages, that are read from offline storage */
    void*         aux1;             /**< pointer to data passed around with a jpacket, multiple use inside jsm */
    xmlnode       x;                /**< xmlnode containing the stanza inside the jpacket */
    jid           to;               /**< destination of the stanza */
    jid           from;             /**< source address for the stanza */
    char*         iqns;             /**< pointer to the namespace inside an IQ stanza */
    xmlnode       iq;               /**< "content" of an iq stanza, pointer to the element in its own namespace */
    pool          p;                /**< memory pool used for this stanza */
} *jpacket, _jpacket;

jpacket jpacket_new(xmlnode x);     /* Creates a jabber packet from the xmlnode */
jpacket jpacket_reset(jpacket p);   /* Resets the jpacket values based on the xmlnode */
int     jpacket_subtype(jpacket p); /* Returns the subtype value (looks at xmlnode for it) */


/* --------------------------------------------------------- */
/*                                                           */
/* Presence Proxy DB structures & constants                  */
/*                                                           */
/* --------------------------------------------------------- */
typedef struct ppdb_struct
{
    jid     id;                /* entry data */
    int     pri;
    xmlnode x;
    struct ppdb_struct* user;  /* linked list for user@server */
    pool                p;     /* db-level data */
    struct ppdb_struct* next;
} _ppdb, *ppdb;

ppdb    ppdb_insert(ppdb db, jid id, xmlnode x); /* Inserts presence into the proxy */
xmlnode ppdb_primary(ppdb db, jid id);           /* Fetches the matching primary presence for the id */
void    ppdb_free(ppdb db);                      /* Frees the db and all entries */
xmlnode ppdb_get(ppdb db, jid id);               /* Called successively to return each presence xmlnode */
						 /*   for the id and children, returns NULL at the end */


/* --------------------------------------------------------- */
/*                                                           */
/* Simple Jabber Rate limit functions                        */
/*                                                           */
/* --------------------------------------------------------- */
typedef struct jlimit_struct
{
    char *key;
    int start;
    int points;
    int maxt, maxp;
    pool p;
} *jlimit, _jlimit;

jlimit jlimit_new(int maxt, int maxp);
void jlimit_free(jlimit r);
int jlimit_check(jlimit r, char *key, int points);


/* --------------------------------------------------------- */
/*                                                           */
/* Error structures & constants                              */
/*                                                           */
/* --------------------------------------------------------- */
typedef struct terror_struct
{
    int  code;
    char msg[64];
} terror;

#define TERROR_BAD           (terror){400,"Bad Request"}
#define TERROR_AUTH          (terror){401,"Unauthorized"}
#define TERROR_PAY           (terror){402,"Payment Required"}
#define TERROR_FORBIDDEN     (terror){403,"Forbidden"}
#define TERROR_NOTFOUND      (terror){404,"Not Found"}
#define TERROR_NOTALLOWED    (terror){405,"Not Allowed"}
#define TERROR_NOTACCEPTABLE (terror){406,"Not Acceptable"}
#define TERROR_REGISTER      (terror){407,"Registration Required"}
#define TERROR_REQTIMEOUT    (terror){408,"Request Timeout"}
#define TERROR_CONFLICT      (terror){409,"Conflict"}

#define TERROR_INTERNAL   (terror){500,"Internal Server Error"}
#define TERROR_NOTIMPL    (terror){501,"Not Implemented"}
#define TERROR_EXTERNAL   (terror){502,"Remote Server Error"}
#define TERROR_UNAVAIL    (terror){503,"Service Unavailable"}
#define TERROR_EXTTIMEOUT (terror){504,"Remote Server Timeout"}
#define TERROR_DISCONNECTED (terror){510,"Disconnected"}

/* we define this to signal that we support xterror */
#define HAS_XTERROR

typedef struct xterror_struct
{
    int  code;
    char msg[256];
    char type[9];
    char condition[64];
} xterror;

#define XTERROR_BAD		(xterror){400,"Bad Request","modify","bad-request"}
#define XTERROR_CONFLICT	(xterror){409,"Conflict","cancel","conflict"}
#define XTERROR_NOTIMPL		(xterror){501,"Not Implemented","cancel","feature-not-implemented"}
#define XTERROR_FORBIDDEN	(xterror){403,"Forbidden","auth","forbidden"}
#define XTERROR_GONE		(xterror){302,"Gone","modify","gone"}
#define XTERROR_INTERNAL	(xterror){500,"Internal Server Error","wait","internal-server-error"}
#define XTERROR_NOTFOUND	(xterror){404,"Not Found","cancel","item-not-found"}
#define XTERROR_JIDMALFORMED	(xterror){400,"Bad Request","modify","jid-malformed"}
#define XTERROR_NOTACCEPTABLE	(xterror){406,"Not Acceptable","modify","not-acceptable"}
#define XTERROR_NOTALLOWED	(xterror){405,"Not Allowed","cancel","not-allowed"}
#define XTERROR_AUTH		(xterror){401,"Unauthorized","auth","not-authorized"}
#define XTERROR_PAY		(xterror){402,"Payment Required","auth","payment-required"}
#define XTERROR_RECIPIENTUNAVAIL (xterror){404,"Receipient Is Unavailable","wait","recipient-unavailable"}
#define XTERROR_REDIRECT	(xterror){302,"Redirect","modify","redirect"}
#define XTERROR_REGISTER	(xterror){407,"Registration Required","auth","registration-required"}
#define XTERROR_REMOTENOTFOUND	(xterror){404,"Remote Server Not Found","cancel","remote-server-not-found"}
#define XTERROR_REMOTETIMEOUT	(xterror){504,"Remote Server Timeout","wait","remote-server-timeout"}
#define XTERROR_RESCONSTRAINT	(xterror){500,"Resource Constraint","wait","resource-constraint"}
#define XTERROR_UNAVAIL		(xterror){503,"Service Unavailable","cancel","service-unavailable"}
#define XTERROR_SUBSCRIPTIONREQ	(xterror){407,"Subscription Required","auth","subscription-required"}
#define XTERROR_UNDEF_CANCEL	(xterror){500,NULL,"cancel","undefined-condition"}
#define XTERROR_UNDEF_CONTINUE	(xterror){500,NULL,"continue","undefined-condition"}
#define XTERROR_UNDEF_MODIFY	(xterror){500,NULL,"modify","undefined-condition"}
#define XTERROR_UNDEF_AUTH	(xterror){500,NULL,"auth","undefined-condition"}
#define XTERROR_UNDEF_WAIT	(xterror){500,NULL,"wait","undefined-condition"}
#define XTERROR_UNEXPECTED	(xterror){400,"Unexpected Request","wait","unexpected-request"}

#define XTERROR_REQTIMEOUT	(xterror){408,"Request Timeout","wait","remote-server-timeout"}
#define XTERROR_EXTERNAL	(xterror){502,"Remote Server Error","wait","service-unavailable"}
#define XTERROR_EXTTIMEOUT	(xterror){504,"Remote Server Timeout","wait","remote-server-timeout"}
#define XTERROR_DISCONNECTED	(xterror){510,"Disconnected","cancel","service-unavailable"}

/* --------------------------------------------------------- */
/*                                                           */
/* Namespace constants                                       */
/*                                                           */
/* --------------------------------------------------------- */
#define NSCHECK(x,n) (j_strcmp(xmlnode_get_attrib(x,"xmlns"),n) == 0)

#define NS_CLIENT    "jabber:client"
#define NS_SERVER    "jabber:server"
#define NS_DIALBACK  "jabber:server:dialback"
#define NS_AUTH      "jabber:iq:auth"
#define NS_AUTH_CRYPT "jabber:iq:auth:crypt"
#define NS_REGISTER  "jabber:iq:register"
#define NS_ROSTER    "jabber:iq:roster"
#define NS_OFFLINE   "jabber:x:offline"
#define NS_AGENT     "jabber:iq:agent"
#define NS_AGENTS    "jabber:iq:agents"
#define NS_DELAY     "jabber:x:delay"
#define NS_VERSION   "jabber:iq:version"
#define NS_TIME      "jabber:iq:time"
#define NS_VCARD     "vcard-temp"
#define NS_PRIVATE   "jabber:iq:private"
#define NS_SEARCH    "jabber:iq:search"
#define NS_OOB       "jabber:iq:oob"
#define NS_XOOB      "jabber:x:oob"
#define NS_ADMIN     "jabber:iq:admin"
#define NS_FILTER    "jabber:iq:filter"
#define NS_AUTH_0K   "jabber:iq:auth:0k"
#define NS_BROWSE    "jabber:iq:browse"
#define NS_EVENT     "jabber:x:event"
#define NS_CONFERENCE "jabber:iq:conference"
#define NS_SIGNED    "jabber:x:signed"
#define NS_ENCRYPTED "jabber:x:encrypted"
#define NS_GATEWAY   "jabber:iq:gateway"
#define NS_LAST      "jabber:iq:last"
#define NS_ENVELOPE  "jabber:x:envelope"
#define NS_EXPIRE    "jabber:x:expire"
#define NS_XHTML     "http://www.w3.org/1999/xhtml"
#define NS_DISCO_INFO "http://jabber.org/protocol/disco#info"
#define NS_DISCO_ITEMS "http://jabber.org/protocol/disco#items"
#define NS_IQ_AUTH    "http://jabber.org/features/iq-auth"
#define NS_REGISTER_FEATURE "http://jabber.org/features/iq-register"
#define NS_MUC       "http://jabber.org/protocol/muc"
#define NS_CHATSTATES "http://jabber.org/protocol/chatstates"

#define NS_XDBGINSERT "jabber:xdb:ginsert"
#define NS_XDBNSLIST  "jabber:xdb:nslist"

#define NS_XMPP_STANZAS "urn:ietf:params:xml:ns:xmpp-stanzas"
#define NS_XMPP_TLS  "urn:ietf:params:xml:ns:xmpp-tls"
#define NS_XMPP_STREAMS "urn:ietf:params:xml:ns:xmpp-streams"

#define NS_JABBERD_STOREDPRESENCE "http://jabberd.org/ns/storedpresence"
#define NS_JABBERD_HISTORY "http://jabberd.org/ns/history"

/* --------------------------------------------------------- */
/*                                                           */
/* Message Types                                             */
/*                                                           */
/* --------------------------------------------------------- */
#define TMSG_NORMAL     "normal"
#define TMSG_ERROR      "error"
#define TMSG_CHAT       "chat"
#define TMSG_GROUPCHAT  "groupchat"
#define TMSG_HEADLINE   "headline"


/* --------------------------------------------------------- */
/*                                                           */
/* JUtil functions                                           */
/*                                                           */
/* --------------------------------------------------------- */
xmlnode jutil_presnew(int type, char *to, char *status); /* Create a skeleton presence packet */
xmlnode jutil_iqnew(int type, char *ns);                 /* Create a skeleton iq packet */
xmlnode jutil_msgnew(char *type, char *to, char *subj, char *body);
							 /* Create a skeleton message packet */
xmlnode jutil_header(char* xmlns, char* server);         /* Create a skeleton stream packet */
int     jutil_priority(xmlnode x);                       /* Determine priority of this packet */
void    jutil_tofrom(xmlnode x);                         /* Swaps to/from fields on a packet */
xmlnode jutil_iqresult(xmlnode x);                       /* Generate a skeleton iq/result, given a iq/query */
char*   jutil_timestamp(void);                           /* Get stringified timestamp */
void    jutil_error(xmlnode x, terror E);                /* Append an <error> node to x */
void    jutil_delay(xmlnode msg, char *reason);          /* Append a delay packet to msg */
char*   jutil_regkey(char *key, char *seed);             /* pass a seed to generate a key, pass the key again to validate (returns it) */


/* --------------------------------------------------------- */
/*                                                           */
/* JConn structures & functions                              */
/*                                                           */
/* --------------------------------------------------------- */
#define JCONN_STATE_OFF       0
#define JCONN_STATE_CONNECTED 1
#define JCONN_STATE_ON        2
#define JCONN_STATE_AUTH      3
#define JCONN_STATE_CONNECTING 4

typedef struct jconn_struct
{
    /* Core structure */
    pool        p;          /* Memory allocation pool */
    int         state;      /* Connection state flag */
    int         cw_state;   /* Low level connection state flag */
    int         fd;         /* Connection file descriptor */
    char        *server;    /* The server we'll connect too */
    int         port;
    int         ssl;
    jid         user;       /* User info */
    char        *pass;      /* User passwd */

    /* Stream stuff */
    int         id;         /* id counter for jab_getid() function */
    char        idbuf[9];   /* temporary storage for jab_getid() */
    char        *sid;       /* stream id from server, for digest auth */
    XML_Parser  parser;     /* Parser instance */
    xmlnode     current;    /* Current node in parsing instance.. */

    /* Event callback ptrs */
    void (*on_state)(struct jconn_struct *j, int state);
    void (*on_packet)(struct jconn_struct *j, jpacket p);
    void (*logger)(struct jconn_struct *j, int inout, const char *p);

} *jconn, jconn_struct;

typedef void (*jconn_state_h)(jconn j, int state);
typedef void (*jconn_packet_h)(jconn j, jpacket p);
typedef void (*jconn_logger)(jconn j, int inout, const char *p);

jconn jab_new(char *user, char *pass, char *server, int port, int ssl);
void jab_delete(jconn j);
void jab_state_handler(jconn j, jconn_state_h h);
void jab_packet_handler(jconn j, jconn_packet_h h);
void jab_logger(jconn j, jconn_logger h);
void jab_start(jconn j);
void jab_stop(jconn j);

int jab_getfd(jconn j);
jid jab_getjid(jconn j);
char *jab_getsid(jconn j);
char *jab_getid(jconn j);

void jab_send(jconn j, xmlnode x);
void jab_send_raw(jconn j, const char *str);
void jab_recv(jconn j);
void jab_poll(jconn j, int timeout);

char *jab_auth(jconn j);
int   jab_auth_mcabber(jconn j, xmlnode x);
char *jab_reg(jconn j);

#ifdef __cplusplus
}
#endif

#endif  /* INCL_JABBER_H */
