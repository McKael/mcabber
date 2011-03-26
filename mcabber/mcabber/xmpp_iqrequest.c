/*
 * xmpp_iqrequest.c -- Jabber IQ request handling
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
#include <sys/time.h>

#include "xmpp_helper.h"
#include "xmpp_iq.h"
#include "screen.h"
#include "utils.h"
#include "settings.h"
#include "hooks.h"
#include "hbuf.h"

extern LmMessageNode *bookmarks;
extern LmMessageNode *rosternotes;

static LmHandlerResult cb_roster(LmMessageHandler *h, LmConnection *c,
                                 LmMessage *m, gpointer user_data);
static LmHandlerResult cb_version(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer user_data);
static LmHandlerResult cb_time(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data);
static LmHandlerResult cb_last(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data);
static LmHandlerResult cb_ping(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data);
static LmHandlerResult cb_vcard(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data);

static struct IqRequestHandlers
{
  const gchar *xmlns;
  const gchar *querytag;
  LmHandleMessageFunction handler;
} iq_request_handlers[] = {
  {NS_ROSTER, "query", &cb_roster},
  {NS_VERSION,"query", &cb_version},
  {NS_TIME,   "query", &cb_time},
  {NS_LAST,   "query", &cb_last},
  {NS_PING,   "ping",  &cb_ping},
  {NS_VCARD,  "vCard", &cb_vcard},
  {NULL, NULL, NULL}
};

// Enum for vCard attributes
enum vcard_attr {
  vcard_home    = 1<<0,
  vcard_work    = 1<<1,
  vcard_postal  = 1<<2,
  vcard_voice   = 1<<3,
  vcard_fax     = 1<<4,
  vcard_cell    = 1<<5,
  vcard_inet    = 1<<6,
  vcard_pref    = 1<<7,
};

static LmHandlerResult cb_ping(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data)
{
  struct timeval *timestamp = (struct timeval *)user_data;
  struct timeval now;
  time_t         dsec;
  suseconds_t    dusec;
  const gchar    *fjid;
  gchar          *bjid, *mesg = NULL;

  gettimeofday(&now, NULL);
  dsec = now.tv_sec - timestamp->tv_sec;
  if (now.tv_usec < timestamp->tv_usec) {
    dusec = now.tv_usec + 1000000 - timestamp->tv_usec;
    --dsec;
  } else
    dusec = now.tv_usec - timestamp->tv_usec;

  // Check IQ result sender
  fjid = lm_message_get_from(m);
  if (!fjid)
    fjid = lm_connection_get_jid(lconnection); // No from means our JID...
  if (!fjid) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result (no sender name).");
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  bjid  = jidtodisp(fjid);

  switch (lm_message_get_sub_type(m)) {
    case LM_MESSAGE_SUB_TYPE_RESULT:
        mesg = g_strdup_printf("Pong from <%s>: %d second%s %d ms.", fjid,
                               (int)dsec, dsec > 1 ? "s" : "",
                               (int)(dusec/1000L));
        break;

    case LM_MESSAGE_SUB_TYPE_ERROR:
        display_server_error(lm_message_node_get_child(m->node, "error"),
                             fjid);
        mesg = g_strdup_printf("Ping to <%s> failed.  "
                               "Response time: %d second%s %d ms.",
                               fjid, (int)dsec, dsec > 1 ? "s" : "",
                               (int)(dusec/1000L));
        break;

    default:
        g_free(bjid);
        return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
        break;
  }

  if (mesg)
    scr_WriteIncomingMessage(bjid, mesg, 0, HBB_PREFIX_INFO, 0);
  g_free(mesg);
  g_free(bjid);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

// Warning!! xmlns has to be a namespace from iq_request_handlers[].xmlns
void xmpp_iq_request(const char *fulljid, const char *xmlns)
{
  LmMessage *iq;
  LmMessageNode *query;
  LmMessageHandler *handler;
  gpointer data = NULL;
  GDestroyNotify notifier = NULL;
  GError *error = NULL;
  int i;

  iq = lm_message_new_with_sub_type(fulljid, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_GET);
  for (i = 0; strcmp(iq_request_handlers[i].xmlns, xmlns) != 0 ; ++i)
       ;
  query = lm_message_node_add_child(iq->node,
                                    iq_request_handlers[i].querytag,
                                    NULL);
  lm_message_node_set_attribute(query, "xmlns", xmlns);

  if (!g_strcmp0(xmlns, NS_PING)) {     // Create handler for ping queries
    struct timeval *now = g_new(struct timeval, 1);
    gettimeofday(now, NULL);
    data = (gpointer)now;
    notifier = g_free;
  }

  handler = lm_message_handler_new(iq_request_handlers[i].handler,
                                   data, notifier);

  lm_connection_send_with_reply(lconnection, iq, handler, &error);
  lm_message_handler_unref(handler);
  lm_message_unref(iq);

  if (error) {
    scr_LogPrint(LPRINT_LOGNORM, "Error sending IQ request: %s.", error->message);
    g_error_free(error);
  }
}

//  This callback is reached when mcabber receives the first roster update
// after the connection.
static LmHandlerResult cb_roster(LmMessageHandler *h, LmConnection *c,
                                 LmMessage *m, gpointer user_data)
{
  LmMessageNode *x;
  const char *ns;

  // Only execute the hook if the roster has been successfully retrieved
  if (lm_message_get_sub_type(m) != LM_MESSAGE_SUB_TYPE_RESULT)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  x = lm_message_node_find_child(m->node, "query");
  if (!x)
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  ns = lm_message_node_get_attribute(x, "xmlns");
  if (ns && !strcmp(ns, NS_ROSTER))
    handle_iq_roster(NULL, c, m, user_data);

  // Post-login stuff
  hk_postconnect();

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult cb_version(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer user_data)
{
  LmMessageNode *ansqry;
  const char *p, *bjid;
  char *buf, *tmp;

  // Check IQ result sender
  bjid = lm_message_get_from(m);
  if (!bjid)
    bjid = lm_connection_get_jid(lconnection); // No from means our JID...
  if (!bjid) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result (no sender name).");
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check for error message
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    scr_LogPrint(LPRINT_LOGNORM, "Received error IQ message (%s)", bjid);
    display_server_error(lm_message_node_get_child(m->node, "error"), NULL);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check message contents
  ansqry = lm_message_node_get_child(m->node, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result from <%s>!", bjid);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  buf = g_strdup_printf("Received IQ:version result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  tmp = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (tmp) *tmp = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  p = lm_message_node_get_child_value(ansqry, "name");
  if (p && *p) {
    buf = g_strdup_printf("Name:    %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = lm_message_node_get_child_value(ansqry, "version");
  if (p && *p) {
    buf = g_strdup_printf("Version: %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = lm_message_node_get_child_value(ansqry, "os");
  if (p && *p) {
    buf = g_strdup_printf("OS:      %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult cb_time(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data)
{
  LmMessageNode *ansqry;
  const char *p, *bjid;
  char *buf, *tmp;

  // Check IQ result sender
  bjid = lm_message_get_from(m);
  if (!bjid)
    bjid = lm_connection_get_jid(lconnection); // No from means our JID...
  if (!bjid) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:time result (no sender name).");
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check for error message
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    scr_LogPrint(LPRINT_LOGNORM, "Received error IQ message (%s)", bjid);
    display_server_error(lm_message_node_get_child(m->node, "error"), NULL);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check message contents
  ansqry = lm_message_node_get_child(m->node, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:time result from <%s>!", bjid);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  buf = g_strdup_printf("Received IQ:time result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  tmp = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (tmp) *tmp = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  p = lm_message_node_get_child_value(ansqry, "utc");
  if (p && *p) {
    buf = g_strdup_printf("UTC:  %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = lm_message_node_get_child_value(ansqry, "tz");
  if (p && *p) {
    buf = g_strdup_printf("TZ:   %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = lm_message_node_get_child_value(ansqry, "display");
  if (p && *p) {
    buf = g_strdup_printf("Time: %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult cb_last(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data)
{
  LmMessageNode *ansqry;
  const char *p, *bjid;
  char *buf, *tmp;

  // Check IQ result sender
  bjid = lm_message_get_from(m);
  if (!bjid)
    bjid = lm_connection_get_jid(lconnection); // No from means our JID...
  if (!bjid) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:last result (no sender name).");
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check for error message
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    scr_LogPrint(LPRINT_LOGNORM, "Received error IQ message (%s)", bjid);
    display_server_error(lm_message_node_get_child(m->node, "error"), NULL);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check message contents
  ansqry = lm_message_node_get_child(m->node, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result from <%s>!", bjid);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  buf = g_strdup_printf("Received IQ:last result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  tmp = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (tmp) *tmp = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  p = lm_message_node_get_attribute(ansqry, "seconds");
  if (p) {
    long int s;
    GString *sbuf;
    sbuf = g_string_new("Idle time: ");
    s = atol(p);
    // Days
    if (s > 86400L) {
      g_string_append_printf(sbuf, "%ldd ", s/86400L);
      s %= 86400L;
    }
    // hh:mm:ss
    g_string_append_printf(sbuf, "%02ld:", s/3600L);
    s %= 3600L;
    g_string_append_printf(sbuf, "%02ld:%02ld", s/60L, s%60L);
    scr_WriteIncomingMessage(bjid, sbuf->str,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_string_free(sbuf, TRUE);
  } else {
    scr_WriteIncomingMessage(bjid, "No idle time reported.",
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
  }
  p = lm_message_node_get_value(ansqry);
  if (p) {
    buf = g_strdup_printf("Status message: %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
    g_free(buf);
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void display_vcard_item(const char *bjid, const char *label,
                               enum vcard_attr vcard_attrib, const char *text)
{
  char *buf;

  if (!text || !*text || !bjid || !label)
    return;

  buf = g_strdup_printf("%s: %s%s%s%s%s%s%s%s%s%s", label,
                        (vcard_attrib & vcard_home ? "[home]" : ""),
                        (vcard_attrib & vcard_work ? "[work]" : ""),
                        (vcard_attrib & vcard_postal ? "[postal]" : ""),
                        (vcard_attrib & vcard_voice ? "[voice]" : ""),
                        (vcard_attrib & vcard_fax  ? "[fax]"  : ""),
                        (vcard_attrib & vcard_cell ? "[cell]" : ""),
                        (vcard_attrib & vcard_inet ? "[inet]" : ""),
                        (vcard_attrib & vcard_pref ? "[pref]" : ""),
                        (vcard_attrib ? " " : ""),
                        text);
  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
  g_free(buf);
}

static void handle_vcard_node(const char *barejid, LmMessageNode *vcardnode)
{
  LmMessageNode *x;
  const char *p;

  for (x = vcardnode->children ; x; x = x->next) {
    const char *data;
    enum vcard_attr vcard_attrib = 0;

    p = x->name;
    if (!p)
      continue;

    data = lm_message_node_get_value(x);

    if (!strcmp(p, "FN"))
      display_vcard_item(barejid, "Name", vcard_attrib, data);
    else if (!strcmp(p, "NICKNAME"))
      display_vcard_item(barejid, "Nickname", vcard_attrib, data);
    else if (!strcmp(p, "URL"))
      display_vcard_item(barejid, "URL", vcard_attrib, data);
    else if (!strcmp(p, "BDAY"))
      display_vcard_item(barejid, "Birthday", vcard_attrib, data);
    else if (!strcmp(p, "TZ"))
      display_vcard_item(barejid, "Timezone", vcard_attrib, data);
    else if (!strcmp(p, "TITLE"))
      display_vcard_item(barejid, "Title", vcard_attrib, data);
    else if (!strcmp(p, "ROLE"))
      display_vcard_item(barejid, "Role", vcard_attrib, data);
    else if (!strcmp(p, "DESC"))
      display_vcard_item(barejid, "Comment", vcard_attrib, data);
    else if (!strcmp(p, "N")) {
      data = lm_message_node_get_child_value(x, "FAMILY");
      display_vcard_item(barejid, "Family Name", vcard_attrib, data);
      data = lm_message_node_get_child_value(x, "GIVEN");
      display_vcard_item(barejid, "Given Name", vcard_attrib, data);
      data = lm_message_node_get_child_value(x, "MIDDLE");
      display_vcard_item(barejid, "Middle Name", vcard_attrib, data);
    } else if (!strcmp(p, "ORG")) {
      data = lm_message_node_get_child_value(x, "ORGNAME");
      display_vcard_item(barejid, "Organisation name", vcard_attrib, data);
      data = lm_message_node_get_child_value(x, "ORGUNIT");
      display_vcard_item(barejid, "Organisation unit", vcard_attrib, data);
    } else {
      // The HOME, WORK and PREF attributes are common to the remaining fields
      // (ADR, TEL & EMAIL)
      if (lm_message_node_get_child(x, "HOME"))
        vcard_attrib |= vcard_home;
      if (lm_message_node_get_child(x, "WORK"))
        vcard_attrib |= vcard_work;
      if (lm_message_node_get_child(x, "PREF"))
        vcard_attrib |= vcard_pref;
      if (!strcmp(p, "ADR")) {          // Address
        if (lm_message_node_get_child(x, "POSTAL"))
          vcard_attrib |= vcard_postal;
        data = lm_message_node_get_child_value(x, "EXTADD");
        display_vcard_item(barejid, "Addr (ext)", vcard_attrib, data);
        data = lm_message_node_get_child_value(x, "STREET");
        display_vcard_item(barejid, "Street", vcard_attrib, data);
        data = lm_message_node_get_child_value(x, "LOCALITY");
        display_vcard_item(barejid, "Locality", vcard_attrib, data);
        data = lm_message_node_get_child_value(x, "REGION");
        display_vcard_item(barejid, "Region", vcard_attrib, data);
        data = lm_message_node_get_child_value(x, "PCODE");
        display_vcard_item(barejid, "Postal code", vcard_attrib, data);
        data = lm_message_node_get_child_value(x, "CTRY");
        display_vcard_item(barejid, "Country", vcard_attrib, data);
      } else if (!strcmp(p, "TEL")) {   // Telephone
        data = lm_message_node_get_child_value(x, "NUMBER");
        if (data) {
          if (lm_message_node_get_child(x, "VOICE"))
            vcard_attrib |= vcard_voice;
          if (lm_message_node_get_child(x, "FAX"))
            vcard_attrib |= vcard_fax;
          if (lm_message_node_get_child(x, "CELL"))
            vcard_attrib |= vcard_cell;
          display_vcard_item(barejid, "Phone", vcard_attrib, data);
        }
      } else if (!strcmp(p, "EMAIL")) { // Email
        if (lm_message_node_get_child(x, "INTERNET"))
          vcard_attrib |= vcard_inet;
        data = lm_message_node_get_child_value(x, "USERID");
        display_vcard_item(barejid, "Email", vcard_attrib, data);
      }
    }
  }
}

static LmHandlerResult cb_vcard(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer user_data)
{
  LmMessageNode *ansqry;
  const char *bjid;
  char *buf, *tmp;

  // Check IQ result sender
  bjid = lm_message_get_from(m);
  if (!bjid)
    bjid = lm_connection_get_jid(lconnection); // No from means our JID...
  if (!bjid) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:vCard result (no sender name).");
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // Check for error message
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    scr_LogPrint(LPRINT_LOGNORM, "Received error IQ message (%s)", bjid);
    display_server_error(lm_message_node_get_child(m->node, "error"), NULL);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  buf = g_strdup_printf("Received IQ:vCard result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // Get the vCard node
  ansqry = lm_message_node_get_child(m->node, "vCard");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Empty IQ:vCard result!");
    g_free(buf);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  // bjid should really be the "bare JID", let's strip the resource
  tmp = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (tmp) *tmp = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  handle_vcard_node(bjid, ansqry);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void storage_bookmarks_parse_conference(LmMessageNode *node)
{
  const char *fjid, *name, *autojoin;
  const char *pstatus, *awhois, *fjoins, *group;
  char *bjid;
  GSList *room_elt;

  fjid = lm_message_node_get_attribute(node, "jid");
  if (!fjid)
    return;
  name = lm_message_node_get_attribute(node, "name");
  autojoin = lm_message_node_get_attribute(node, "autojoin");
  awhois = lm_message_node_get_attribute(node, "autowhois");
  pstatus = lm_message_node_get_child_value(node, "print_status");
  fjoins = lm_message_node_get_child_value(node, "flag_joins");
  group = lm_message_node_get_child_value(node, "group");

  bjid = jidtodisp(fjid); // Bare jid

  // Make sure this is a room (it can be a conversion user->room)
  room_elt = roster_find(bjid, jidsearch, 0);
  if (!room_elt) {
    room_elt = roster_add_user(bjid, name, group, ROSTER_TYPE_ROOM,
                               sub_none, -1);
  } else {
    buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);
    /*
    // If the name is available, should we use it?
    // I don't think so, it would be confusing because this item is already
    // in the roster.
    if (name)
      buddy_setname(room_elt->data, name);

    // The same question for roster group.
    if (group)
      buddy_setgroup(room_elt->data, group);
    */
  }

  // Set the print_status and auto_whois values
  if (pstatus) {
    enum room_printstatus i;
    for (i = status_none; i <= status_all; i++)
      if (!strcasecmp(pstatus, strprintstatus[i]))
        break;
    if (i <= status_all)
      buddy_setprintstatus(room_elt->data, i);
  }
  if (awhois) {
    enum room_autowhois i = autowhois_default;
    if (!strcmp(awhois, "1") || !(strcmp(awhois, "true")))
      i = autowhois_on;
    else if (!strcmp(awhois, "0") || !(strcmp(awhois, "false")))
      i = autowhois_off;
    if (i != autowhois_default)
      buddy_setautowhois(room_elt->data, i);
  }
  if (fjoins) {
    enum room_flagjoins i;
    for (i = flagjoins_none; i <= flagjoins_all; i++)
      if (!strcasecmp(fjoins, strflagjoins[i]))
        break;
    if (i <= flagjoins_all)
      buddy_setflagjoins(room_elt->data, i);
  }

  // Is autojoin set?
  // If it is, we'll look up for more information (nick? password?) and
  // try to join the room.
  if (autojoin && !strcmp(autojoin, "1")) {
    const char *nick, *passwd;
    char *tmpnick = NULL;
    nick = lm_message_node_get_child_value(node, "nick");
    passwd = lm_message_node_get_child_value(node, "password");
    if (!nick || !*nick)
      nick = tmpnick = default_muc_nickname(NULL);
    // Let's join now
    scr_LogPrint(LPRINT_LOGNORM, "Auto-join bookmark <%s>", bjid);
    xmpp_room_join(bjid, nick, passwd);
    g_free(tmpnick);
  }
  g_free(bjid);
}

