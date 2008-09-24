/*
 * xmpp_helper.c    -- Jabber protocol helper functions
 *
 * Copyright (C) 2008-2009 Frank Zschockelt <mcabber@freakysoft.de>
 * Copyright (C) 2005-2009 Mikael Berthe <mikael@lilotux.net>
 * Some parts initially came from the centericq project:
 * Copyright (C) 2002-2005 by Konstantin Klyagin <konst@konst.org.ua>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "xmpp_helper.h"

time_t iqlast; // last message/status change time

const gchar* lm_message_node_get_child_value(LmMessageNode *node,
                                             const gchar *child)
{
  LmMessageNode *tmp;
  tmp = lm_message_node_find_child(node, child);
  if (tmp)
    return lm_message_node_get_value(tmp);
  else return NULL;
}

static LmMessageNode *hidden = NULL;

void lm_message_node_hide(LmMessageNode *node)
{
  LmMessageNode *parent = node->parent, *prev_sibling = node->prev;

  if (hidden) {
    hidden->children = hidden->next = hidden->prev = hidden->parent = NULL;
    lm_message_node_unref(hidden);
  }

  if (parent->children == node)
    parent->children = node->next;
  if (prev_sibling)
    prev_sibling->next = node->next;
  if (node->next)
    node->next->prev = prev_sibling;
}

//maybe not a good idea, because it uses internals of loudmouth...
//it's used for rosternotes/bookmarks
LmMessageNode *lm_message_node_new(const gchar *name, const gchar *xmlns)
{
  LmMessageNode *node;

  node = g_new0 (LmMessageNode, 1);
  node->name       = g_strdup (name);
  node->value      = NULL;
  node->raw_mode   = FALSE;
  node->attributes = NULL;
  node->next       = NULL;
  node->prev       = NULL;
  node->parent     = NULL;
  node->children   = NULL;

  node->ref_count  = 1;
  lm_message_node_set_attribute(node, "xmlns", xmlns);
  return node;
}

void lm_message_node_insert_childnode(LmMessageNode *node,
                                      LmMessageNode *child)
{
  LmMessageNode *x;
  lm_message_node_deep_ref(child);

  if (node->children == NULL)
    node->children = child;
  else {
    for (x = node->children; x->next; x = x->next)
      ;
    x->next = child;
  }
}

void lm_message_node_deep_ref(LmMessageNode *node)
{
  if (node == NULL)
    return;
  lm_message_node_ref(node);
  lm_message_node_deep_ref(node->next);
  lm_message_node_deep_ref(node->children);
}

const gchar* lm_message_get_from(LmMessage *m)
{
  return lm_message_node_get_attribute(m->node, "from");
}

const gchar* lm_message_get_id(LmMessage *m)
{
  return lm_message_node_get_attribute(m->node, "id");
}

static LmMessage *lm_message_new_iq_from_query(LmMessage *m,
                                               LmMessageSubType type)
{
  LmMessage *new;
  const char *from = lm_message_node_get_attribute(m->node, "from");
  const char *id = lm_message_node_get_attribute(m->node, "id");

  new = lm_message_new_with_sub_type(from, LM_MESSAGE_TYPE_IQ,
                                     type);
  if (id)
    lm_message_node_set_attribute(new->node, "id", id);

  return new;
}

//  entity_version(enum imstatus status)
// Return a static version string for Entity Capabilities.
// It should be specific to the client version, please change the id
// if you alter mcabber's disco support (or add something to the version
// number) so that it doesn't conflict with the official client.
const char *entity_version(enum imstatus status)
{
  static char *ver, *ver_notavail;

  if (ver && (status != notavail))
    return ver;
  if (ver_notavail)
    return ver_notavail;

  caps_add("");
  caps_set_identity("", "client", PACKAGE_STRING, "pc");
  caps_add_feature("", NS_DISCO_INFO);
  caps_add_feature("", NS_MUC);
  // advertise ChatStates only if they aren't disabled
  if (!settings_opt_get_int("disable_chatstates"))
   caps_add_feature("", NS_CHATSTATES);
  caps_add_feature("", NS_TIME);
  caps_add_feature("", NS_XMPP_TIME);
  caps_add_feature("", NS_VERSION);
  caps_add_feature("", NS_PING);
  caps_add_feature("", NS_COMMANDS);
  caps_add_feature("", NS_RECEIPTS);
  if (!settings_opt_get_int("iq_last_disable") &&
      (!settings_opt_get_int("iq_last_disable_when_notavail") ||
       status != notavail))
   caps_add_feature("", NS_LAST);

  if (status == notavail) {
    ver_notavail = caps_generate();
    return ver_notavail;
  }

  ver = caps_generate();
  return ver;
}

inline static LmMessageNode *lm_message_node_find_xmlns(LmMessageNode *node,
                                                        const char *xmlns)
{
  LmMessageNode *x;
  const char *p;

  for (x = node->children ; x; x = x->next) {
    if ((p = lm_message_node_get_attribute(x, "xmlns")) && !strcmp(p, xmlns))
      break;
  }
  return x;
}

static time_t lm_message_node_get_timestamp(LmMessageNode *node)
{
  LmMessageNode *x;
  const char *p;

  x = lm_message_node_find_xmlns(node, NS_XMPP_DELAY);
  if (x && (!strcmp(x->name, "delay")) &&
      (p = lm_message_node_get_attribute(x, "stamp")) != NULL)
    return from_iso8601(p, 1);
  x = lm_message_node_find_xmlns(node, NS_DELAY);
  if (x && (p = lm_message_node_get_attribute(x, "stamp")) != NULL)
    return from_iso8601(p, 1);
  return 0;
}

//  lm_message_new_presence(status, recipient, message)
// Create an xmlnode with default presence attributes
// Note: the caller must free the node after use
static LmMessage *lm_message_new_presence(enum imstatus st,
                                           const char *recipient,
                                           const char *msg)
{
  unsigned int prio;
  LmMessage *x = lm_message_new(recipient, LM_MESSAGE_TYPE_PRESENCE);

  switch(st) {
    case away:
    case notavail:
    case dontdisturb:
    case freeforchat:
        lm_message_node_add_child(x->node, "show", imstatus_showmap[st]);
        break;

    case invisible:
        lm_message_node_set_attribute(x->node, "type", "invisible");
        break;

    case offline:
        lm_message_node_set_attribute(x->node, "type", "unavailable");
        break;

    default:
        break;
  }

  if (st == away || st == notavail)
    prio = settings_opt_get_int("priority_away");
  else
    prio = settings_opt_get_int("priority");

  if (prio) {
    char strprio[8];
    snprintf(strprio, 8, "%d", (int)prio);
    lm_message_node_add_child(x->node, "priority", strprio);
  }

  if (msg)
    lm_message_node_add_child(x->node, "status", msg);

  return x;
}

static const char *defaulterrormsg(guint code)
{
  int i = 0;

  for (i = 0; xmpp_errors[i].code; ++i) {
    if (xmpp_errors[i].code == code)
      return xmpp_errors[i].meaning;
  }
  return NULL;
}

//  display_server_error(x)
// Display the error to the user
// x: error tag xmlnode pointer
void display_server_error(LmMessageNode *x)
{
  const char *desc = NULL, *p=NULL, *s;
  char *sdesc, *tmp;
  int code = 0;

  if (!x) return;

  /* RFC3920:
   *    The <error/> element:
   *       o  MUST contain a child element corresponding to one of the defined
   *          stanza error conditions specified below; this element MUST be
   *          qualified by the 'urn:ietf:params:xml:ns:xmpp-stanzas' namespace.
   */
  if (x->children)
    p = x->children->name;
  if (p)
    scr_LogPrint(LPRINT_LOGNORM, "Received error packet [%s]", p);

  // For backward compatibility
  if ((s = lm_message_node_get_attribute(x, "code")) != NULL) {
    code = atoi(s);
    // Default message
    desc = defaulterrormsg(code);
  }

  // Error tag data is better, if available
  s = lm_message_node_get_value(x);
  if (s && *s) desc = s;

  // And sometimes there is a text message
  s = lm_message_node_get_child_value(x, "text");

  if (s && *s) desc = s;

  // If we still have no description, let's give up
  if (!desc)
    return;

  // Strip trailing newlines
  sdesc = g_strdup(desc);
  for (tmp = sdesc; *tmp; tmp++) ;
  if (tmp > sdesc)
    tmp--;
  while (tmp >= sdesc && (*tmp == '\n' || *tmp == '\r'))
    *tmp-- = '\0';

  scr_LogPrint(LPRINT_LOGNORM, "Error code from server: %d %s", code, sdesc);
  g_free(sdesc);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
