#ifndef __XMPPHELPER_H__
#define __XMPPHELPER_H__ 1

#include <time.h>
#include <loudmouth/loudmouth.h>

#include "xmpp.h"
#include "xmpp_defines.h"

extern time_t iqlast;           /* last message/status change time */

struct T_presence {
  enum imstatus st;
  const char *msg;
};

struct xmpp_error {
  guint code;
  const char *code_str;
  const char *meaning;
  const char *condition;
  const char *type;
};


LmMessageNode *lm_message_node_new(const gchar *name, const gchar *xmlns);
LmMessageNode *lm_message_node_find_xmlns(LmMessageNode *node,
                                          const char *xmlns);
const gchar* lm_message_node_get_child_value(LmMessageNode *node,
                                             const gchar *child);
void lm_message_node_hide(LmMessageNode *node);
void lm_message_node_insert_childnode(LmMessageNode *node,
                                      LmMessageNode *child);
void lm_message_node_deep_ref(LmMessageNode *node);
time_t lm_message_node_get_timestamp(LmMessageNode *node);

LmMessage *lm_message_new_iq_from_query(LmMessage *m, LmMessageSubType type);

LmMessage *lm_message_new_presence(enum imstatus st,
                                   const char *recipient, const char *msg);

const gchar* lm_message_get_from(LmMessage *m);
const gchar* lm_message_get_id(LmMessage *m);

void display_server_error(LmMessageNode *x);

/* XEP-0115 (Entity Capabilities) node */
const char *entity_version(enum imstatus status);

#endif

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
