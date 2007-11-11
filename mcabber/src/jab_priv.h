#ifndef __JAB_PRIV_H__
#define __JAB_PRIV_H__ 1

/* This header file declares functions used by jab*.c only. */

#include "jabglue.h"
#include "events.h"

/* XEP-0115 (Entity Capabilities) node */
#define MCABBER_CAPS_NODE   "http://mcabber.lilotux.net/caps"

#define JABBER_AGENT_GROUP  "Jabber Agents"

enum enum_jstate {
  STATE_CONNECTING,
  STATE_GETAUTH,
  STATE_SENDAUTH,
  STATE_LOGGED
};

struct T_presence {
  enum imstatus st;
  const char *msg;
};


#define IQS_DEFAULT_TIMEOUT 90U
#define IQS_MAX_TIMEOUT     600U

#define IQS_CONTEXT_RESULT  0U  /* Normal result should be zero */
#define IQS_CONTEXT_TIMEOUT 1U
#define IQS_CONTEXT_ERROR   2U

extern enum enum_jstate jstate;
extern xmlnode bookmarks, rosternotes;

const char *entity_version(void);

extern time_t iqlast;           /* last message/status change time */

char *jidtodisp(const char *fjid);
void handle_packet_iq(jconn conn, char *type, char *from, xmlnode xmldata);
void display_server_error(xmlnode x);
eviqs *iqs_new(guint8 type, const char *ns, const char *prefix, time_t timeout);
int  iqs_del(const char *iqid);
int  iqs_callback(const char *iqid, xmlnode xml_result, guint iqcontext);
void iqs_check_timeout(time_t now_t);
int  iqscallback_auth(eviqs *iqp, xmlnode xml_result, guint iqcontext);
void request_version(const char *fulljid);
void request_time(const char *fulljid);
void request_last(const char *fulljid);
void request_vcard(const char *barejid);
void send_storage_bookmarks(void);
void send_storage_rosternotes(void);

#endif /* __JAB_PRIV_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
