/*
 * xmpp.c       -- Jabber protocol handling
 *
 * Copyright (C) 2008-2009 Frank Zschockelt <mcabber@freakysoft.de>
 * Copyright (C) 2005-2009 Mikael Berthe <mikael@lilotux.net>
 * Parts come from the centericq project:
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
#include <stdlib.h>
#include <string.h>

#include "xmpp.h"
#include "xmpp_helper.h"
#include "xmpp_iq.h"
#include "xmpp_iqrequest.h"
#include "xmpp_muc.h"
#include "xmpp_s10n.h"
#include "caps.h"
#include "events.h"
#include "histolog.h"
#include "hooks.h"
#include "otr.h"
#include "roster.h"
#include "screen.h"
#include "settings.h"
#include "utils.h"

#define RECONNECTION_TIMEOUT    60L

LmConnection* lconnection;
static guint AutoConnection;

inline void update_last_use(void);
inline gboolean xmpp_reconnect();

enum imstatus mystatus = offline;
static enum imstatus mywantedstatus = available;
gchar *mystatusmsg;

char imstatus2char[imstatus_size+1] = {
    '_', 'o', 'f', 'd', 'n', 'a', 'i', '\0'
};

char *imstatus_showmap[] = {
  "",
  "",
  "chat",
  "dnd",
  "xa",
  "away",
  ""
};

LmMessageNode *bookmarks = NULL;
LmMessageNode *rosternotes = NULL;

static struct IqHandlers
{
  const gchar *xmlns;
  LmHandleMessageFunction handler;
} iq_handlers[] = {
  {NS_PING,       &handle_iq_ping},
  {NS_VERSION,    &handle_iq_version},
  {NS_TIME,       &handle_iq_time},
  {NS_ROSTER,     &handle_iq_roster},
  {NS_XMPP_TIME,  &handle_iq_time202},
  {NS_LAST,       &handle_iq_last},
  {NS_DISCO_INFO, &handle_iq_disco_info},
  {NS_DISCO_ITEMS,&handle_iq_disco_items},
  {NS_COMMANDS,   &handle_iq_commands},
  {NS_VCARD,      &handle_iq_vcard},
  {NULL, NULL}
};

void update_last_use(void)
{
  iqlast = time(NULL);
}

// Note: the caller should check the jid is correct
void xmpp_addbuddy(const char *bjid, const char *name, const char *group)
{
  LmMessageNode *query, *y;
  LmMessage *iq;
  char *cleanjid;

  if (!lm_connection_is_authenticated(lconnection)) return;

  cleanjid = jidtodisp(bjid); // Stripping resource, just in case...

  // We don't check if the jabber user already exists in the roster,
  // because it allows to re-ask for notification.

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);
  query = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_ROSTER);
  y = lm_message_node_add_child(query, "item", NULL);
  lm_message_node_set_attribute(y, "jid", cleanjid);

  if (name)
    lm_message_node_set_attribute(y, "name", name);

  if (group)
    lm_message_node_add_child(y, "group", group);

  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);

  xmpp_send_s10n(cleanjid, LM_MESSAGE_SUB_TYPE_SUBSCRIBE);

  roster_add_user(cleanjid, name, group, ROSTER_TYPE_USER, sub_pending, -1);
  g_free(cleanjid);
  buddylist_build();

  update_roster = TRUE;
}

void xmpp_updatebuddy(const char *bjid, const char *name, const char *group)
{
  LmMessage *iq;
  LmMessageNode *x;
  char *cleanjid;

  if (!lm_connection_is_authenticated(lconnection)) return;

  // XXX We should check name's and group's correctness

  cleanjid = jidtodisp(bjid); // Stripping resource, just in case...

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);
  x = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(x, "xmlns", NS_ROSTER);
  x = lm_message_node_add_child(x, "item", NULL);
  lm_message_node_set_attributes(x,
                                 "jid", cleanjid,
                                 "name", name,
                                 NULL);

  if (group)
    lm_message_node_add_child(x, "group", group);

  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);
  g_free(cleanjid);
}

void xmpp_delbuddy(const char *bjid)
{
  LmMessageNode *y, *z;
  LmMessage *iq;
  char *cleanjid;

  if (!lm_connection_is_authenticated(lconnection)) return;

  cleanjid = jidtodisp(bjid); // Stripping resource, just in case...

  // If the current buddy is an agent, unsubscribe from it
  if (roster_gettype(cleanjid) == ROSTER_TYPE_AGENT) {
    scr_LogPrint(LPRINT_LOGNORM, "Unregistering from the %s agent", cleanjid);

    iq = lm_message_new_with_sub_type(cleanjid, LM_MESSAGE_TYPE_IQ,
                                      LM_MESSAGE_SUB_TYPE_SET);
    y = lm_message_node_add_child(iq->node, "query", NULL);
    lm_message_node_set_attribute(y, "xmlns", NS_REGISTER);
    lm_message_node_add_child(y, "remove", NULL);
    lm_connection_send(lconnection, iq, NULL);
    lm_message_unref(iq);
  }

  // Cancel the subscriptions
  xmpp_send_s10n(cleanjid, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED); //cancel "from"
  xmpp_send_s10n(cleanjid, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);  //cancel "to"

  // Ask for removal from roster
  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  y = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(y, "xmlns", NS_ROSTER);
  z = lm_message_node_add_child(y, "item", NULL);
  lm_message_node_set_attributes(z,
                                 "jid", cleanjid,
                                 "subscription", "remove",
                                 NULL);
  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);

  roster_del_user(cleanjid);
  g_free(cleanjid);
  buddylist_build();

  update_roster = TRUE;
}

void xmpp_request(const char *fjid, enum iqreq_type reqtype)
{
  GSList *resources, *p_res;
  GSList *roster_elt;
  const char *strreqtype, *xmlns;

  if (reqtype == iqreq_version) {
    xmlns = NS_VERSION;
    strreqtype = "version";
  } else if (reqtype == iqreq_time) {
    xmlns = NS_TIME;
    strreqtype = "time";
  } else if (reqtype == iqreq_last) {
    xmlns = NS_LAST;
    strreqtype = "last";
  } else if (reqtype == iqreq_vcard) {
    xmlns = NS_VCARD;
    strreqtype = "vCard";
    // Special case
  } else
    return;

  if (strchr(fjid, JID_RESOURCE_SEPARATOR)) {
    // This is a full JID
    xmpp_iq_request(fjid, xmlns);
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fjid);
    return;
  }

  // The resource has not been specified
  roster_elt = roster_find(fjid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_ROOM);
  if (!roster_elt) {
    scr_LogPrint(LPRINT_NORMAL, "No known resource for <%s>...", fjid);
    xmpp_iq_request(fjid, xmlns); // Let's send a request anyway...
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fjid);
    return;
  }

  // Send a request to each resource
  resources = buddy_getresources(roster_elt->data);
  if (!resources) {
    scr_LogPrint(LPRINT_NORMAL, "No known resource for <%s>...", fjid);
    xmpp_iq_request(fjid, xmlns); // Let's send a request anyway...
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fjid);
  }
  for (p_res = resources ; p_res ; p_res = g_slist_next(p_res)) {
    gchar *fulljid;
    fulljid = g_strdup_printf("%s/%s", fjid, (char*)p_res->data);
    xmpp_iq_request(fulljid, xmlns);
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fulljid);
    g_free(fulljid);
    g_free(p_res->data);
  }
  g_slist_free(resources);
}

static LmHandlerResult cb_xep184(LmMessageHandler *h, LmConnection *c,
                                 LmMessage *m, gpointer user_data)
{
  char *from = jidtodisp(lm_message_get_from(m));
  scr_RemoveReceiptFlag(from, h);
  g_free(from);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

//  xmpp_send_msg(jid, text, type, subject,
//                otrinject, *encrypted, type_overwrite)
// When encrypted is not NULL, the function set *encrypted to 1 if the
// message has been PGP-encrypted.  If encryption enforcement is set and
// encryption fails, *encrypted is set to -1.
void xmpp_send_msg(const char *fjid, const char *text, int type,
                   const char *subject, gboolean otrinject, gint *encrypted,
                   LmMessageSubType type_overwrite, gpointer *xep184)
{
  LmMessage *x;
  LmMessageSubType subtype;
#ifdef HAVE_LIBOTR
  int otr_msg = 0;
#endif
#if defined HAVE_GPGME || defined JEP0022 || defined JEP0085
  char *rname, *barejid;
  GSList *sl_buddy;
#endif
#if defined JEP0022 || defined JEP0085
  LmMessageNode *event;
  guint use_jep85 = 0;
  struct jep0085 *jep85 = NULL;
#endif
  gchar *enc = NULL;

  if (encrypted)
    *encrypted = 0;

  if (!lm_connection_is_authenticated(lconnection))
    return;

  if (!text && type == ROSTER_TYPE_USER)
    return;

  if (type_overwrite != LM_MESSAGE_SUB_TYPE_NOT_SET)
    subtype = type_overwrite;
  else {
    if (type == ROSTER_TYPE_ROOM)
      subtype = LM_MESSAGE_SUB_TYPE_GROUPCHAT;
    else
      subtype = LM_MESSAGE_SUB_TYPE_CHAT;
  }

#if defined HAVE_GPGME || defined HAVE_LIBOTR || \
    defined JEP0022 || defined JEP0085
  rname = strchr(fjid, JID_RESOURCE_SEPARATOR);
  barejid = jidtodisp(fjid);
  sl_buddy = roster_find(barejid, jidsearch, ROSTER_TYPE_USER);

  // If we can get a resource name, we use it.  Else we use NULL,
  // which hopefully will give us the most likely resource.
  if (rname)
    rname++;

#ifdef HAVE_LIBOTR
  if (otr_enabled() && !otrinject) {
    if (type == ROSTER_TYPE_USER) {
      otr_msg = otr_send((char **)&text, barejid);
      if (!text) {
        g_free(barejid);
        if (encrypted)
          *encrypted = -1;
        return;
      }
    }
    if (otr_msg && encrypted)
      *encrypted = ENCRYPTED_OTR;
  }
#endif

#ifdef HAVE_GPGME
  if (type == ROSTER_TYPE_USER && sl_buddy && gpg_enabled()) {
    if (!settings_pgp_getdisabled(barejid)) { // not disabled for this contact?
      guint force;
      struct pgp_data *res_pgpdata;
      force = settings_pgp_getforce(barejid);
      res_pgpdata = buddy_resource_pgp(sl_buddy->data, rname);
      if (force || (res_pgpdata && res_pgpdata->sign_keyid)) {
        /* Remote client has PGP support (we have a signature)
         * OR encryption is enforced (force = TRUE).
         * If the contact has a specific KeyId, we'll use it;
         * if not, we'll use the key used for the signature.
         * Both keys should match, in theory (cf. XEP-0027). */
        const char *key;
        key = settings_pgp_getkeyid(barejid);
        if (!key && res_pgpdata)
          key = res_pgpdata->sign_keyid;
        if (key)
          enc = gpg_encrypt(text, key);
        if (!enc && force) {
          if (encrypted)
            *encrypted = -1;
          g_free(barejid);
          return;
        }
      }
    }
  }
