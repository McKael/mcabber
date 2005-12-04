#ifndef __JAB_PRIV_H__
#define __JAB_PRIV_H__ 1

/* This header file declares functions used by jab*.c only. */

#include "jabglue.h"

#define JABBER_AGENT_GROUP "Jabber Agents"

static enum {
  STATE_CONNECTING,
  STATE_GETAUTH,
  STATE_SENDAUTH,
  STATE_LOGGED
} jstate;

struct T_presence {
  enum imstatus st;
  const char *msg;
};

extern int regmode, regdone;
extern int s_id;

char *jidtodisp(const char *jid);
void handle_packet_iq(jconn conn, char *type, char *from, xmlnode xmldata);
void display_server_error(xmlnode x);

#endif /* __JAB_PRIV_H__ */

/* vim: set expandtab cindent cinoptions=>2:2(0:  For Vim users... */