static LmHandlerResult cb_storage_bookmarks(LmMessageHandler *h,
                                            LmConnection *c,
                                            LmMessage *m, gpointer user_data)
{
  LmMessageNode *x, *ansqry;
  char *p;

  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    // No server support, or no bookmarks?
    p = m->node->children->name;
    if (p && !strcmp(p, "item-not-found")) {
      // item-no-found means the server has Private Storage, but it's
      // currently empty.
      if (bookmarks)
        lm_message_node_unref(bookmarks);
      bookmarks = lm_message_node_new("storage", "storage:bookmarks");
      // We return 0 so that the IQ error message be
      // not displayed, as it isn't a real error.
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS; // Unhandled error
  }

  ansqry = lm_message_node_get_child(m->node, "query");
  ansqry = lm_message_node_get_child(ansqry, "storage");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOG, "Invalid IQ:private result! (storage:bookmarks)");
    return 0;
  }

  // Walk through the storage tags
  for (x = ansqry->children ; x; x = x->next) {
    // If the current node is a conference item, parse it and update the roster
    if (x->name && !strcmp(x->name, "conference"))
      storage_bookmarks_parse_conference(x);
  }
  // "Copy" the bookmarks node
  if (bookmarks)
    lm_message_node_unref(bookmarks);
  lm_message_node_deep_ref(ansqry);
  bookmarks = ansqry;
  return 0;
}