#endif // HAVE_GPGME

  g_free(barejid);
#endif // HAVE_GPGME || defined JEP0022 || defined JEP0085

  x = lm_message_new_with_sub_type(fjid, LM_MESSAGE_TYPE_MESSAGE, subtype);
  lm_message_node_add_child(x->node, "body",
                            enc ? "This message is PGP-encrypted." : text);

  if (subject)
    lm_message_node_add_child(x->node, "subject", subject);

  if (enc) {
    LmMessageNode *y;
    y = lm_message_node_add_child(x->node, "x", enc);
    lm_message_node_set_attribute(y, "xmlns", NS_ENCRYPTED);
    if (encrypted)
      *encrypted = ENCRYPTED_PGP;
    g_free(enc);
  }

  //XEP-0184: Message Receipts
  if (sl_buddy && rname && xep184 &&
      caps_has_feature(buddy_resource_getcaps(sl_buddy->data, rname),
                       NS_RECEIPTS)) {
    lm_message_node_set_attribute
            (lm_message_node_add_child(x->node, "request", NULL),
             "xmlns", NS_RECEIPTS);
    *xep184 = lm_message_handler_new(cb_xep184, NULL, NULL);
  }

#if defined JEP0022 || defined JEP0085
  // If typing notifications are disabled, we can skip all this stuff...
  if (chatstates_disabled || type == ROSTER_TYPE_ROOM)
    goto xmpp_send_msg_no_chatstates;

  if (sl_buddy)
    jep85 = buddy_resource_jep85(sl_buddy->data, rname);
#endif

#ifdef JEP0085
  /* JEP-0085 5.1
   * "Until receiving a reply to the initial content message (or a standalone
   * notification) from the Contact, the User MUST NOT send subsequent chat
   * state notifications to the Contact."
   * In our implementation support is initially "unknown", then it's "probed"
   * and can become "ok".
   */
  if (jep85 && (jep85->support == CHATSTATES_SUPPORT_OK ||
                jep85->support == CHATSTATES_SUPPORT_UNKNOWN)) {
    event = lm_message_node_add_child(x->node, "active", NULL);
    lm_message_node_set_attribute(event, "xmlns", NS_CHATSTATES);
    if (jep85->support == CHATSTATES_SUPPORT_UNKNOWN)
      jep85->support = CHATSTATES_SUPPORT_PROBED;
    else
      use_jep85 = 1;
    jep85->last_state_sent = ROSTER_EVENT_ACTIVE;
  }
#endif
#ifdef JEP0022
  /* JEP-22
   * If the Contact supports JEP-0085, we do not use JEP-0022.
   * If not, we try to fall back to JEP-0022.
   */
  if (!use_jep85) {
    struct jep0022 *jep22 = NULL;
    event = lm_message_node_add_child(x->node, "x", NULL);
    lm_message_node_set_attribute(event, "xmlns", NS_EVENT);
    lm_message_node_add_child(event, "composing", NULL);

    if (sl_buddy)
      jep22 = buddy_resource_jep22(sl_buddy->data, rname);
    if (jep22)
      jep22->last_state_sent = ROSTER_EVENT_ACTIVE;

    // An id is mandatory when using JEP-0022.
    if (text || subject) {
      const gchar *msgid = lm_message_get_id(x);
      // Let's update last_msgid_sent
      if (jep22) {
        g_free(jep22->last_msgid_sent);
        jep22->last_msgid_sent = g_strdup(msgid);
      }
    }
  }
#endif

xmpp_send_msg_no_chatstates:
  if (mystatus != invisible)
    update_last_use();
  if (xep184 && *xep184) {
    lm_connection_send_with_reply(lconnection, x, *xep184, NULL);
    lm_message_handler_unref(*xep184);
  } else
    lm_connection_send(lconnection, x, NULL);
  lm_message_unref(x);
}

#ifdef JEP0085
//  xmpp_send_jep85_chatstate()
// Send a JEP-85 chatstate.
static void xmpp_send_jep85_chatstate(const char *bjid, const char *resname,
                                      guint state)
{
  LmMessage *m;
  LmMessageNode *event;
  GSList *sl_buddy;
  const char *chattag;
  char *rjid, *fjid = NULL;
  struct jep0085 *jep85 = NULL;

  if (!lm_connection_is_authenticated(lconnection)) return;

  sl_buddy = roster_find(bjid, jidsearch, ROSTER_TYPE_USER);

  // If we have a resource name, we use it.  Else we use NULL,
  // which hopefully will give us the most likely resource.
  if (sl_buddy)
    jep85 = buddy_resource_jep85(sl_buddy->data, resname);

  if (!jep85 || (jep85->support != CHATSTATES_SUPPORT_OK))
    return;

  if (state == jep85->last_state_sent)
    return;

  if (state == ROSTER_EVENT_ACTIVE)
    chattag = "active";
  else if (state == ROSTER_EVENT_COMPOSING)
    chattag = "composing";
  else if (state == ROSTER_EVENT_PAUSED)
    chattag = "paused";
  else {
    scr_LogPrint(LPRINT_LOGNORM, "Error: unsupported JEP-85 state (%d)", state);
    return;
  }

  jep85->last_state_sent = state;

  if (resname)
    fjid = g_strdup_printf("%s/%s", bjid, resname);

  rjid = resname ? fjid : (char*)bjid;
  m = lm_message_new_with_sub_type(rjid, LM_MESSAGE_TYPE_MESSAGE,
                                   LM_MESSAGE_SUB_TYPE_CHAT);

  event = lm_message_node_add_child(m->node, chattag, NULL);
  lm_message_node_set_attribute(event, "xmlns", NS_CHATSTATES);

  lm_connection_send(lconnection, m, NULL);
  lm_message_unref(m);

  g_free(fjid);
}
#endif

#ifdef JEP0022
//  xmpp_send_jep22_event()
// Send a JEP-22 message event (delivered, composing...).
static void xmpp_send_jep22_event(const char *fjid, guint type)
{
  LmMessage *x;
  LmMessageNode *event;
  const char *msgid;
  char *rname, *barejid;
  GSList *sl_buddy;
  struct jep0022 *jep22 = NULL;
  guint jep22_state;

  if (!lm_connection_is_authenticated(lconnection)) return;

  rname = strchr(fjid, JID_RESOURCE_SEPARATOR);
  barejid = jidtodisp(fjid);
  sl_buddy = roster_find(barejid, jidsearch, ROSTER_TYPE_USER);
  g_free(barejid);

  // If we can get a resource name, we use it.  Else we use NULL,
  // which hopefully will give us the most likely resource.
  if (rname)
    rname++;
  if (sl_buddy)
    jep22 = buddy_resource_jep22(sl_buddy->data, rname);

  if (!jep22)
    return; // XXX Maybe we could try harder (other resources?)

  msgid = jep22->last_msgid_rcvd;

  // For composing events (composing, active, inactive, paused...),
  // JEP22 only has 2 states; we'll use composing and active.
  if (type == ROSTER_EVENT_COMPOSING)
    jep22_state = ROSTER_EVENT_COMPOSING;
  else if (type == ROSTER_EVENT_ACTIVE ||
           type == ROSTER_EVENT_PAUSED)
    jep22_state = ROSTER_EVENT_ACTIVE;
  else
    jep22_state = 0; // ROSTER_EVENT_NONE

  if (jep22_state) {
    // Do not re-send a same event
    if (jep22_state == jep22->last_state_sent)
      return;
    jep22->last_state_sent = jep22_state;
  }

  x = lm_message_new_with_sub_type(fjid, LM_MESSAGE_TYPE_MESSAGE,
                                   LM_MESSAGE_SUB_TYPE_CHAT);

  event = lm_message_node_add_child(x->node, "x", NULL);
  lm_message_node_set_attribute(event, "xmlns", NS_EVENT);
  if (type == ROSTER_EVENT_DELIVERED)
    lm_message_node_add_child(event, "delivered", NULL);
  else if (type == ROSTER_EVENT_COMPOSING)
    lm_message_node_add_child(event, "composing", NULL);
  lm_message_node_add_child(event, "id", msgid);

  lm_connection_send(lconnection, x, NULL);
  lm_message_unref(x);
}
#endif

