/*
 * xmpp_helper.c    -- Jabber protocol helper functions
 *
 * Copyright (C) 2008-2010 Frank Zschockelt <mcabber@freakysoft.de>
 * Copyright (C) 2005-2010 Mikael Berthe <mikael@lilotux.net>
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>   // snprintf

#include "xmpp_helper.h"
#include "settings.h"
#include "utils.h"
#include "caps.h"
#include "logprint.h"
#include "config.h"

time_t iqlast; // last message/status change time

extern char *imstatus_showmap[];

struct xmpp_error xmpp_errors[] = {
  {XMPP_ERROR_REDIRECT,              "302",
    "Redirect",              "redirect",                "modify"},
  {XMPP_ERROR_BAD_REQUEST,           "400",
    "Bad Request",           "bad-request",             "modify"},
  {XMPP_ERROR_NOT_AUTHORIZED,        "401",
    "Not Authorized",        "not-authorized",          "auth"},
  {XMPP_ERROR_PAYMENT_REQUIRED,      "402",
    "Payment Required",      "payment-required",        "auth"},
  {XMPP_ERROR_FORBIDDEN,             "403",
    "Forbidden",             "forbidden",               "auth"},
  {XMPP_ERROR_NOT_FOUND,             "404",
    "Not Found",             "item-not-found",          "cancel"},
  {XMPP_ERROR_NOT_ALLOWED,           "405",
    "Not Allowed",           "not-allowed",             "cancel"},
  {XMPP_ERROR_NOT_ACCEPTABLE,        "406",
    "Not Acceptable",        "not-acceptable",          "modify"},
  {XMPP_ERROR_REGISTRATION_REQUIRED, "407",
    "Registration required", "registration-required",   "auth"},
  {XMPP_ERROR_REQUEST_TIMEOUT,       "408",
    "Request Timeout",       "remote-server-timeout",   "wait"},
  {XMPP_ERROR_CONFLICT,              "409",
    "Conflict",               "conflict",               "cancel"},
  {XMPP_ERROR_INTERNAL_SERVER_ERROR, "500",
    "Internal Server Error", "internal-server-error",   "wait"},
  {XMPP_ERROR_NOT_IMPLEMENTED,       "501",
    "Not Implemented",       "feature-not-implemented", "cancel"},
  {XMPP_ERROR_REMOTE_SERVER_ERROR,   "502",
    "Remote Server Error",   "service-unavailable",     "wait"},
  {XMPP_ERROR_SERVICE_UNAVAILABLE,   "503",
    "Service Unavailable",   "service-unavailable",     "cancel"},
  {XMPP_ERROR_REMOTE_SERVER_TIMEOUT, "504",
    "Remote Server Timeout", "remote-server-timeout",   "wait"},
  {XMPP_ERROR_DISCONNECTED,          "510",
    "Disconnected",          "service-unavailable",     "cancel"},
  {0, NULL, NULL, NULL, NULL}
};

#ifdef MODULES_ENABLE
static GSList *xmpp_additional_features = NULL;
static char *ver, *ver_notavail;

void xmpp_add_feature(const char *xmlns)
{
  if (xmlns) {
    ver = NULL;
    ver_notavail = NULL;
    xmpp_additional_features = g_slist_append(xmpp_additional_features,
                                              g_strdup(xmlns));
  }
}

void xmpp_del_feature(const char *xmlns)
{
  GSList *feature = xmpp_additional_features;
  while (feature) {
    if (!strcmp(feature->data, xmlns)) {
      ver = NULL;
      ver_notavail = NULL;
      g_free(feature->data);
      xmpp_additional_features = g_slist_delete_link(xmpp_additional_features,
                                                     feature);
      return;
    }
    feature = g_slist_next(feature);
  }
}
#endif

const gchar* lm_message_node_get_child_value(LmMessageNode *node,
                                             const gchar *child)
{
  LmMessageNode *tmp;
  tmp = lm_message_node_find_child(node, child);
  if (tmp) {
    const gchar *val = lm_message_node_get_value(tmp);
    return (val ? val : "");
  }
  return NULL;
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

// Maybe not a good idea, because it uses internals of loudmouth...
// It's used for rosternotes/bookmarks
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

LmMessage *lm_message_new_iq_from_query(LmMessage *m,
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
#ifndef MODULES_ENABLE
  static char *ver, *ver_notavail;
#endif

  if (ver && (status != notavail))
    return ver;
  if (ver_notavail)
    return ver_notavail;

  caps_add("");
  caps_set_identity("", "client", PACKAGE_STRING, "pc");
  caps_add_feature("", NS_DISCO_INFO);
  caps_add_feature("", NS_CAPS);
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
  caps_add_feature("", NS_X_CONFERENCE);
  if (!settings_opt_get_int("iq_last_disable") &&
      (!settings_opt_get_int("iq_last_disable_when_notavail") ||
       status != notavail))
    caps_add_feature("", NS_LAST);
#ifdef MODULES_ENABLE
  {
    GSList *el = xmpp_additional_features;
    while (el) {
      caps_add_feature("", el->data);
      el = g_slist_next(el);
    }
  }
#endif

  if (status == notavail) {
    ver_notavail = caps_generate();
    return ver_notavail;
  }

  ver = caps_generate();
  return ver;
}

LmMessageNode *lm_message_node_find_xmlns(LmMessageNode *node,
                                          const char *xmlns)
{
  LmMessageNode *x;
  const char *p;

  if (!node) return NULL;

  for (x = node->children ; x; x = x->next) {
    if ((p = lm_message_node_get_attribute(x, "xmlns")) && !strcmp(p, xmlns))
      break;
  }
  return x;
}

time_t lm_message_node_get_timestamp(LmMessageNode *node)
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
LmMessage *lm_message_new_presence(enum imstatus st,
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

#ifdef WITH_DEPRECATED_STATUS_INVISIBLE
    case invisible:
        lm_message_node_set_attribute(x->node, "type", "invisible");
        break;
#endif

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
  int i;

  for (i = 0; xmpp_errors[i].code; ++i) {
    if (xmpp_errors[i].code == code)
      return xmpp_errors[i].meaning;
  }
  return NULL;
}

//  display_server_error(x)
// Display the error to the user
// x: error tag xmlnode pointer
void display_server_error(LmMessageNode *x, const char *from)
{
  const char *desc = NULL, *errname = NULL, *s;
  char *sdesc, *tmp;

  if (!x) return;

  /* RFC3920:
   *    The <error/> element:
   *       o  MUST contain a child element corresponding to one of the defined
   *          stanza error conditions specified below; this element MUST be
   *          qualified by the 'urn:ietf:params:xml:ns:xmpp-stanzas' namespace.
   */
  if (x->children)
    errname = x->children->name;

  if (from)
    scr_LogPrint(LPRINT_LOGNORM, "Received error packet [%s] from <%s>",
                 (errname ? errname : ""), from);
  else
    scr_LogPrint(LPRINT_LOGNORM, "Received error packet [%s]",
                 (errname ? errname : ""));

  // For backward compatibility
  if (!errname && ((s = lm_message_node_get_attribute(x, "code")) != NULL)) {
    // Default message
    desc = defaulterrormsg(atoi(s));
  }

  // Error tag data is better, if available
  s = lm_message_node_get_value(x);
  if (s && *s) desc = s;

  // And sometimes there is a text message
  s = lm_message_node_get_child_value(x, "text");

  if (s && *s) desc = s;

  // If we still have no description, let's give up
  if (!desc || !*desc)
    return;

  // Strip trailing newlines
  sdesc = g_strdup(desc);
  for (tmp = sdesc; *tmp; tmp++) ;
  if (tmp > sdesc)
    tmp--;
  while (tmp >= sdesc && (*tmp == '\n' || *tmp == '\r'))
    *tmp-- = '\0';

  if (*sdesc)
    scr_LogPrint(LPRINT_LOGNORM, "Error message from server: %s", sdesc);
  g_free(sdesc);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