static LmHandlerResult cb_storage_rosternotes(LmMessageHandler *h,
                                              LmConnection *c,
                                              LmMessage *m, gpointer user_data)
{
  LmMessageNode *ansqry;

  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    const char *p;
    // No server support, or no roster notes?
    p = m->node->children->name;
    if (p && !strcmp(p, "item-not-found")) {
      // item-no-found means the server has Private Storage, but it's
      // currently empty.
      if (rosternotes)
        lm_message_node_unref(rosternotes);
      rosternotes = lm_message_node_new("storage", "storage:rosternotes");
      // We return 0 so that the IQ error message be
      // not displayed, as it isn't a real error.
      return LM_HANDLER_RESULT_REMOVE_MESSAGE;
    }
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS; // Unhandled error
  }

  ansqry = lm_message_node_get_child(m->node, "query");
  ansqry = lm_message_node_get_child(ansqry, "storage");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOG, "Invalid IQ:private result! "
                 "(storage:rosternotes)");
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }
  // Copy the rosternotes node
  if (rosternotes)
    lm_message_node_unref(rosternotes);
  lm_message_node_deep_ref(ansqry);
  rosternotes = ansqry;
  return 0;
}


static struct IqRequestStorageHandlers
{
  const gchar *storagens;
  LmHandleMessageFunction handler;
} iq_request_storage_handlers[] = {
  {"storage:rosternotes", &cb_storage_rosternotes},
  {"storage:bookmarks", &cb_storage_bookmarks},
  {NULL, NULL}
};

void xmpp_request_storage(const gchar *storage)
{
  LmMessage *iq;
  LmMessageNode *query;
  LmMessageHandler *handler;
  int i;

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_GET);
  query = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_PRIVATE);
  lm_message_node_set_attribute(lm_message_node_add_child
                                (query, "storage", NULL),
                                "xmlns", storage);

  for (i = 0;
       strcmp(iq_request_storage_handlers[i].storagens, storage) != 0;
       ++i) ;

  handler = lm_message_handler_new(iq_request_storage_handlers[i].handler,
                                   NULL, FALSE);
  lm_connection_send_with_reply(lconnection, iq, handler, NULL);
  lm_message_handler_unref(handler);
  lm_message_unref(iq);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