//  xmpp_send_chatstate(buddy, state)
// Send a chatstate or event (JEP-22/85) according to the buddy's capabilities.
// The message is sent to one of the resources with the highest priority.
#if defined JEP0022 || defined JEP0085
void xmpp_send_chatstate(gpointer buddy, guint chatstate)
{
  const char *bjid;
#ifdef JEP0085
  GSList *resources, *p_res, *p_next;
  struct jep0085 *jep85 = NULL;
#endif
#ifdef JEP0022
  struct jep0022 *jep22;
#endif

  bjid = buddy_getjid(buddy);
  if (!bjid) return;

#ifdef JEP0085
  /* Send the chatstate to the last resource (which should have the highest
     priority).
     If chatstate is "active", send an "active" state to all resources
     which do not curently have this state.
   */
  resources = buddy_getresources(buddy);
  for (p_res = resources ; p_res ; p_res = p_next) {
    p_next = g_slist_next(p_res);
    jep85 = buddy_resource_jep85(buddy, p_res->data);
    if (jep85 && jep85->support == CHATSTATES_SUPPORT_OK) {
      // If p_next is NULL, this is the highest (prio) resource, i.e.
      // the one we are probably writing to.
      if (!p_next || (jep85->last_state_sent != ROSTER_EVENT_ACTIVE &&
                      chatstate == ROSTER_EVENT_ACTIVE))
        xmpp_send_jep85_chatstate(bjid, p_res->data, chatstate);
    }
    g_free(p_res->data);
  }
  g_slist_free(resources);
  // If the last resource had chatstates support when can return now,
  // we don't want to send a JEP22 event.
  if (jep85 && jep85->support == CHATSTATES_SUPPORT_OK)
    return;
#endif
#ifdef JEP0022
  jep22 = buddy_resource_jep22(buddy, NULL);
  if (jep22 && jep22->support == CHATSTATES_SUPPORT_OK) {
    xmpp_send_jep22_event(bjid, chatstate);
  }
#endif
}
#endif


//  chatstates_reset_probed(fulljid)
// If the JEP has been probed for this contact, set it back to unknown so
// that we probe it again.  The parameter must be a full jid (w/ resource).
#if defined JEP0022 || defined JEP0085
static void chatstates_reset_probed(const char *fulljid)
{
  char *rname, *barejid;
  GSList *sl_buddy;
  struct jep0085 *jep85;
  struct jep0022 *jep22;

  rname = strchr(fulljid, JID_RESOURCE_SEPARATOR);
  if (!rname++)
    return;

  barejid = jidtodisp(fulljid);
  sl_buddy = roster_find(barejid, jidsearch, ROSTER_TYPE_USER);
  g_free(barejid);

  if (!sl_buddy)
    return;

  jep85 = buddy_resource_jep85(sl_buddy->data, rname);
  jep22 = buddy_resource_jep22(sl_buddy->data, rname);

  if (jep85 && jep85->support == CHATSTATES_SUPPORT_PROBED)
    jep85->support = CHATSTATES_SUPPORT_UNKNOWN;
  if (jep22 && jep22->support == CHATSTATES_SUPPORT_PROBED)
    jep22->support = CHATSTATES_SUPPORT_UNKNOWN;
}
#endif

#ifdef HAVE_GPGME
//  keys_mismatch(key, expectedkey)
// Return TRUE if both keys are non-null and "expectedkey" doesn't match
// the end of "key".
// If one of the keys is null, return FALSE.
// If expectedkey is less than 8 bytes long, return TRUE.
//
// Example: keys_mismatch("C9940A9BB0B92210", "B0B92210") will return FALSE.
static bool keys_mismatch(const char *key, const char *expectedkey)
{
  int lk, lek;

  if (!expectedkey || !key)
    return FALSE;

  lk = strlen(key);
  lek = strlen(expectedkey);

  // If the expectedkey is less than 8 bytes long, this is probably a
  // user mistake so we consider it's a mismatch.
  if (lek < 8)
    return TRUE;

  if (lek < lk)
    key += lk - lek;

  return strcasecmp(key, expectedkey);
}
#endif

//  check_signature(barejid, resourcename, xmldata, text)
// Verify the signature (in xmldata) of "text" for the contact
// barejid/resourcename.
// xmldata is the 'jabber:x:signed' stanza.
// If the key id is found, the contact's PGP data are updated.
static void check_signature(const char *barejid, const char *rname,
                            LmMessageNode *node, const char *text)
{
#ifdef HAVE_GPGME
  const char *p, *key;
  GSList *sl_buddy;
  struct pgp_data *res_pgpdata;
  gpgme_sigsum_t sigsum;

  // All parameters must be valid
  if (!(node && barejid && rname && text))
    return;

  if (!gpg_enabled())
    return;

  // Get the resource PGP data structure
  sl_buddy = roster_find(barejid, jidsearch, ROSTER_TYPE_USER);
  if (!sl_buddy)
    return;
  res_pgpdata = buddy_resource_pgp(sl_buddy->data, rname);
  if (!res_pgpdata)
    return;

  if (!node->name || strcmp(node->name, "x")) //XXX: probably useless
    return; // We expect "<x xmlns='jabber:x:signed'>"

  // Get signature
  p = lm_message_node_get_value(node);
  if (!p)
    return;

  key = gpg_verify(p, text, &sigsum);
  if (key) {
    const char *expectedkey;
    char *buf;
    g_free(res_pgpdata->sign_keyid);
    res_pgpdata->sign_keyid = (char *)key;
    res_pgpdata->last_sigsum = sigsum;
    if (sigsum & GPGME_SIGSUM_RED) {
      buf = g_strdup_printf("Bad signature from <%s/%s>", barejid, rname);
      scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_INFO, 0);
      scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
      g_free(buf);
    }
    // Verify that the key id is the one we expect.
    expectedkey = settings_pgp_getkeyid(barejid);
    if (keys_mismatch(key, expectedkey)) {
      buf = g_strdup_printf("Warning: The KeyId from <%s/%s> doesn't match "
                            "the key you set up", barejid, rname);
      scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_INFO, 0);
      scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
      g_free(buf);
    }
  }
#endif
}

static LmSSLResponse ssl_cb(LmSSL *ssl, LmSSLStatus status, gpointer ud)
{
  scr_LogPrint(LPRINT_LOGNORM, "SSL status:%d", status);

  switch (status) {
  case LM_SSL_STATUS_NO_CERT_FOUND:
    scr_LogPrint(LPRINT_LOGNORM, "No certificate found!");
    break;
  case LM_SSL_STATUS_UNTRUSTED_CERT:
    scr_LogPrint(LPRINT_LOGNORM, "Certificate is not trusted!");
    break;
  case LM_SSL_STATUS_CERT_EXPIRED:
    scr_LogPrint(LPRINT_LOGNORM, "Certificate has expired!");
    break;
  case LM_SSL_STATUS_CERT_NOT_ACTIVATED:
    scr_LogPrint(LPRINT_LOGNORM, "Certificate has not been activated!");
    break;
  case LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH:
    scr_LogPrint(LPRINT_LOGNORM,
                 "Certificate hostname does not match expected hostname!");
    break;
  case LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH: {
    char fpr[49];
    fingerprint_to_hex((const unsigned char*)lm_ssl_get_fingerprint(ssl),
                       fpr);
    scr_LogPrint(LPRINT_LOGNORM,
              "Certificate fingerprint does not match expected fingerprint!");
    scr_LogPrint(LPRINT_LOGNORM, "Remote fingerprint: %s", fpr);

    scr_LogPrint(LPRINT_LOGNORM, "Expected fingerprint: %s",
                 settings_opt_get("ssl_fingerprint"));

    return LM_SSL_RESPONSE_STOP;
    break;
  }
  case LM_SSL_STATUS_GENERIC_ERROR:
    scr_LogPrint(LPRINT_LOGNORM, "Generic SSL error!");
    break;
  }

  if (!settings_opt_get_int("ssl_ignore_checks"))
    return LM_SSL_RESPONSE_CONTINUE;
  return LM_SSL_RESPONSE_STOP;
}

static void connection_auth_cb(LmConnection *connection, gboolean success,
                               gpointer user_data)
{
  if (success) {
    LmMessage *m;

    m = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_PRESENCE,
                                     LM_MESSAGE_SUB_TYPE_AVAILABLE);
    lm_connection_send(connection, m, NULL);

    lm_message_unref(m);
    xmpp_setprevstatus();
    xmpp_iq_request(NULL, NS_ROSTER);
    xmpp_request_storage("storage:bookmarks");
    xmpp_request_storage("storage:rosternotes");

    AutoConnection = TRUE;
  } else
    scr_LogPrint(LPRINT_LOGNORM, "Authentication failed");
}

