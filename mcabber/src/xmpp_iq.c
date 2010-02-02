/*
 * xmpp_iq.c    -- Jabber protocol IQ-related stuff
 *
 * Copyright (C) 2008-2009 Frank Zschockelt <mcabber@freakysoft.de>
 * Copyright (C) 2005-2009 Mikael Berthe <mikael@lilotux.net>
 * Parts come from the centericq project:
 * Copyright (C) 2002-2005 by Konstantin Klyagin <konst@konst.org.ua>
 * Some small parts come from the Pidgin project <http://pidgin.im/>
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
#include <sys/utsname.h>

#include "xmpp_helper.h"
#include "commands.h"
#include "screen.h"
#include "utils.h"
#include "logprint.h"
#include "settings.h"
#include "caps.h"
#include "main.h"

extern struct xmpp_error xmpp_errors[];

static LmHandlerResult handle_iq_command_set_status(LmMessageHandler *h,
                                                    LmConnection *c,
                                                    LmMessage *m,
                                                    gpointer ud);

static LmHandlerResult handle_iq_command_leave_groupchats(LmMessageHandler *h,
                                                          LmConnection *c,
                                                          LmMessage *m,
                                                          gpointer ud);

inline double seconds_since_last_use(void);

struct adhoc_command {
  char *name;
  char *description;
  bool only_for_self;
  LmHandleMessageFunction callback;
};

const struct adhoc_command adhoc_command_list[] = {
  { "http://jabber.org/protocol/rc#set-status",
    "Change client status",
    1,
    &handle_iq_command_set_status },
  { "http://jabber.org/protocol/rc#leave-groupchats",
    "Leave groupchat(s)",
    1,
    &handle_iq_command_leave_groupchats },
  { NULL, NULL, 0, NULL },
};

struct adhoc_status {
  char *name;   // the name used by adhoc
  char *description;
  char *status; // the string, used by setstus
};
// It has to match imstatus of roster.h!
const struct adhoc_status adhoc_status_list[] = {
  {"offline", "Offline", "offline"},
  {"online", "Online", "avail"},
  {"chat", "Chat", "free"},
  {"dnd", "Do not disturb", "dnd"},
  {"xd", "Extended away", "notavail"},
  {"away", "Away", "away"},
  {"invisible", "Invisible", "invisible"},
  {NULL, NULL, NULL},
};

static char *generate_session_id(char *prefix)
{
  char *result;
  static int counter = 0;
  counter++;
  // TODO better use timestamp?
  result = g_strdup_printf("%s-%i", prefix, counter);
  return result;
}

static LmMessage *lm_message_new_iq_error(LmMessage *m, guint error)
{
  LmMessage *r;
  LmMessageNode *err;
  int i;

  for (i = 0; xmpp_errors[i].code; ++i)
    if (xmpp_errors[i].code == error)
      break;
  g_return_val_if_fail(xmpp_errors[i].code > 0, NULL);

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_ERROR);
  err = lm_message_node_add_child(r->node, "error", NULL);
  lm_message_node_set_attribute(err, "code", xmpp_errors[i].code_str);
  lm_message_node_set_attribute(err, "type", xmpp_errors[i].type);
  lm_message_node_set_attribute
          (lm_message_node_add_child(err,
                                     xmpp_errors[i].condition, NULL),
           "xmlns", NS_XMPP_STANZAS);

  return r;
}

void send_iq_error(LmConnection *c, LmMessage *m, guint error)
{
  LmMessage *r;
  r = lm_message_new_iq_error(m, error);
  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
}

static void lm_message_node_add_dataform_result(LmMessageNode *node,
                                                const char *message)
{
  LmMessageNode *x, *field;

  x = lm_message_node_add_child(node, "x", NULL);
  lm_message_node_set_attributes(x,
                                 "type", "result",
                                 "xmlns", "jabber:x:data",
                                 NULL);
  field = lm_message_node_add_child(x, "field", NULL);
  lm_message_node_set_attributes(field,
                                 "type", "text-single",
                                 "var", "message",
                                 NULL);
  lm_message_node_add_child(field, "value", message);
}

static LmHandlerResult handle_iq_commands_list(LmMessageHandler *h,
                                               LmConnection *c,
                                               LmMessage *m, gpointer ud)
{
  LmMessage *iq;
  LmMessageNode *query;
  const char *requester_jid;
  const struct adhoc_command *command;
  const char *node;
  gboolean from_self;

  iq = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  query = lm_message_node_add_child(iq->node, "query", NULL);
  node = lm_message_node_get_attribute
          (lm_message_node_get_child(m->node, "query"),
           "node");
  if (node)
    lm_message_node_set_attribute(query, "node", node);

  requester_jid = lm_message_get_from(m);
  from_self = jid_equal(lm_connection_get_jid(c), requester_jid);

  for (command = adhoc_command_list ; command->name ; command++) {
    if (!command->only_for_self || from_self) {
      lm_message_node_set_attributes
              (lm_message_node_add_child(query, "item", NULL),
               "node", command->name,
               "name", command->description,
               "jid", lm_connection_get_jid(c),
               NULL);
    }
  }

  lm_connection_send(c, iq, NULL);
  lm_message_unref(iq);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static LmHandlerResult handle_iq_command_set_status(LmMessageHandler *h,
                                                    LmConnection *c,
                                                    LmMessage *m, gpointer ud)
{
  const char *action, *node;
  char *sessionid;
  LmMessage *iq;
  LmMessageNode *command, *x, *y;
  const struct adhoc_status *s;

  x = lm_message_node_get_child(m->node, "command");
  action = lm_message_node_get_attribute(x, "action");
  node = lm_message_node_get_attribute(x, "node");
  sessionid = (char *)lm_message_node_get_attribute(x, "sessionid");

  iq = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  command = lm_message_node_add_child(iq->node, "command", NULL);
  lm_message_node_set_attribute(command, "node", node);
  lm_message_node_set_attribute(command, "xmlns", NS_COMMANDS);

  if (!sessionid) {
    sessionid = generate_session_id("set-status");
    lm_message_node_set_attribute(command, "sessionid", sessionid);
    g_free(sessionid);
    sessionid = NULL;
    lm_message_node_set_attribute(command, "status", "executing");

    x = lm_message_node_add_child(command, "x", NULL);
    lm_message_node_set_attribute(x, "type", "form");
    lm_message_node_set_attribute(x, "xmlns", "jabber:x:data");

    lm_message_node_add_child(x, "title", "Change Status");

    lm_message_node_add_child(x, "instructions",
                              "Choose the status and status message");

    // TODO see if factorisation is possible
    y = lm_message_node_add_child(x, "field", NULL);
    lm_message_node_set_attribute(y, "type", "hidden");
    lm_message_node_set_attribute(y, "var", "FORM_TYPE");

    lm_message_node_add_child(y, "value", "http://jabber.org/protocol/rc");

    y = lm_message_node_add_child(x, "field", NULL);
    lm_message_node_set_attributes(y,
                                   "type", "list-single",
                                   "var", "status",
                                   "label", "Status",
                                   NULL);
    lm_message_node_add_child(y, "required", NULL);

    // XXX: ugly
    lm_message_node_add_child(y, "value",
                              adhoc_status_list[xmpp_getstatus()].name);
    for (s = adhoc_status_list; s->name; s++) {
        LmMessageNode *option = lm_message_node_add_child(y, "option", NULL);
        lm_message_node_add_child(option, "value", s->name);
        lm_message_node_set_attribute(option, "label", s->description);
    }
    // TODO add priority ?
    // I do not think this is useful, user should not have to care of the
    // priority like gossip and gajim do (misc)
    lm_message_node_set_attributes
            (lm_message_node_add_child(x, "field", NULL),
             "type", "text-multi",
             "var", "status-message",
             "label", "Message",
             NULL);
  } else if (action && !strcmp(action, "cancel")) {
    lm_message_node_set_attribute(command, "status", "canceled");
  } else  { // (if sessionid and not canceled)
    y = lm_message_node_find_xmlns(x, "jabber:x:data"); //x?xmlns=jabber:x:data
    if (y) {
      const char *value=NULL, *message=NULL;
      LmMessageNode *fields, *field;
      field = fields = lm_message_node_get_child(y, "field"); //field?var=status
      while (field && strcmp("status",
                             lm_message_node_get_attribute(field, "var")))
        field = field->next;
      field = lm_message_node_get_child(field, "value");
      if (field)
        value = lm_message_node_get_value(field);
      field = fields; //field?var=status-message
      while (field && strcmp("status-message",
                             lm_message_node_get_attribute(field, "var")))
        field = field->next;
      field = lm_message_node_get_child(field, "value");
      if (field)
        message = lm_message_node_get_value(field);
      if (value) {
        for (s = adhoc_status_list; !s->name || strcmp(s->name, value); s++);
        if (s->name) {
          char *status = g_strdup_printf("%s %s", s->status,
                                         message ? message : "");
          cmd_setstatus(NULL, status);
          g_free(status);
          lm_message_node_set_attribute(command, "status", "completed");
          lm_message_node_add_dataform_result(command,
                                              "Status has been changed");
        }
      }
    }
  }
  if (sessionid)
    lm_message_node_set_attribute(command, "sessionid", sessionid);
  lm_connection_send(c, iq, NULL);
  lm_message_unref(iq);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void _callback_foreach_buddy_groupchat(gpointer rosterdata, void *param)
{
  LmMessageNode *field, *option;
  const char *room_jid, *nickname;
  char *desc;

  room_jid = buddy_getjid(rosterdata);
  if (!room_jid) return;
  nickname = buddy_getnickname(rosterdata);
  if (!nickname) return;
  field = param;

  option = lm_message_node_add_child(field, "option", NULL);
  lm_message_node_add_child(option, "value", room_jid);
  desc = g_strdup_printf("%s on %s", nickname, room_jid);
  lm_message_node_set_attribute(option, "label", desc);
  g_free(desc);
}

static LmHandlerResult handle_iq_command_leave_groupchats(LmMessageHandler *h,
                                                          LmConnection *c,
                                                          LmMessage *m,
                                                          gpointer ud)
{
  const char *action, *node;
  char *sessionid;
  LmMessage *iq;
  LmMessageNode *command, *x;

  x = lm_message_node_get_child(m->node, "command");
  action = lm_message_node_get_attribute(x, "action");
  node = lm_message_node_get_attribute(x, "node");
  sessionid = (char*)lm_message_node_get_attribute(x, "sessionid");

  iq = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  command = lm_message_node_add_child(iq->node, "command", NULL);
  lm_message_node_set_attributes(command,
                                 "node", node,
                                 "xmlns", NS_COMMANDS,
                                 NULL);

  if (!sessionid) {
    LmMessageNode *field;

    sessionid = generate_session_id("leave-groupchats");
    lm_message_node_set_attribute(command, "sessionid", sessionid);
    g_free(sessionid);
    sessionid = NULL;
    lm_message_node_set_attribute(command, "status", "executing");

    x = lm_message_node_add_child(command, "x", NULL);
    lm_message_node_set_attributes(x,
                                   "type", "form",
                                   "xmlns", "jabber:x:data",
                                   NULL);

    lm_message_node_add_child(x, "title", "Leave groupchat(s)");

    lm_message_node_add_child(x, "instructions",
                              "What groupchats do you want to leave?");

    field = lm_message_node_add_child(x, "field", NULL);
    lm_message_node_set_attributes(field,
                                   "type", "hidden",
                                   "var", "FORM_TYPE",
                                   NULL);

    lm_message_node_add_child(field, "value",
                              "http://jabber.org/protocol/rc");

    field = lm_message_node_add_child(x, "field", NULL);
    lm_message_node_set_attributes(field,
                                   "type", "list-multi",
                                   "var", "groupchats",
                                   "label", "Groupchats: ",
                                   NULL);
    lm_message_node_add_child(field, "required", NULL);

    foreach_buddy(ROSTER_TYPE_ROOM, &_callback_foreach_buddy_groupchat, field);
    //TODO: return an error if we are not connected to groupchats
  } else if (action && !strcmp(action, "cancel")) {
    lm_message_node_set_attribute(command, "status", "canceled");
  } else  { // (if sessionid and not canceled)
    LmMessageNode *form = lm_message_node_find_xmlns(x, "jabber:x:data");//TODO
    if (form) {
      LmMessageNode *field;

      lm_message_node_set_attribute(command, "status", "completed");
      //TODO: implement sth. like "field?var=groupchats" in xmlnode...
      field  = lm_message_node_get_child(form, "field");
      while (field && strcmp("groupchats",
                             lm_message_node_get_attribute(field, "var")))
        field = field->next;

      if (field)
        for (x = field->children ; x ; x = x->next)
        {
          if (!strcmp (x->name, "value")) {
            GList* b = buddy_search_jid(lm_message_node_get_value(x));
            if (b)
              cmd_room_leave(b->data, "Requested by remote command");
          }
        }
      lm_message_node_add_dataform_result(command,
                                          "Groupchats have been left");
    }
  }
  if (sessionid)
    lm_message_node_set_attribute(command, "sessionid", sessionid);
  lm_connection_send(c, iq, NULL);
  lm_message_unref(iq);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

LmHandlerResult handle_iq_commands(LmMessageHandler *h,
                                   LmConnection *c,
                                   LmMessage *m, gpointer ud)
{
  const char *requester_jid = NULL;
  LmMessageNode *cmd;
  const struct adhoc_command *command;

  // mcabber has only partial XEP-0146 support...
  if (LM_MESSAGE_SUB_TYPE_SET != lm_message_get_sub_type(m))
    return LM_HANDLER_RESULT_ALLOW_MORE_HANDLERS;

  requester_jid = lm_message_get_from(m);

  cmd = lm_message_node_get_child(m->node, "command");
  if (jid_equal(lm_connection_get_jid(c), requester_jid)) {
    const char *action, *node;
    action = lm_message_node_get_attribute(cmd, "action");
    node = lm_message_node_get_attribute(cmd, "node");
    // action can be NULL, in which case it seems to take the default,
    // ie execute
    if (!action || !strcmp(action, "execute") || !strcmp(action, "cancel")
        || !strcmp(action, "next") || !strcmp(action, "complete")) {
      for (command = adhoc_command_list; command->name; command++) {
        if (!strcmp(node, command->name))
          command->callback(h, c, m, ud);
      }
      // "prev" action will get there, as we do not implement it,
      // and do not authorize it
    } else {
      LmMessage *r;
      LmMessageNode *err;
      r = lm_message_new_iq_error(m, XMPP_ERROR_BAD_REQUEST);
      err = lm_message_node_get_child(r->node, "error");
      lm_message_node_set_attribute
              (lm_message_node_add_child(err, "malformed-action", NULL),
               "xmlns", NS_COMMANDS);
      lm_connection_send(c, r, NULL);
      lm_message_unref(r);
    }
  } else {
    send_iq_error(c, m, XMPP_ERROR_FORBIDDEN);
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


LmHandlerResult handle_iq_disco_items(LmMessageHandler *h,
                                      LmConnection *c,
                                      LmMessage *m, gpointer ud)
{
  LmMessageNode *query;
  const char *node;
  query = lm_message_node_get_child(m->node, "query");
  node = lm_message_node_get_attribute(query, "node");
  if (node) {
    if (!strcmp(node, NS_COMMANDS)) {
      return handle_iq_commands_list(NULL, c, m, ud);
    } else {
      send_iq_error(c, m, XMPP_ERROR_NOT_IMPLEMENTED);
    }
  } else {
    // not sure about this one
    send_iq_error(c, m, XMPP_ERROR_NOT_IMPLEMENTED);
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}


void _disco_add_feature_helper(gpointer data, gpointer user_data)
{
  LmMessageNode *node = user_data;
  lm_message_node_set_attribute
          (lm_message_node_add_child(node, "feature", NULL), "var", data);
}

//  disco_info_set_caps(ansquery, entitycaps)
// Add features attributes to ansquery.  entitycaps should either be a
// valid capabilities hash or NULL. If it is NULL, the node attribute won't
// be added to the query child and Entity Capabilities will be announced
// as a feature.
// Please change the entity version string if you modify mcabber disco
// source code, so that it doesn't conflict with the upstream client.
static void disco_info_set_caps(LmMessageNode *ansquery,
                                const char *entitycaps)
{
  if (entitycaps) {
    char *eversion;
    eversion = g_strdup_printf("%s#%s", MCABBER_CAPS_NODE, entitycaps);
    lm_message_node_set_attribute(ansquery, "node", eversion);
    g_free(eversion);
  }

  lm_message_node_set_attributes
          (lm_message_node_add_child(ansquery, "identity", NULL),
           "category", "client",
           "name", PACKAGE_STRING,
           "type", "pc",
           NULL);

  if (entitycaps)
    caps_foreach_feature(entitycaps, _disco_add_feature_helper, ansquery);
  else {
    caps_foreach_feature(entity_version(xmpp_getstatus()),
                         _disco_add_feature_helper,
                         ansquery);
    lm_message_node_set_attribute
            (lm_message_node_add_child(ansquery, "feature", NULL),
             "var", NS_CAPS);
  }
}

LmHandlerResult handle_iq_disco_info(LmMessageHandler *h,
                                     LmConnection *c,
                                     LmMessage *m, gpointer ud)
{
  LmMessage *r;
  LmMessageNode *query, *tmp;
  const char *node = NULL;
  const char *param = NULL;

  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_RESULT)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  query = lm_message_node_add_child(r->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_DISCO_INFO);
  tmp = lm_message_node_find_child(m->node, "query");
  if (tmp) {
    node = lm_message_node_get_attribute(tmp, "node");
    param = node+strlen(MCABBER_CAPS_NODE)+1;
  }
  if (node && startswith(node, MCABBER_CAPS_NODE "#", FALSE))
    disco_info_set_caps(query, param);  // client#version
  else
    // Basic discovery request
    disco_info_set_caps(query, NULL);

  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

LmHandlerResult handle_iq_roster(LmMessageHandler *h, LmConnection *c,
                                 LmMessage *m, gpointer ud)
{
  LmMessageNode *y;
  const char *fjid, *name, *group, *sub, *ask;
  char *cleanalias;
  enum subscr esub;
  int need_refresh = FALSE;
  guint roster_type;

  for (y = lm_message_node_find_child(lm_message_node_find_xmlns
                                      (m->node, NS_ROSTER),
                                      "item");
       y;
       y = y->next) {
    char *name_tmp = NULL;

    fjid = lm_message_node_get_attribute(y, "jid");
    name = lm_message_node_get_attribute(y, "name");
    sub = lm_message_node_get_attribute(y, "subscription");
    ask = lm_message_node_get_attribute(y, "ask");

    if (lm_message_node_find_child(y, "group"))
      group = lm_message_node_get_value(lm_message_node_find_child(y, "group"));
    else
      group = NULL;

    if (!fjid)
      continue;

    cleanalias = jidtodisp(fjid);

    esub = sub_none;
    if (sub) {
      if (!strcmp(sub, "to"))          esub = sub_to;
      else if (!strcmp(sub, "from"))   esub = sub_from;
      else if (!strcmp(sub, "both"))   esub = sub_both;
      else if (!strcmp(sub, "remove")) esub = sub_remove;
    }

    if (esub == sub_remove) {
      roster_del_user(cleanalias);
      scr_LogPrint(LPRINT_LOGNORM, "Buddy <%s> has been removed "
                   "from the roster", cleanalias);
      g_free(cleanalias);
      need_refresh = TRUE;
      continue;
    }

    if (ask && !strcmp(ask, "subscribe"))
      esub |= sub_pending;

    if (!name) {
      if (!settings_opt_get_int("roster_hide_domain")) {
        name = cleanalias;
      } else {
        char *p;
        name = name_tmp = g_strdup(cleanalias);
        p = strchr(name_tmp, JID_DOMAIN_SEPARATOR);
        if (p)  *p = '\0';
      }
    }

    // Tricky... :-\  My guess is that if there is no JID_DOMAIN_SEPARATOR,
    // this is an agent.
    if (strchr(cleanalias, JID_DOMAIN_SEPARATOR))
      roster_type = ROSTER_TYPE_USER;
    else
      roster_type = ROSTER_TYPE_AGENT;

    roster_add_user(cleanalias, name, group, roster_type, esub, 1);

    g_free(name_tmp);
    g_free(cleanalias);
  }

  buddylist_build();
  update_roster = TRUE;
  if (need_refresh)
    scr_UpdateBuddyWindow();
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

LmHandlerResult handle_iq_ping(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer ud)
{
  LmMessage *r;

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

double seconds_since_last_use(void)
{
  return difftime(time(NULL), iqlast);
}

LmHandlerResult handle_iq_last(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer ud)
{
  LmMessage *r;
  LmMessageNode *query;
  char *seconds;

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ last time request from <%s>",
                 lm_message_get_from(m));
  }

  if (settings_opt_get_int("iq_last_disable") ||
      (settings_opt_get_int("iq_last_disable_when_notavail") &&
       xmpp_getstatus() == notavail))
  {
    send_iq_error(c, m, XMPP_ERROR_SERVICE_UNAVAILABLE);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  query = lm_message_node_add_child(r->node, "query", NULL);
  seconds = g_strdup_printf("%.0f", seconds_since_last_use());
  lm_message_node_set_attribute(query, "seconds", seconds);
  g_free(seconds);

  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

LmHandlerResult handle_iq_version(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer ud)
{
  LmMessage *r;
  LmMessageNode *query;
  char *os = NULL;
  char *ver = mcabber_version();

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ version request from <%s>",
                 lm_message_get_from(m));
  }
  if (!settings_opt_get_int("iq_version_hide_os")) {
    struct utsname osinfo;
    uname(&osinfo);
    os = g_strdup_printf("%s %s %s", osinfo.sysname, osinfo.release,
                         osinfo.machine);
  }

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);

  query = lm_message_node_add_child(r->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_VERSION);

  lm_message_node_add_child(query, "name", PACKAGE_NAME);
  lm_message_node_add_child(query, "version", ver);
  if (os) {
    lm_message_node_add_child(query, "os", os);
    g_free(os);
  }

  g_free(ver);
  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

// This function borrows some code from the Pidgin project
LmHandlerResult handle_iq_time(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer ud)
{
  LmMessage *r;
  LmMessageNode *query;
  char *buf, *utf8_buf;
  time_t now_t;
  struct tm *now;

  time(&now_t);

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ time request from <%s>",
                 lm_message_get_from(m));
  }

  buf = g_new0(char, 512);

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  query = lm_message_node_add_child(r->node, "query", NULL);

  now = gmtime(&now_t);

  strftime(buf, 512, "%Y%m%dT%T", now);
  lm_message_node_add_child(query, "utc", buf);

  now = localtime(&now_t);

  strftime(buf, 512, "%Z", now);
  if ((utf8_buf = to_utf8(buf))) {
    lm_message_node_add_child(query, "tz", utf8_buf);
    g_free(utf8_buf);
  }

  strftime(buf, 512, "%d %b %Y %T", now);
  if ((utf8_buf = to_utf8(buf))) {
    lm_message_node_add_child(query, "display", utf8_buf);
    g_free(utf8_buf);
  }

  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
  g_free(buf);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

// This function borrows some code from the Pidgin project
LmHandlerResult handle_iq_time202(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer ud)
{
  LmMessage *r;
  LmMessageNode *query;
  char *buf, *utf8_buf;
  time_t now_t;
  struct tm *now;
  char const *sign;
  int diff = 0;

  time(&now_t);

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ time request from <%s>",
                 lm_message_get_from(m));
  }

  buf = g_new0(char, 512);

  r = lm_message_new_iq_from_query(m, LM_MESSAGE_SUB_TYPE_RESULT);
  query = lm_message_node_add_child(r->node, "time", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_XMPP_TIME);

  now = localtime(&now_t);

  if (now->tm_isdst >= 0) {
#if defined HAVE_TM_GMTOFF
    diff = now->tm_gmtoff;
#elif defined HAVE_TIMEZONE
    tzset();
    diff = -timezone;
#endif
  }

  if (diff < 0) {
    sign = "-";
    diff = -diff;
  } else {
    sign = "+";
  }
  diff /= 60;
  snprintf(buf, 512, "%c%02d:%02d", *sign, diff / 60, diff % 60);
  if ((utf8_buf = to_utf8(buf))) {
    lm_message_node_add_child(query, "tzo", utf8_buf);
    g_free(utf8_buf);
  }

  now = gmtime(&now_t);

  strftime(buf, 512, "%Y-%m-%dT%TZ", now);
  lm_message_node_add_child(query, "utc", buf);

  lm_connection_send(c, r, NULL);
  lm_message_unref(r);
  g_free(buf);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

LmHandlerResult handle_iq_vcard(LmMessageHandler *h, LmConnection *c,
                                LmMessage *m, gpointer ud)
{
  send_iq_error(c, m, XMPP_ERROR_SERVICE_UNAVAILABLE);
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
