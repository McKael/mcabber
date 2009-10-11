#ifndef __XMPPHELPER_H__
#define __XMPPHELPER_H__ 1

extern time_t iqlast;           /* last message/status change time */

struct T_presence {
  enum imstatus st;
  const char *msg;
};

LmMessageNode * lm_message_node_new(const gchar *name, const gchar *xmlns);
const gchar* lm_message_node_get_child_value(LmMessageNode * node,
                                             const gchar *child);
void lm_message_node_hide(LmMessageNode * node);
void lm_message_node_insert_childnode(LmMessageNode * node,
                                      LmMessageNode *child);
void lm_message_node_deep_ref(LmMessageNode * node);

/* XEP-0115 (Entity Capabilities) node */
#define MCABBER_CAPS_NODE "http://mcabber.com/caps"
const char *entity_version(void);

#endif