gboolean xmpp_reconnect()
{
  if (!lm_connection_is_authenticated(lconnection))
    xmpp_connect();
  return FALSE;
}

static void _try_to_reconnect(void)
{
  if (AutoConnection)
    g_timeout_add_seconds(RECONNECTION_TIMEOUT, xmpp_reconnect, NULL);
}

static void connection_open_cb(LmConnection *connection, gboolean success,
                               gpointer user_data)
{
  GError *error;

  if (success) {
    const char *password, *resource;
    char *username;
    username   = jid_get_username(settings_opt_get("jid"));
    password   = settings_opt_get("password");
    resource   = strchr(lm_connection_get_jid(connection),
                        JID_RESOURCE_SEPARATOR);
    if (resource)
      resource++;

    if (!lm_connection_authenticate(lconnection, username, password, resource,
                                    connection_auth_cb, NULL, FALSE, &error)) {
      scr_LogPrint(LPRINT_LOGNORM, "Failed to authenticate: %s\n",
                   error->message);
      _try_to_reconnect();
    }
    g_free(username);
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "There was an error while connecting.");
    _try_to_reconnect();
  }
}

static void connection_close_cb(LmConnection *connection,
                                LmDisconnectReason reason,
                                gpointer user_data)
{
  const char *str;

  switch (reason) {
  case LM_DISCONNECT_REASON_OK:
          str = "LM_DISCONNECT_REASON_OK";
          break;
  case LM_DISCONNECT_REASON_PING_TIME_OUT:
          str = "LM_DISCONNECT_REASON_PING_TIME_OUT";
          break;
  case LM_DISCONNECT_REASON_HUP:
          str = "LM_DISCONNECT_REASON_HUP";
          break;
  case LM_DISCONNECT_REASON_ERROR:
          str = "LM_DISCONNECT_REASON_ERROR";
          break;
  case LM_DISCONNECT_REASON_UNKNOWN:
  default:
          str = "LM_DISCONNECT_REASON_UNKNOWN";
          break;
  }

  if (reason != LM_DISCONNECT_REASON_OK)
    _try_to_reconnect();

  // Free bookmarks
  if (bookmarks)
    lm_message_node_unref(bookmarks);
  bookmarks = NULL;
  // Free roster
  roster_free();
  if (rosternotes)
    lm_message_node_unref(rosternotes);
  rosternotes = NULL;
  // Update display
  update_roster = TRUE;
  scr_UpdateBuddyWindow();

  scr_LogPrint(LPRINT_NORMAL, "Disconnected, reason:%d->'%s'\n", reason, str);
}

static void handle_state_events(const char *from, LmMessageNode *node)
{
#if defined JEP0022 || defined JEP0085
  LmMessageNode *state_ns = NULL;
  const char *body;
  char *rname, *bjid;
  GSList *sl_buddy;
  guint events;
  struct jep0022 *jep22 = NULL;
  struct jep0085 *jep85 = NULL;
  enum {
    JEP_none,
    JEP_85,
    JEP_22
  } which_jep = JEP_none;

  rname = strchr(from, JID_RESOURCE_SEPARATOR);
  if (rname)
    ++rname;
  else
    rname = (char *)from + strlen(from);
  bjid  = jidtodisp(from);
  sl_buddy = roster_find(bjid, jidsearch, ROSTER_TYPE_USER);
  g_free(bjid);

  /* XXX Actually that's wrong, since it filters out server "offline"
     messages (for JEP-0022).  This JEP is (almost) deprecated so
     we don't really care. */
  if (!sl_buddy) {
    return;
  }

  /* Let's see chich JEP the contact uses.  If possible, we'll use
     JEP-85, if not we'll look for JEP-22 support. */
  events = buddy_resource_getevents(sl_buddy->data, rname);

  jep85 = buddy_resource_jep85(sl_buddy->data, rname);
  if (jep85) {
    state_ns = lm_message_node_find_xmlns(node, NS_CHATSTATES);
    if (state_ns)
      which_jep = JEP_85;
  }

  if (which_jep != JEP_85) { /* Fall back to JEP-0022 */
    jep22 = buddy_resource_jep22(sl_buddy->data, rname);
    if (jep22) {
      state_ns = lm_message_node_find_xmlns(node, NS_EVENT);
      if (state_ns)
        which_jep = JEP_22;
    }
  }

  if (!which_jep) { /* Sender does not use chat states */
    return;
  }

  body = lm_message_node_get_child_value(node, "body");

  if (which_jep == JEP_85) { /* JEP-0085 */
    jep85->support = CHATSTATES_SUPPORT_OK;

    if (!strcmp(state_ns->name, "composing")) {
      jep85->last_state_rcvd = ROSTER_EVENT_COMPOSING;
    } else if (!strcmp(state_ns->name, "active")) {
      jep85->last_state_rcvd = ROSTER_EVENT_ACTIVE;
    } else if (!strcmp(state_ns->name, "paused")) {
      jep85->last_state_rcvd = ROSTER_EVENT_PAUSED;
    } else if (!strcmp(state_ns->name, "inactive")) {
      jep85->last_state_rcvd = ROSTER_EVENT_INACTIVE;
    } else if (!strcmp(state_ns->name, "gone")) {
      jep85->last_state_rcvd = ROSTER_EVENT_GONE;
    }
    events = jep85->last_state_rcvd;
  } else {              /* JEP-0022 */
#ifdef JEP0022
    const char *msgid;
    jep22->support = CHATSTATES_SUPPORT_OK;
    jep22->last_state_rcvd = ROSTER_EVENT_NONE;

    msgid = lm_message_node_get_attribute(node, "id");

    if (lm_message_node_get_child(state_ns, "composing")) {
      // Clear composing if the message contains a body
      if (body)
        events &= ~ROSTER_EVENT_COMPOSING;
      else
        events |= ROSTER_EVENT_COMPOSING;
      jep22->last_state_rcvd |= ROSTER_EVENT_COMPOSING;

    } else {
      events &= ~ROSTER_EVENT_COMPOSING;
    }

    // Cache the message id
    g_free(jep22->last_msgid_rcvd);
    if (msgid)
      jep22->last_msgid_rcvd = g_strdup(msgid);
    else
      jep22->last_msgid_rcvd = NULL;

    if (lm_message_node_get_child(state_ns, "delivered")) {
      jep22->last_state_rcvd |= ROSTER_EVENT_DELIVERED;

      // Do we have to send back an ACK?
      if (body)
        xmpp_send_jep22_event(from, ROSTER_EVENT_DELIVERED);
    }
#endif
  }

  buddy_resource_setevents(sl_buddy->data, rname, events);

  update_roster = TRUE;
#endif
}

static void gotmessage(LmMessageSubType type, const char *from,
                       const char *body, const char *enc, const char *subject,
                       time_t timestamp, LmMessageNode *node_signed)
{
  char *bjid;
  const char *rname, *s;
  char *decrypted_pgp = NULL;
  char *decrypted_otr = NULL;
  int otr_msg = 0, free_msg = 0;

  bjid = jidtodisp(from);

  rname = strchr(from, JID_RESOURCE_SEPARATOR);
  if (rname) rname++;

#ifdef HAVE_GPGME
  if (enc && gpg_enabled()) {
    decrypted_pgp = gpg_decrypt(enc);
    if (decrypted_pgp) {
      body = decrypted_pgp;
    }
  }
  // Check signature of an unencrypted message
  if (node_signed && gpg_enabled())
    check_signature(bjid, rname, node_signed, decrypted_pgp);
#endif

#ifdef HAVE_LIBOTR
  if (otr_enabled()) {
    decrypted_otr = (char*)body;
    otr_msg = otr_receive(&decrypted_otr, bjid, &free_msg);
    if (!decrypted_otr) {
      goto gotmessage_return;
    }
    body = decrypted_otr;
  }
#endif

  // Check for unexpected groupchat messages
  // If we receive a groupchat message from a room we're not a member of,
  // this is probably a server issue and the best we can do is to send
  // a type unavailable.
  if (type == LM_MESSAGE_SUB_TYPE_GROUPCHAT && !roster_getnickname(bjid)) {
    // It shouldn't happen, probably a server issue
    GSList *room_elt;
    char *mbuf;

    mbuf = g_strdup_printf("Unexpected groupchat packet!");
    scr_LogPrint(LPRINT_LOGNORM, "%s", mbuf);
    scr_WriteIncomingMessage(bjid, mbuf, 0, HBB_PREFIX_INFO, 0);
    g_free(mbuf);

    // Send back an unavailable packet
    xmpp_setstatus(offline, bjid, "", TRUE);

    // MUC
    // Make sure this is a room (it can be a conversion user->room)
    room_elt = roster_find(bjid, jidsearch, 0);
    if (!room_elt) {
      room_elt = roster_add_user(bjid, NULL, NULL, ROSTER_TYPE_ROOM,
                                 sub_none, -1);
    } else {
      buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);
    }

    buddylist_build();
    scr_DrawRoster();
    goto gotmessage_return;
  }

  // We don't call the message_in hook if 'block_unsubscribed' is true and
  // this is a regular message from an unsubscribed user.
  // System messages (from our server) are allowed.
  if ((!settings_opt_get_int("block_unsubscribed") ||
       (roster_getsubscription(bjid) & sub_from) ||
       (type == LM_MESSAGE_SUB_TYPE_CHAT)) ||
      ((s = settings_opt_get("server")) != NULL && !strcasecmp(bjid, s))) {
    gchar *fullbody = NULL;
    guint encrypted;

    if (decrypted_pgp)
      encrypted = ENCRYPTED_PGP;
    else if (otr_msg)
      encrypted = ENCRYPTED_OTR;
    else
      encrypted = 0;

    if (subject) {
      if (body)
        fullbody = g_strdup_printf("[%s]\n%s", subject, body);
      else
        fullbody = g_strdup_printf("[%s]\n", subject);
      body = fullbody;
    }
    hk_message_in(bjid, rname, timestamp, body, type, encrypted);
    g_free(fullbody);
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "Blocked a message from <%s>", bjid);
  }

gotmessage_return:
  // Clean up and exit
  g_free(bjid);
  g_free(decrypted_pgp);
  if (free_msg)
    g_free(decrypted_otr);
}


static LmHandlerResult handle_messages(LmMessageHandler *handler,
                                       LmConnection *connection,
                                       LmMessage *m, gpointer user_data)
{
  const char *p, *from=lm_message_get_from(m);
  char *r, *s;
  LmMessageNode *x;
  const char *body = NULL;
  const char *enc = NULL;
  const char *subject = NULL;
  time_t timestamp = 0L;
  LmMessageSubType mstype;

  mstype = lm_message_get_sub_type(m);

  body = lm_message_node_get_child_value(m->node, "body");

  x = lm_message_node_find_xmlns(m->node, NS_ENCRYPTED);
  if (x && (p = lm_message_node_get_value(x)) != NULL)
    enc = p;

  p = lm_message_node_get_child_value(m->node, "subject");
  if (p != NULL) {
    if (mstype != LM_MESSAGE_SUB_TYPE_GROUPCHAT) {
      // Chat message
      subject = p;
    } else {                                      // Room topic
      GSList *roombuddy;
      gchar *mbuf;
      const gchar *subj = p;
      // Get the room (s) and the nickname (r)
      s = g_strdup(lm_message_get_from(m));
      r = strchr(s, JID_RESOURCE_SEPARATOR);
      if (r) *r++ = 0;
      else   r = s;
      // Set the new topic
      roombuddy = roster_find(s, jidsearch, 0);
      if (roombuddy)
        buddy_settopic(roombuddy->data, subj);
      // Display inside the room window
      if (r == s) {
        // No specific resource (this is certainly history)
        mbuf = g_strdup_printf("The topic has been set to: %s", subj);
      } else {
        mbuf = g_strdup_printf("%s has set the topic to: %s", r, subj);
      }
      scr_WriteIncomingMessage(s, mbuf, 0,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
      if (settings_opt_get_int("log_muc_conf"))
        hlog_write_message(s, 0, -1, mbuf);
      g_free(s);
      g_free(mbuf);
      // The topic is displayed in the chat status line, so refresh now.
      scr_UpdateChatStatus(TRUE);
    }
  }

  // Timestamp?
  timestamp = lm_message_node_get_timestamp(m->node);

  if (mstype == LM_MESSAGE_SUB_TYPE_ERROR) {
    x = lm_message_node_get_child(m->node, "error");
    display_server_error(x);
#if defined JEP0022 || defined JEP0085
    // If the JEP85/22 support is probed, set it back to unknown so that
    // we probe it again.
    chatstates_reset_probed(from);
#endif
  } else {
    handle_state_events(from, m->node);
  }
  if (from && (body || subject))
    gotmessage(mstype, from, body, enc, subject, timestamp,
               lm_message_node_find_xmlns(m->node, NS_SIGNED));
  //report received message if message receipt was requested
  if (lm_message_node_get_child(m->node, "request")) {
    LmMessage *rcvd = lm_message_new(from, LM_MESSAGE_TYPE_MESSAGE);
    lm_message_node_set_attribute(rcvd->node, "id", lm_message_get_id(m));
    lm_message_node_set_attribute
            (lm_message_node_add_child(rcvd->node, "received", NULL),
             "xmlns", NS_RECEIPTS);
    lm_connection_send(connection, rcvd, NULL);
    lm_message_unref(rcvd);
  }

  if (from) {
    x = lm_message_node_find_xmlns(m->node,
                                   "http://jabber.org/protocol/muc#user");
    if (x && !strcmp(x->name, "x"))
      got_muc_message(from, x);
  }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult cb_caps(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data)
{
  char *ver = user_data;
  LmMessageSubType mstype = lm_message_get_sub_type(m);

  caps_add(ver);
  if (mstype == LM_MESSAGE_SUB_TYPE_ERROR) {
    display_server_error(lm_message_node_get_child(m->node, "error"));
  } else if (mstype == LM_MESSAGE_SUB_TYPE_RESULT) {
    LmMessageNode *info;
    LmMessageNode *query = lm_message_node_get_child(m->node, "query");

    info = lm_message_node_get_child(query, "identity");
    if (info)
      caps_set_identity(ver, lm_message_node_get_attribute(info, "category"),
                        lm_message_node_get_attribute(info, "name"),
                        lm_message_node_get_attribute(info, "type"));
    info = lm_message_node_get_child(query, "feature");
    while (info) {
      if (!g_strcmp0(info->name, "feature"))
        caps_add_feature(ver, lm_message_node_get_attribute(info, "var"));
      info = info->next;
    }
  }
  g_free(ver);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult handle_presence(LmMessageHandler *handler,
                                       LmConnection *connection,
                                       LmMessage *m, gpointer user_data)
{
  char *r;
  const char *from, *rname, *p=NULL, *ustmsg=NULL;
  enum imstatus ust;
  char bpprio;
  time_t timestamp = 0L;
  LmMessageNode *muc_packet, *caps;
  LmMessageSubType mstype;

  // Check for MUC presence packet
  muc_packet = lm_message_node_find_xmlns
          (m->node, "http://jabber.org/protocol/muc#user");

  from = lm_message_get_from(m);

  rname = strchr(from, JID_RESOURCE_SEPARATOR);
  if (rname) rname++;

  if (settings_opt_get_int("ignore_self_presence")) {
    const char *self_fjid = lm_connection_get_jid(connection);
    if (self_fjid && !strcasecmp(self_fjid, from)) {
      return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS; // Ignoring self presence
    }
  }

  r = jidtodisp(from);
  mstype = lm_message_get_sub_type(m);

  if (mstype == LM_MESSAGE_SUB_TYPE_ERROR) {
    LmMessageNode *x;
    scr_LogPrint(LPRINT_LOGNORM, "Error presence packet from <%s>", r);
    x = lm_message_node_find_child(m->node, "error");
    display_server_error(x);
    // Let's check it isn't a nickname conflict.
    // XXX Note: We should handle the <conflict/> string condition.
    if ((p = lm_message_node_get_attribute(x, "code")) != NULL) {
      if (atoi(p) == 409) {
        // 409 = conflict (nickname is in use or registered by another user)
        // If we are not inside this room, we should reset the nickname
        GSList *room_elt = roster_find(r, jidsearch, 0);
        if (room_elt && !buddy_getinsideroom(room_elt->data))
          buddy_setnickname(room_elt->data, NULL);
      }
    }

    g_free(r);
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
  }

  p = lm_message_node_get_child_value(m->node, "priority");
  if (p && *p) bpprio = (gchar)atoi(p);
  else         bpprio = 0;

  ust = available;

  p = lm_message_node_get_child_value(m->node, "show");
  if (p) {
    if (!strcmp(p, "away"))      ust = away;
    else if (!strcmp(p, "dnd"))  ust = dontdisturb;
    else if (!strcmp(p, "xa"))   ust = notavail;
    else if (!strcmp(p, "chat")) ust = freeforchat;
  }

  if (mstype == LM_MESSAGE_SUB_TYPE_UNAVAILABLE)
    ust = offline;

  ustmsg = lm_message_node_get_child_value(m->node, "status");

  // Timestamp?
  timestamp = lm_message_node_get_timestamp(m->node);

  if (muc_packet) {
    // This is a MUC presence message
    handle_muc_presence(from, muc_packet, r, rname,
                        ust, ustmsg, timestamp, bpprio);
  } else {
    // Not a MUC message, so this is a regular buddy...
    // Call hk_statuschange() if status has changed or if the
    // status message is different
    const char *msg;
    msg = roster_getstatusmsg(r, rname);
    if ((ust != roster_getstatus(r, rname)) ||
        (!ustmsg && msg && msg[0]) || (ustmsg && (!msg || strcmp(ustmsg, msg))))
      hk_statuschange(r, rname, bpprio, timestamp, ust, ustmsg);
    // Presence signature processing
    if (!ustmsg)
      ustmsg = ""; // Some clients omit the <status/> element :-(
    check_signature(r, rname, lm_message_node_find_xmlns(m->node, NS_SIGNED),
                    ustmsg);
  }

  // XEP-0115 Entity Capabilities
  caps = lm_message_node_find_xmlns(m->node, NS_CAPS);
  if (caps && ust != offline) {
    const char *ver = lm_message_node_get_attribute(caps, "ver");
    GSList *sl_buddy = NULL;
    if (rname)
      sl_buddy = roster_find(r, jidsearch, ROSTER_TYPE_USER);
    // Only cache the caps if the user is on the roster
    if (sl_buddy && buddy_getonserverflag(sl_buddy->data)) {
      buddy_resource_setcaps(sl_buddy->data, rname, ver);

      if (!caps_has_hash(ver)) {
        char *node;
        LmMessageHandler *handler;
        LmMessage *iq = lm_message_new_with_sub_type(from, LM_MESSAGE_TYPE_IQ,
                                                     LM_MESSAGE_SUB_TYPE_GET);
        node = g_strdup_printf("%s#%s",
                               lm_message_node_get_attribute(caps, "node"),
                               ver);
        lm_message_node_set_attributes
                (lm_message_node_add_child(iq->node, "query", NULL),
                 "xmlns", NS_DISCO_INFO,
                 "node", node,
                 NULL);
        g_free(node);
        handler = lm_message_handler_new(cb_caps, g_strdup(ver), NULL);
        lm_connection_send_with_reply(connection, iq, handler, NULL);
        lm_message_unref(iq);
        lm_message_handler_unref(handler);
      }
    }
  }

  g_free(r);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


static LmHandlerResult handle_iq(LmMessageHandler *handler,
                                 LmConnection *connection,
                                 LmMessage *m, gpointer user_data)
{
  int i;
  guint dbgflg;
  const char *xmlns = NULL;
  LmMessageNode *x;
  LmMessageSubType mstype = lm_message_get_sub_type(m);

  for (x = m->node->children; x; x=x->next) {
    xmlns = lm_message_node_get_attribute(x, "xmlns");
    if (xmlns)
      for (i=0; iq_handlers[i].xmlns; ++i)
        if (!strcmp(iq_handlers[i].xmlns, xmlns))
          return iq_handlers[i].handler(NULL, connection, m, user_data);
    xmlns = NULL;
  }

  if (mstype == LM_MESSAGE_SUB_TYPE_ERROR) {
    display_server_error(lm_message_node_get_child(m->node, "error"));
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  if ((mstype == LM_MESSAGE_SUB_TYPE_SET) ||
      (mstype == LM_MESSAGE_SUB_TYPE_GET))
    send_iq_error(connection, m, XMPP_ERROR_NOT_IMPLEMENTED);

  if (mstype == LM_MESSAGE_SUB_TYPE_RESULT)
    dbgflg = LPRINT_DEBUG;
  else
    dbgflg = LPRINT_NORMAL|LPRINT_DEBUG;

  scr_LogPrint(dbgflg, "Unhandled IQ: %s", lm_message_node_to_string(m->node));
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult handle_s10n(LmMessageHandler *handler,
                                   LmConnection *connection,
                                   LmMessage *m, gpointer user_data)
{
  char *r;
  char *buf;
  int newbuddy;
  const char *from = lm_message_get_from(m);
  LmMessageSubType mstype;

  r = jidtodisp(from);

  newbuddy = !roster_find(r, jidsearch, 0);
  mstype = lm_message_get_sub_type(m);

  if (mstype == LM_MESSAGE_SUB_TYPE_SUBSCRIBE) {
    /* The sender wishes to subscribe to our presence */
    const char *msg;
    int isagent;
    eviqs *evn;

    isagent = (roster_gettype(r) & ROSTER_TYPE_AGENT) != 0;
    msg = lm_message_node_get_child_value(m->node, "status");

    buf = g_strdup_printf("<%s> wants to subscribe to your presence updates",
                          from);
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);

    if (msg) {
      buf = g_strdup_printf("<%s> said: %s", from, msg);
      scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
      replace_nl_with_dots(buf);
      scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
      g_free(buf);
    }

    // Create a new event item
    evn = evs_new(EVS_TYPE_SUBSCRIPTION, EVS_MAX_TIMEOUT);
    if (evn) {
      evn->callback = &evscallback_subscription;
      evn->data = g_strdup(r);
      evn->desc = g_strdup_printf("<%s> wants to subscribe to your "
                                  "presence updates", r);
      buf = g_strdup_printf("Please use /event %s accept|reject", evn->id);
    } else {
      buf = g_strdup_printf("Unable to create a new event!");
    }
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);
  } else if (mstype == LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE) {
    /* The sender is unsubscribing from our presence */
    xmpp_send_s10n(from, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);
    buf = g_strdup_printf("<%s> is unsubscribing from your "
                          "presence updates", from);
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);
  } else if (mstype == LM_MESSAGE_SUB_TYPE_SUBSCRIBED) {
    /* The sender has allowed us to receive their presence */
    buf = g_strdup_printf("<%s> has allowed you to receive their "
                          "presence updates", from);
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);
  } else if (mstype == LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED) {
    /* The subscription request has been denied or a previously-granted
       subscription has been cancelled */
    roster_unsubscribed(from);
    update_roster = TRUE;
    buf = g_strdup_printf("<%s> has cancelled your subscription to "
                          "their presence updates", from);
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);
  } else {
    g_free(r);
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
  }

  if (newbuddy)
    update_roster = TRUE;
  g_free(r);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

//TODO: Use the enum of loudmouth, when it's included in the header...
typedef enum {
  LM_LOG_LEVEL_VERBOSE = 1 << (G_LOG_LEVEL_USER_SHIFT),
  LM_LOG_LEVEL_NET     = 1 << (G_LOG_LEVEL_USER_SHIFT + 1),
  LM_LOG_LEVEL_PARSER  = 1 << (G_LOG_LEVEL_USER_SHIFT + 2),
  LM_LOG_LEVEL_SSL     = 1 << (G_LOG_LEVEL_USER_SHIFT + 3),
  LM_LOG_LEVEL_SASL    = 1 << (G_LOG_LEVEL_USER_SHIFT + 4),
  LM_LOG_LEVEL_ALL     = (LM_LOG_LEVEL_NET |
        LM_LOG_LEVEL_VERBOSE |
        LM_LOG_LEVEL_PARSER |
        LM_LOG_LEVEL_SSL |
        LM_LOG_LEVEL_SASL)
} LmLogLevelFlags;

static void lm_debug_handler (const gchar    *log_domain,
                              GLogLevelFlags  log_level,
                              const gchar    *message,
                              gpointer        user_data)
{
  if (message && *message) {
    char *msg;
    int mcabber_loglevel = settings_opt_get_int("tracelog_level");

    if (mcabber_loglevel < 2)
      return;

    if (message[0] == '\n')
      msg = g_strdup(&message[1]);
    else
      msg = g_strdup(message);

    if (msg[strlen(msg)-1] == '\n')
      msg[strlen(msg)-1] = '\0';

    if (log_level & LM_LOG_LEVEL_VERBOSE) {
      scr_LogPrint(LPRINT_DEBUG, "LM-VERBOSE: %s", msg);
    }
    if (log_level & LM_LOG_LEVEL_NET) {
      if (mcabber_loglevel > 2)
        scr_LogPrint(LPRINT_DEBUG, "LM-NET: %s", msg);
    } else if (log_level & LM_LOG_LEVEL_PARSER) {
      if (mcabber_loglevel > 3)
        scr_LogPrint(LPRINT_DEBUG, "LM-PARSER: %s", msg);
    } else if (log_level & LM_LOG_LEVEL_SASL) {
      scr_LogPrint(LPRINT_DEBUG, "LM-SASL: %s", msg);
    } else if (log_level & LM_LOG_LEVEL_SSL) {
      scr_LogPrint(LPRINT_DEBUG, "LM-SSL: %s", msg);
    }
    g_free(msg);
  }
}


void xmpp_connect(void)
{
  const char *userjid, *password, *resource, *servername, *ssl_fpr;
  char *dynresource = NULL;
  char fpr[16];
  const char *proxy_host;
  const char *resource_prefix = PACKAGE_NAME;
  char *fjid;
  int ssl, tls;
  LmSSL *lssl;
  unsigned int port;
  unsigned int ping;
  LmMessageHandler *handler;
  GError *error = NULL;

  if (lconnection && lm_connection_is_open(lconnection))
    xmpp_disconnect();

  servername = settings_opt_get("server");
  userjid    = settings_opt_get("jid");
  password   = settings_opt_get("password");
  resource   = settings_opt_get("resource");
  proxy_host = settings_opt_get("proxy_host");
  ssl_fpr    = settings_opt_get("ssl_fingerprint");

  if (!userjid) {
    scr_LogPrint(LPRINT_LOGNORM, "Your JID has not been specified!");
    return;
  }
  if (!password) {
    scr_LogPrint(LPRINT_LOGNORM, "Your password has not been specified!");
    return;
  }

  lconnection = lm_connection_new_with_context
          (NULL, g_main_loop_get_context(main_loop));

  g_log_set_handler("LM", LM_LOG_LEVEL_ALL, lm_debug_handler, NULL);

  ping = 40;
  if (settings_opt_get("pinginterval"))
    ping = (unsigned int) settings_opt_get_int("pinginterval");
  lm_connection_set_keep_alive_rate(lconnection, ping);
  scr_LogPrint(LPRINT_DEBUG, "Ping interval established: %d secs", ping);

  lm_connection_set_disconnect_function(lconnection, connection_close_cb,
                                        NULL, NULL);

  handler = lm_message_handler_new(handle_messages, NULL, NULL);
  lm_connection_register_message_handler(lconnection, handler,
                                         LM_MESSAGE_TYPE_MESSAGE,
                                         LM_HANDLER_PRIORITY_NORMAL);
  lm_message_handler_unref(handler);

  handler = lm_message_handler_new(handle_iq, NULL, NULL);
  lm_connection_register_message_handler(lconnection, handler,
                                         LM_MESSAGE_TYPE_IQ,
                                         LM_HANDLER_PRIORITY_NORMAL);
  lm_message_handler_unref(handler);

  handler = lm_message_handler_new(handle_presence, NULL, NULL);
  lm_connection_register_message_handler(lconnection, handler,
                                         LM_MESSAGE_TYPE_PRESENCE,
                                         LM_HANDLER_PRIORITY_LAST);
  lm_message_handler_unref(handler);

  handler = lm_message_handler_new(handle_s10n, NULL, NULL);
  lm_connection_register_message_handler(lconnection, handler,
                                         LM_MESSAGE_TYPE_PRESENCE,
                                         LM_HANDLER_PRIORITY_NORMAL);
  lm_message_handler_unref(handler);

  /* Connect to server */
  scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, "Connecting to server: %s",
               servername ? servername : "...");
  if (!resource)
    resource = resource_prefix;

  if (!settings_opt_get("disable_random_resource")) {
#if HAVE_ARC4RANDOM
    dynresource = g_strdup_printf("%s.%08x", resource, arc4random());
#else
    unsigned int tab[2];
    srand(time(NULL));
    tab[0] = (unsigned int) (0xffff * (rand() / (RAND_MAX + 1.0)));
    tab[1] = (unsigned int) (0xffff * (rand() / (RAND_MAX + 1.0)));
    dynresource = g_strdup_printf("%s.%04x%04x", resource, tab[0], tab[1]);
#endif
    resource = dynresource;
  }

  port = (unsigned int) settings_opt_get_int("port");

  if (port)
    scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " using port %d", port);
  scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " resource %s", resource);

  if (proxy_host) {
    int proxy_port = settings_opt_get_int("proxy_port");
    if (proxy_port <= 0 || proxy_port > 65535) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, "Invalid proxy port: %d",
                   proxy_port);
    } else {
      const char *proxy_user, *proxy_pass;
      LmProxy *lproxy;
      proxy_user = settings_opt_get("proxy_user");
      proxy_pass = settings_opt_get("proxy_pass");
      // Proxy initialization
      lproxy = lm_proxy_new_with_server(LM_PROXY_TYPE_HTTP,
                                        proxy_host, proxy_port);
      lm_proxy_set_username(lproxy, proxy_user);
      lm_proxy_set_password(lproxy, proxy_pass);
      lm_connection_set_proxy(lconnection, lproxy);
      lm_proxy_unref(lproxy);
      scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " using proxy %s:%d",
                   proxy_host, proxy_port);
    }
  }

  fjid = compose_jid(userjid, servername, resource);
  lm_connection_set_jid(lconnection, fjid);
  if (servername)
    lm_connection_set_server(lconnection, servername);
#if defined(HAVE_LIBOTR)
  otr_init(fjid);
#endif
  g_free(fjid);
  g_free(dynresource);

  ssl = settings_opt_get_int("ssl");
  tls = settings_opt_get_int("tls");

  if (!lm_ssl_is_supported()) {
    if (ssl || tls) {
      scr_LogPrint(LPRINT_LOGNORM, "** Error: SSL is NOT available, "
                   "please recompile loudmouth with SSL enabled.");
      return;
    }
  }

  if (ssl && tls) {
    scr_LogPrint(LPRINT_LOGNORM, "You can only set ssl or tls, not both.");
    return;
  }

  if (!port)
    port = (ssl ? LM_CONNECTION_DEFAULT_PORT_SSL : LM_CONNECTION_DEFAULT_PORT);
  lm_connection_set_port(lconnection, port);

  if (ssl_fpr && (!hex_to_fingerprint(ssl_fpr, fpr))) {
    scr_LogPrint(LPRINT_LOGNORM, "** Plese set the fingerprint in the format "
                 "97:5C:00:3F:1D:77:45:25:E2:C5:70:EC:83:C8:87:EE");
    return;
  }

  lssl = lm_ssl_new((ssl_fpr ? fpr : NULL), ssl_cb, NULL, NULL);
  if (lssl) {
    lm_ssl_use_starttls(lssl, !ssl, tls);
    lm_connection_set_ssl(lconnection, lssl);
    lm_ssl_unref(lssl);
  } else if (ssl || tls) {
    scr_LogPrint(LPRINT_LOGNORM, "** Error: Couldn't create SSL struct.");
    return;
  }

  if (!lm_connection_open(lconnection, connection_open_cb,
                          NULL, FALSE, &error)) {
    _try_to_reconnect();
    scr_LogPrint(LPRINT_LOGNORM, "Failed to open: %s\n", error->message);
  }
}

//  insert_entity_capabilities(presence_stanza)
// Entity Capabilities (XEP-0115)
static void insert_entity_capabilities(LmMessageNode *x, enum imstatus status)
{
  LmMessageNode *y;
  const char *ver = entity_version(status);

  y = lm_message_node_add_child(x, "c", NULL);
  lm_message_node_set_attribute(y, "xmlns", NS_CAPS);
  lm_message_node_set_attribute(y, "hash", "sha-1");
  lm_message_node_set_attribute(y, "node", MCABBER_CAPS_NODE);
  lm_message_node_set_attribute(y, "ver", ver);
}

void xmpp_disconnect(void)
{
  if (!lconnection || !lm_connection_is_authenticated(lconnection))
    return;

  // Launch pre-disconnect internal hook
  hook_execute_internal("hook-pre-disconnect");
  // Announce it to  everyone else
  xmpp_setstatus(offline, NULL, "", FALSE);
  lm_connection_close(lconnection, NULL);
}

void xmpp_setstatus(enum imstatus st, const char *recipient, const char *msg,
                  int do_not_sign)
{
  LmMessage *m;

  if (msg) {
    // The status message has been specified.  We'll use it, unless it is
    // "-" which is a special case (option meaning "no status message").
    if (!strcmp(msg, "-"))
      msg = "";
  } else {
    // No status message specified; we'll use:
    // a) the default status message (if provided by the user);
    // b) the current status message;
    // c) no status message (i.e. an empty one).
    msg = settings_get_status_msg(st);
    if (!msg) {
      if (mystatusmsg)
        msg = mystatusmsg;
      else
        msg = "";
    }
  }

  // Only send the packet if we're online.
  // (But we want to update internal status even when disconnected,
  // in order to avoid some problems during network failures)
  if (lm_connection_is_authenticated(lconnection)) {
    const char *s_msg = (st != invisible ? msg : NULL);
    m = lm_message_new_presence(st, recipient, s_msg);
    insert_entity_capabilities(m->node, st); // Entity Capabilities (XEP-0115)
#ifdef HAVE_GPGME
    if (!do_not_sign && gpg_enabled()) {
      char *signature;
      signature = gpg_sign(s_msg ? s_msg : "");
      if (signature) {
        LmMessageNode *y;
        y = lm_message_node_add_child(m->node, "x", signature);
        lm_message_node_set_attribute(y, "xmlns", NS_SIGNED);
        g_free(signature);
      }
    }
#endif
    lm_connection_send(lconnection, m, NULL);
    lm_message_unref(m);
  }

  // If we didn't change our _global_ status, we are done
  if (recipient) return;

  if (lm_connection_is_authenticated(lconnection)) {
    // Send presence to chatrooms
    if (st != invisible) {
      struct T_presence room_presence;
      room_presence.st = st;
      room_presence.msg = msg;
      foreach_buddy(ROSTER_TYPE_ROOM, &roompresence, &room_presence);
    }

    // We'll have to update the roster if we switch to/from offline because
    // we don't know the presences of buddies when offline...
    if (mystatus == offline || st == offline)
      update_roster = TRUE;

    hk_mystatuschange(0, mystatus, st, (st != invisible ? msg : ""));
    mystatus = st;
  }

  if (st)
    mywantedstatus = st;

  if (msg != mystatusmsg) {
    g_free(mystatusmsg);
    if (*msg)
      mystatusmsg = g_strdup(msg);
    else
      mystatusmsg = NULL;
  }

  if (!Autoaway)
    update_last_use();

  // Update status line
  scr_UpdateMainStatus(TRUE);
}


enum imstatus xmpp_getstatus(void)
{
  return mystatus;
}

const char *xmpp_getstatusmsg(void)
{
  return mystatusmsg;
}

//  xmpp_setprevstatus()
// Set previous status.  This wrapper function is used after a disconnection.
void xmpp_setprevstatus(void)
{
  xmpp_setstatus(mywantedstatus, NULL, mystatusmsg, FALSE);
}

//  send_storage(store)
// Send the node "store" to update the server.
// Note: the sender should check we're online.
void send_storage(LmMessageNode *store)
{
  LmMessage *iq;
  LmMessageNode *query;

  if (!rosternotes) return;

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);
  query = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_PRIVATE);
  lm_message_node_insert_childnode(query, store);

  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);
}


//  xmpp_is_bookmarked(roomjid)
// Return TRUE if there's a bookmark for the given jid.
guint xmpp_is_bookmarked(const char *bjid)
{
  LmMessageNode *x;

  if (!bookmarks)
    return FALSE;

  // Walk through the storage bookmark tags
  for (x = bookmarks->children ; x; x = x->next) {
    // If the node is a conference item, check the jid.
    if (x->name && !strcmp(x->name, "conference")) {
      const char *fjid = lm_message_node_get_attribute(x, "jid");
      if (fjid && !strcasecmp(bjid, fjid))
        return TRUE;
    }
  }
  return FALSE;
}

//  xmpp_get_bookmark_nick(roomjid)
// Return the room nickname if it is present in a bookmark.
const char *xmpp_get_bookmark_nick(const char *bjid)
{
  LmMessageNode *x;

  if (!bookmarks || !bjid)
    return NULL;

  // Walk through the storage bookmark tags
  for (x = bookmarks->children ; x; x = x->next) {
    // If the node is a conference item, check the jid.
    if (x->name && !strcmp(x->name, "conference")) {
      const char *fjid = lm_message_node_get_attribute(x, "jid");
      if (fjid && !strcasecmp(bjid, fjid))
        return lm_message_node_get_child_value(x, "nick");
    }
  }
  return NULL;
}


//  xmpp_get_all_storage_bookmarks()
// Return a GSList with all storage bookmarks.
// The caller should g_free the list (not the MUC jids).
GSList *xmpp_get_all_storage_bookmarks(void)
{
  LmMessageNode *x;
  GSList *sl_bookmarks = NULL;

  // If we have no bookmarks, probably the server doesn't support them.
  if (!bookmarks)
    return NULL;

  // Walk through the storage bookmark tags
  for (x = bookmarks->children ; x; x = x->next) {
    // If the node is a conference item, let's add the note to our list.
    if (x->name && !strcmp(x->name, "conference")) {
      struct bookmark *bm_elt;
      const char *autojoin, *name, *nick;
      const char *fjid = lm_message_node_get_attribute(x, "jid");
      if (!fjid)
        continue;
      bm_elt = g_new0(struct bookmark, 1);
      bm_elt->roomjid = g_strdup(fjid);
      autojoin = lm_message_node_get_attribute(x, "autojoin");
      nick = lm_message_node_get_attribute(x, "nick");
      name = lm_message_node_get_attribute(x, "name");
      if (autojoin && !strcmp(autojoin, "1"))
        bm_elt->autojoin = 1;
      if (nick)
        bm_elt->nick = g_strdup(nick);
      if (name)
        bm_elt->name = g_strdup(name);
      sl_bookmarks = g_slist_append(sl_bookmarks, bm_elt);
    }
  }
  return sl_bookmarks;
}

//  xmpp_set_storage_bookmark(roomid, name, nick, passwd, autojoin,
//                          printstatus, autowhois)
// Update the private storage bookmarks: add a conference room.
// If name is nil, we remove the bookmark.
void xmpp_set_storage_bookmark(const char *roomid, const char *name,
                               const char *nick, const char *passwd,
                               int autojoin, enum room_printstatus pstatus,
                               enum room_autowhois awhois)
{
  LmMessageNode *x;
  bool changed = FALSE;

  if (!roomid)
    return;

  // If we have no bookmarks, probably the server doesn't support them.
  if (!bookmarks) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Sorry, your server doesn't seem to support private storage.");
    return;
  }

  // Walk through the storage tags
  for (x = bookmarks->children ; x; x = x->next) {
    // If the current node is a conference item, see if we have to replace it.
    if (x->name && !strcmp(x->name, "conference")) {
      const char *fjid = lm_message_node_get_attribute(x, "jid");
      if (!fjid)
        continue;
      if (!strcmp(fjid, roomid)) {
        // We've found a bookmark for this room.  Let's hide it and we'll
        // create a new one.
        lm_message_node_hide(x);
        changed = TRUE;
        if (!name)
          scr_LogPrint(LPRINT_LOGNORM, "Deleting bookmark...");
      }
    }
  }

  // Let's create a node/bookmark for this roomid, if the name is not NULL.
  if (name) {
    x = lm_message_node_add_child(bookmarks, "conference", NULL);
    lm_message_node_set_attributes(x,
                                   "jid", roomid,
                                   "name", name,
                                   "autojoin", autojoin ? "1" : "0",
                                   NULL);
    if (nick)
      lm_message_node_add_child(x, "nick", nick);
    if (passwd)
      lm_message_node_add_child(x, "password", passwd);
    if (pstatus)
      lm_message_node_add_child(x, "print_status", strprintstatus[pstatus]);
    if (awhois)
      lm_message_node_set_attributes(x, "autowhois",
                                     (awhois == autowhois_on) ? "1" : "0",
                                     NULL);
    changed = TRUE;
    scr_LogPrint(LPRINT_LOGNORM, "Updating bookmarks...");
  }

  if (!changed)
    return;

  if (lm_connection_is_authenticated(lconnection))
    send_storage(bookmarks);
  else
    scr_LogPrint(LPRINT_LOGNORM,
                 "Warning: you're not connected to the server.");
}

static struct annotation *parse_storage_rosternote(LmMessageNode *notenode)
{
  const char *p;
  struct annotation *note = g_new0(struct annotation, 1);
  p = lm_message_node_get_attribute(notenode, "cdate");
  if (p)
    note->cdate = from_iso8601(p, 1);
  p = lm_message_node_get_attribute(notenode, "mdate");
  if (p)
    note->mdate = from_iso8601(p, 1);
  note->text = g_strdup(lm_message_node_get_value(notenode));
  note->jid = g_strdup(lm_message_node_get_attribute(notenode, "jid"));
  return note;
}

//  xmpp_get_all_storage_rosternotes()
// Return a GSList with all storage annotations.
// The caller should g_free the list and its contents.
GSList *xmpp_get_all_storage_rosternotes(void)
{
  LmMessageNode *x;
  GSList *sl_notes = NULL;

  // If we have no rosternotes, probably the server doesn't support them.
  if (!rosternotes)
    return NULL;

  // Walk through the storage rosternotes tags
  for (x = rosternotes->children ; x; x = x->next) {
    struct annotation *note;

    // We want a note item
    if (!x->name || strcmp(x->name, "note"))
      continue;
    // Just in case, check the jid...
    if (!lm_message_node_get_attribute(x, "jid"))
      continue;
    // Ok, let's add the note to our list
    note = parse_storage_rosternote(x);
    sl_notes = g_slist_append(sl_notes, note);
  }
  return sl_notes;
}

//  xmpp_get_storage_rosternotes(barejid, silent)
// Return the annotation associated with this jid.
// If silent is TRUE, no warning is displayed when rosternotes is disabled
// The caller should g_free the string and structure after use.
struct annotation *xmpp_get_storage_rosternotes(const char *barejid, int silent)
{
  LmMessageNode *x;

  if (!barejid)
    return NULL;

  // If we have no rosternotes, probably the server doesn't support them.
  if (!rosternotes) {
    if (!silent)
      scr_LogPrint(LPRINT_NORMAL, "Sorry, "
                   "your server doesn't seem to support private storage.");
    return NULL;
  }

  // Walk through the storage rosternotes tags
  for (x = rosternotes->children ; x; x = x->next) {
    const char *fjid;
    // We want a note item
    if (!x->name || strcmp(x->name, "note"))
      continue;
    // Just in case, check the jid...
    fjid = lm_message_node_get_attribute(x, "jid");
    if (fjid && !strcmp(fjid, barejid)) // We've found a note for this contact.
      return parse_storage_rosternote(x);
  }
  return NULL;  // No note found
}

//  xmpp_set_storage_rosternotes(barejid, note)
// Update the private storage rosternotes: add/delete a note.
// If note is nil, we remove the existing note.
void xmpp_set_storage_rosternotes(const char *barejid, const char *note)
{
  LmMessageNode *x;
  bool changed = FALSE;
  const char *cdate = NULL;

  if (!barejid)
    return;

  // If we have no rosternotes, probably the server doesn't support them.
  if (!rosternotes) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Sorry, your server doesn't seem to support private storage.");
    return;
  }

  // Walk through the storage tags
  for (x = rosternotes->children ; x; x = x->next) {
    // If the current node is a conference item, see if we have to replace it.
    if (x->name && !strcmp(x->name, "note")) {
      const char *fjid = lm_message_node_get_attribute(x, "jid");
      if (!fjid)
        continue;
      if (!strcmp(fjid, barejid)) {
        // We've found a note for this jid.  Let's hide it and we'll
        // create a new one.
        cdate = lm_message_node_get_attribute(x, "cdate");
        lm_message_node_hide(x);
        changed = TRUE;
        break;
      }
    }
  }

  // Let's create a node for this jid, if the note is not NULL.
  if (note) {
    char mdate[20];
    time_t now;
    time(&now);
    to_iso8601(mdate, now);
    if (!cdate)
      cdate = mdate;
    x = lm_message_node_add_child(rosternotes, "note", note);
    lm_message_node_set_attributes(x,
                                   "jid", barejid,
                                   "cdate", cdate,
                                   "mdate", mdate,
                                   NULL);
    changed = TRUE;
  }

  if (!changed)
    return;

  if (lm_connection_is_authenticated(lconnection))
    send_storage(rosternotes);
  else
    scr_LogPrint(LPRINT_LOGNORM,
                 "Warning: you're not connected to the server.");
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
