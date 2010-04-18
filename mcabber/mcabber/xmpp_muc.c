/*
 * xmpp_muc.c   -- Jabber MUC protocol handling
 *
 * Copyright (C) 2008-2010 Frank Zschockelt <mcabber@freakysoft.de>
 * Copyright (C) 2005-2010 Mikael Berthe <mikael@lilotux.net>
 * Copyrigth (C) 2010      Myhailo Danylenko <isbear@ukrposte.net>
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

#include "xmpp_helper.h"
#include "xmpp_muc.h"
#include "events.h"
#include "hooks.h"
#include "screen.h"
#include "hbuf.h"
#include "roster.h"
#include "commands.h"
#include "settings.h"
#include "utils.h"
#include "histolog.h"

extern enum imstatus mystatus;
extern gchar *mystatusmsg;

static void decline_invitation(event_muc_invitation *invitation, const char *reason)
{
  // cut and paste from xmpp_room_invite
  LmMessage *m;
  LmMessageNode *x, *y;

  if (!invitation) return;
  if (!invitation->to || !invitation->from) return;

  m = lm_message_new(invitation->to, LM_MESSAGE_TYPE_MESSAGE);

  x = lm_message_node_add_child(m->node, "x", NULL);
  lm_message_node_set_attribute(x, "xmlns", NS_MUC_USER);

  y = lm_message_node_add_child(x, "decline", NULL);
  lm_message_node_set_attribute(y, "to", invitation->from);

  if (reason)
    lm_message_node_add_child(y, "reason", reason);

  lm_connection_send(lconnection, m, NULL);
  lm_message_unref(m);
}

void destroy_event_muc_invitation(event_muc_invitation *invitation)
{
  g_free(invitation->to);
  g_free(invitation->from);
  g_free(invitation->passwd);
  g_free(invitation->reason);
  g_free(invitation);
}

// invitation event handler
// TODO: if event is accepted, check if other events to the same room exist and
// destroy them? (need invitation registry list for that)
static gboolean evscallback_invitation(guint evcontext, const char *arg, gpointer userdata)
{
  event_muc_invitation *invitation = userdata;

  // Sanity check
  if (G_UNLIKELY(!invitation)) {
    // Shouldn't happen.
    scr_LogPrint(LPRINT_LOGNORM, "Error in evs callback.");
    return FALSE;
  }

  if (evcontext == EVS_CONTEXT_TIMEOUT) {
    scr_LogPrint(LPRINT_LOGNORM, "Invitation event %s timed out, cancelled.", invitation->to);
    return FALSE;
  }
  if (evcontext == EVS_CONTEXT_CANCEL) {
    scr_LogPrint(LPRINT_LOGNORM, "Invitation event %s cancelled.", invitation->to);
    return FALSE;
  }
  if (!(evcontext == EVS_CONTEXT_ACCEPT || evcontext == EVS_CONTEXT_REJECT))
    return FALSE;

  // Ok, let's work now
  if (evcontext == EVS_CONTEXT_ACCEPT) {
    char *nickname = default_muc_nickname(invitation->to);
    xmpp_room_join(invitation->to, nickname, invitation->passwd);
    g_free(nickname);
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "Invitation to %s refused.", invitation->to);
    decline_invitation(invitation, arg);
  }

  return FALSE;
}

// Join a MUC room
void xmpp_room_join(const char *room, const char *nickname, const char *passwd)
{
  LmMessage *x;
  LmMessageNode *y;
  gchar *roomid;
  GSList *room_elt;

  if (!xmpp_is_online() || !room)
    return;
  if (!nickname)        return;

  roomid = g_strdup_printf("%s/%s", room, nickname);
  if (check_jid_syntax(roomid)) {
    scr_LogPrint(LPRINT_NORMAL, "<%s/%s> is not a valid Jabber room", room,
                 nickname);
    g_free(roomid);
    return;
  }

  room_elt = roster_find(room, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_ROOM);
  // Add room if it doesn't already exist
  if (!room_elt) {
    room_elt = roster_add_user(room, NULL, NULL, ROSTER_TYPE_ROOM,
                               sub_none, -1);
  } else {
    // Make sure this is a room (it can be a conversion user->room)
    buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);
  }
  // If insideroom is TRUE, this is a nickname change and we don't care here
  if (!buddy_getinsideroom(room_elt->data)) {
    // We're trying to enter a room
    buddy_setnickname(room_elt->data, nickname);
  }

  // Send the XML request
  x = lm_message_new_presence(mystatus, roomid, mystatusmsg);
  y = lm_message_node_add_child(x->node, "x", NULL);
  lm_message_node_set_attribute(y, "xmlns", NS_MUC);
  if (passwd)
    lm_message_node_add_child(y, "password", passwd);

  lm_connection_send(lconnection, x, NULL);
  lm_message_unref(x);
  g_free(roomid);
}

// Invite a user to a MUC room
// room syntax: "room@server"
// reason can be null.
void xmpp_room_invite(const char *room, const char *fjid, const char *reason)
{
  LmMessage *msg;
  LmMessageNode *x, *y;

  if (!xmpp_is_online() || !room || !fjid)
    return;

  msg = lm_message_new(room, LM_MESSAGE_TYPE_MESSAGE);

  x = lm_message_node_add_child(msg->node, "x", NULL);
  lm_message_node_set_attribute(x, "xmlns", NS_MUC_USER);

  y = lm_message_node_add_child(x, "invite", NULL);
  lm_message_node_set_attribute(y, "to", fjid);

  if (reason)
    lm_message_node_add_child(y, "reason", reason);

  lm_connection_send(lconnection, msg, NULL);
  lm_message_unref(msg);
}

int xmpp_room_setattrib(const char *roomid, const char *fjid,
                        const char *nick, struct role_affil ra,
                        const char *reason)
{
  LmMessage *iq;
  LmMessageNode *query, *x;

  if (!xmpp_is_online() || !roomid)
    return 1;
  if (!fjid && !nick) return 1;

  if (check_jid_syntax((char*)roomid)) {
    scr_LogPrint(LPRINT_NORMAL, "<%s> is not a valid Jabber id", roomid);
    return 1;
  }
  if (fjid && check_jid_syntax((char*)fjid)) {
    scr_LogPrint(LPRINT_NORMAL, "<%s> is not a valid Jabber id", fjid);
    return 1;
  }

  if (ra.type == type_affil && ra.val.affil == affil_outcast && !fjid)
    return 1; // Shouldn't happen (jid mandatory when banning)

  iq = lm_message_new_with_sub_type(roomid, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);
  query = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_MUC_ADMIN);
  x = lm_message_node_add_child(query, "item", NULL);

  if (fjid) {
    lm_message_node_set_attribute(x, "jid", fjid);
  } else { // nickname
    lm_message_node_set_attribute(x, "nick", nick);
  }

  if (ra.type == type_affil)
    lm_message_node_set_attribute(x, "affiliation", straffil[ra.val.affil]);
  else if (ra.type == type_role)
    lm_message_node_set_attribute(x, "role", strrole[ra.val.role]);

  if (reason)
    lm_message_node_add_child(x, "reason", reason);

  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);

  return 0;
}

// Unlock a MUC room
// room syntax: "room@server"
void xmpp_room_unlock(const char *room)
{
  LmMessageNode *node;
  LmMessage *iq;

  if (!xmpp_is_online() || !room)
    return;

  iq = lm_message_new_with_sub_type(room, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  node = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(node, "xmlns", NS_MUC_OWNER);
  node = lm_message_node_add_child(node, "x", NULL);
  lm_message_node_set_attributes(node, "xmlns", "jabber:x:data",
                                 "type", "submit", NULL);

  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);
}

// Destroy a MUC room
// room syntax: "room@server"
void xmpp_room_destroy(const char *room, const char *venue, const char *reason)
{
  LmMessage *iq;
  LmMessageNode *query, *x;

  if (!xmpp_is_online() || !room)
    return;

  iq = lm_message_new_with_sub_type(room, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);
  query = lm_message_node_add_child(iq->node, "query", NULL);
  lm_message_node_set_attribute(query, "xmlns", NS_MUC_OWNER);
  x = lm_message_node_add_child(query, "destroy", NULL);

  if (venue && *venue)
    lm_message_node_set_attribute(x, "jid", venue);

  if (reason)
    lm_message_node_add_child(x, "reason", reason);

  lm_connection_send(lconnection, iq, NULL);
  lm_message_unref(iq);
}

//  muc_get_item_info(...)
// Get room member's information from xmlndata.
// The variables must be initialized before calling this function,
// because they are not touched if the relevant information is missing.
static void muc_get_item_info(const char *from, LmMessageNode *xmldata,
                              enum imrole *mbrole, enum imaffiliation *mbaffil,
                              const char **mbjid, const char **mbnick,
                              const char **actorjid, const char **reason)
{
  LmMessageNode *y, *z;
  const char *p;

  y = lm_message_node_find_child(xmldata, "item");
  if (!y)
    return;

  p = lm_message_node_get_attribute(y, "affiliation");
  if (p) {
    if (!strcmp(p, "owner"))        *mbaffil = affil_owner;
    else if (!strcmp(p, "admin"))   *mbaffil = affil_admin;
    else if (!strcmp(p, "member"))  *mbaffil = affil_member;
    else if (!strcmp(p, "outcast")) *mbaffil = affil_outcast;
    else if (!strcmp(p, "none"))    *mbaffil = affil_none;
    else scr_LogPrint(LPRINT_LOGNORM, "<%s>: Unknown affiliation \"%s\"",
                      from, p);
  }
  p = lm_message_node_get_attribute(y, "role");
  if (p) {
    if (!strcmp(p, "moderator"))        *mbrole = role_moderator;
    else if (!strcmp(p, "participant")) *mbrole = role_participant;
    else if (!strcmp(p, "visitor"))     *mbrole = role_visitor;
    else if (!strcmp(p, "none"))        *mbrole = role_none;
    else scr_LogPrint(LPRINT_LOGNORM, "<%s>: Unknown role \"%s\"",
                      from, p);
  }
  *mbjid = lm_message_node_get_attribute(y, "jid");
  *mbnick = lm_message_node_get_attribute(y, "nick");
  // For kick/ban, there can be actor and reason tags
  *reason = lm_message_node_get_child_value(y, "reason");
  if (*reason && !**reason)
    *reason = NULL;
  z = lm_message_node_find_child(y, "actor");
  if (z)
    *actorjid = lm_message_node_get_attribute(z, "jid");
}

//  muc_handle_join(...)
// Handle a join event in a MUC room.
// This function will return the new_member value TRUE if somebody else joins
// the room (and FALSE if _we_ are joining the room).
static bool muc_handle_join(const GSList *room_elt, const char *rname,
                            const char *roomjid, const char *ournick,
                            enum room_printstatus printstatus,
                            time_t usttime, int log_muc_conf)
{
  bool new_member = FALSE; // True if somebody else joins the room (not us)
  gchar *mbuf;

  if (!buddy_getinsideroom(room_elt->data)) {
    // We weren't inside the room yet.  Now we are.
    // However, this could be a presence packet from another room member

    buddy_setinsideroom(room_elt->data, TRUE);
    // Set the message flag unless we're already in the room buffer window
    scr_setmsgflag_if_needed(roomjid, FALSE);
    // Add a message to the tracelog file
    mbuf = g_strdup_printf("You have joined %s as \"%s\"", roomjid, ournick);
    scr_LogPrint(LPRINT_LOGNORM, "%s", mbuf);
    g_free(mbuf);
    mbuf = g_strdup_printf("You have joined as \"%s\"", ournick);

    // The 1st presence message could be for another room member
    if (strcmp(ournick, rname)) {
      // Display current mbuf and create a new message for the member
      // Note: the usttime timestamp is related to the other member,
      //       so we use 0 here.
      scr_WriteIncomingMessage(roomjid, mbuf, 0,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
      if (log_muc_conf)
        hlog_write_message(roomjid, 0, -1, mbuf);
      g_free(mbuf);
      if (printstatus != status_none)
        mbuf = g_strdup_printf("%s has joined", rname);
      else
        mbuf = NULL;
      new_member = TRUE;
    }
  } else {
    mbuf = NULL;
    if (strcmp(ournick, rname)) {
      if (printstatus != status_none)
        mbuf = g_strdup_printf("%s has joined", rname);
      new_member = TRUE;
    }
  }

  if (mbuf) {
    guint msgflags = HBB_PREFIX_INFO;
    if (!settings_opt_get_int("muc_flag_joins"))
      msgflags |= HBB_PREFIX_NOFLAG;
    scr_WriteIncomingMessage(roomjid, mbuf, usttime, msgflags, 0);
    if (log_muc_conf)
      hlog_write_message(roomjid, 0, -1, mbuf);
    g_free(mbuf);
  }

  return new_member;
}

void handle_muc_presence(const char *from, LmMessageNode *xmldata,
                         const char *roomjid, const char *rname,
                         enum imstatus ust, const char *ustmsg,
                         time_t usttime, char bpprio)
{
  LmMessageNode *y;
  const char *p;
  char *mbuf;
  const char *ournick;
  enum imrole mbrole = role_none;
  enum imaffiliation mbaffil = affil_none;
  enum room_printstatus printstatus;
  enum room_autowhois autowhois;
  const char *mbjid = NULL, *mbnick = NULL;
  const char *actorjid = NULL, *reason = NULL;
  bool new_member = FALSE; // True if somebody else joins the room (not us)
  guint statuscode = 0;
  guint nickchange = 0;
  GSList *room_elt;
  int log_muc_conf;
  guint msgflags;

  log_muc_conf = settings_opt_get_int("log_muc_conf");

  room_elt = roster_find(roomjid, jidsearch, 0);
  if (!room_elt) {
    // Add room if it doesn't already exist
    // It shouldn't happen, there is probably something wrong (server or
    // network issue?)
    room_elt = roster_add_user(roomjid, NULL, NULL, ROSTER_TYPE_ROOM,
                               sub_none, -1);
    scr_LogPrint(LPRINT_LOGNORM, "Strange MUC presence message");
  } else {
    // Make sure this is a room (it can be a conversion user->room)
    buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);
  }

  // Get room member's information
  muc_get_item_info(from, xmldata, &mbrole, &mbaffil, &mbjid, &mbnick,
                    &actorjid, &reason);

  // Get our room nickname
  ournick = buddy_getnickname(room_elt->data);

  if (!ournick) {
    // It shouldn't happen, probably a server issue
    mbuf = g_strdup_printf("Unexpected groupchat packet!");

    scr_LogPrint(LPRINT_LOGNORM, "%s", mbuf);
    scr_WriteIncomingMessage(roomjid, mbuf, 0, HBB_PREFIX_INFO, 0);
    g_free(mbuf);
    // Send back an unavailable packet
    xmpp_setstatus(offline, roomjid, "", TRUE);
    scr_draw_roster();
    return;
  }

  // Get the status code
  // 201: a room has been created
  // 301: the user has been banned from the room
  // 303: new room nickname
  // 307: the user has been kicked from the room
  // 321,322,332: the user has been removed from the room
  y = lm_message_node_find_child(xmldata, "status");
  if (y) {
    p = lm_message_node_get_attribute(y, "code");
    if (p)
      statuscode = atoi(p);
  }

  // Get the room's "print_status" settings
  printstatus = buddy_getprintstatus(room_elt->data);
  if (printstatus == status_default) {
    printstatus = (guint) settings_opt_get_int("muc_print_status");
    if (printstatus > 3)
      printstatus = status_default;
  }

  // A new room has been created; accept MUC default config
  if (statuscode == 201)
    xmpp_room_unlock(roomjid);

  // Check for nickname change
  if (statuscode == 303 && mbnick) {
    mbuf = g_strdup_printf("%s is now known as %s", rname, mbnick);
    scr_WriteIncomingMessage(roomjid, mbuf, usttime,
                             HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
    if (log_muc_conf)
      hlog_write_message(roomjid, 0, -1, mbuf);
    g_free(mbuf);
    buddy_resource_setname(room_elt->data, rname, mbnick);
    // Maybe it's _our_ nickname...
    if (ournick && !strcmp(rname, ournick))
      buddy_setnickname(room_elt->data, mbnick);
    nickchange = TRUE;
  }

  // Check for departure/arrival
  if (statuscode != 303 && ust == offline) {
    // Somebody is leaving
    enum { leave=0, kick, ban } how = leave;
    bool we_left = FALSE;

    if (statuscode == 307)
      how = kick;
    else if (statuscode == 301)
      how = ban;

    // If this is a leave, check if it is ourself
    if (ournick && !strcmp(rname, ournick)) {
      we_left = TRUE; // _We_ have left! (kicked, banned, etc.)
      buddy_setinsideroom(room_elt->data, FALSE);
      buddy_setnickname(room_elt->data, NULL);
      buddy_del_all_resources(room_elt->data);
      buddy_settopic(room_elt->data, NULL);
      scr_update_chat_status(FALSE);
      update_roster = TRUE;
    }

    // The message depends on _who_ left, and _how_
    if (how) {
      gchar *mbuf_end;
      gchar *reason_msg = NULL;
      // Forced leave
      if (actorjid) {
        mbuf_end = g_strdup_printf("%s from %s by <%s>.",
                                   (how == ban ? "banned" : "kicked"),
                                   roomjid, actorjid);
      } else {
        mbuf_end = g_strdup_printf("%s from %s.",
                                   (how == ban ? "banned" : "kicked"),
                                   roomjid);
      }
      if (reason)
        reason_msg = g_strdup_printf("\nReason: %s", reason);
      if (we_left)
        mbuf = g_strdup_printf("You have been %s%s", mbuf_end,
                               reason_msg ? reason_msg : "");
      else
        mbuf = g_strdup_printf("%s has been %s%s", rname, mbuf_end,
                               reason_msg ? reason_msg : "");

      g_free(reason_msg);
      g_free(mbuf_end);
    } else {
      // Natural leave
      if (we_left) {
        LmMessageNode *destroynode = lm_message_node_find_child(xmldata,
                                                                "destroy");
        if (destroynode) {
          reason = lm_message_node_get_child_value(destroynode, "reason");
          if (reason && *reason) {
            mbuf = g_strdup_printf("You have left %s, "
                                   "the room has been destroyed: %s",
                                   roomjid, reason);
          } else {
            mbuf = g_strdup_printf("You have left %s, "
                                   "the room has been destroyed", roomjid);
          }
        } else {
          mbuf = g_strdup_printf("You have left %s", roomjid);
        }
      } else {
        if (ust != offline) {
          // This can happen when a network failure occurs,
          // this isn't an official leave but the user isn't there anymore.
          mbuf = g_strdup_printf("%s has disappeared!", rname);
          ust = offline;
        } else {
          if (ustmsg)
            mbuf = g_strdup_printf("%s has left: %s", rname, ustmsg);
          else
            mbuf = g_strdup_printf("%s has left", rname);
        }
      }
    }

    // Display the mbuf message if we're concerned
    // or if the print_status isn't set to none.
    if (we_left || printstatus != status_none) {
      msgflags = HBB_PREFIX_INFO;
      if (!we_left && settings_opt_get_int("muc_flag_joins") != 2)
        msgflags |= HBB_PREFIX_NOFLAG;
      scr_WriteIncomingMessage(roomjid, mbuf, usttime, msgflags, 0);
    }

    if (log_muc_conf)
      hlog_write_message(roomjid, 0, -1, mbuf);

    if (we_left) {
      scr_LogPrint(LPRINT_LOGNORM, "%s", mbuf);
      g_free(mbuf);
      return;
    }
    g_free(mbuf);
  } else if (buddy_getstatus(room_elt->data, rname) == offline &&
             ust != offline) {
    // Somebody is joining
    new_member = muc_handle_join(room_elt, rname, roomjid, ournick,
                                 printstatus, usttime, log_muc_conf);
  } else {
    // This is a simple member status change

    if (printstatus == status_all && !nickchange) {
      mbuf = g_strdup_printf("Member status has changed: %s [%c] %s", rname,
                             imstatus2char[ust], ((ustmsg) ? ustmsg : ""));
      scr_WriteIncomingMessage(roomjid, mbuf, usttime,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
      g_free(mbuf);
    }
  }

  // Sanity check, shouldn't happen...
  if (!rname)
    return;

  // Update room member status
  roster_setstatus(roomjid, rname, bpprio, ust, ustmsg, usttime,
                   mbrole, mbaffil, mbjid);

  autowhois = buddy_getautowhois(room_elt->data);
  if (autowhois == autowhois_default)
    autowhois = (settings_opt_get_int("muc_auto_whois") ?
                 autowhois_on : autowhois_off);

  if (new_member && autowhois == autowhois_on) {
    // FIXME: This will fail for some UTF-8 nicknames.
    gchar *joiner_nick = from_utf8(rname);
    cmd_room_whois(room_elt->data, joiner_nick, FALSE);
    g_free(joiner_nick);
  }

  scr_draw_roster();
}

void roompresence(gpointer room, void *presencedata)
{
  const char *bjid;
  const char *nickname;
  char *to;
  struct T_presence *pres = presencedata;

  if (!buddy_getinsideroom(room))
    return;

  bjid = buddy_getjid(room);
  if (!bjid) return;
  nickname = buddy_getnickname(room);
  if (!nickname) return;

  to = g_strdup_printf("%s/%s", bjid, nickname);
  xmpp_setstatus(pres->st, to, pres->msg, TRUE);
  g_free(to);
}

//  got_invite(from, to, reason, passwd)
// This function should be called when receiving an invitation from user
// "from", to enter the room "to".  Optional reason and room password can
// be provided.
// TODO: check for duplicate invites (need an existing invitation registry
// for that).
static void got_invite(const char* from, const char *to, const char* reason,
                       const char* passwd)
{
  GString *sbuf;
  char *barejid;
  GSList *room_elt;

  sbuf = g_string_new("");
  if (reason) {
    g_string_printf(sbuf,
                    "Received an invitation to <%s>, from <%s>, reason: %s",
                    to, from, reason);
  } else {
    g_string_printf(sbuf, "Received an invitation to <%s>, from <%s>",
                    to, from);
  }

  barejid = jidtodisp(from);
  scr_WriteIncomingMessage(barejid, sbuf->str, 0, HBB_PREFIX_INFO, 0);
  scr_LogPrint(LPRINT_LOGNORM, "%s", sbuf->str);

  {
    const char *id;
    char *desc = g_strdup_printf("<%s> invites you to %s", from, to);
    event_muc_invitation *invitation;

    invitation = g_new(event_muc_invitation, 1);
    invitation->to = g_strdup(to);
    invitation->from = g_strdup(from);
    invitation->passwd = g_strdup(passwd);
    invitation->reason = g_strdup(reason);

    id = evs_new(desc, NULL, 0, evscallback_invitation, invitation,
                 (GDestroyNotify)destroy_event_muc_invitation);
    g_free(desc);
    if (id)
      g_string_printf(sbuf, "Please use /event %s accept|reject", id);
    else
      g_string_printf(sbuf, "Unable to create a new event!");
  }
  scr_WriteIncomingMessage(barejid, sbuf->str, 0, HBB_PREFIX_INFO, 0);
  scr_LogPrint(LPRINT_LOGNORM, "%s", sbuf->str);
  g_string_free(sbuf, TRUE);
  g_free(barejid);

  // Make sure the MUC room barejid is a room in the roster
  barejid = jidtodisp(to);
  room_elt = roster_find(barejid, jidsearch, 0);
  if (room_elt)
    buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);

  g_free(barejid);
}


// Specific MUC message handling (for example invitation processing)
void got_muc_message(const char *from, LmMessageNode *x)
{
  LmMessageNode *invite = lm_message_node_get_child(x, "invite");
  if (invite)
  {
    const char *invite_from;
    const char *reason = NULL;
    const char *password = NULL;

    invite_from = lm_message_node_get_attribute(invite, "from");
    reason = lm_message_node_get_child_value(invite, "reason");
    password = lm_message_node_get_child_value(invite, "password");
    if (invite_from)
      got_invite(invite_from, from, reason, password);
  }
  // TODO
  // handle status code = 100 ( not anonymous )
  // handle status code = 170 ( changement de config )
  // 10.2.1 Notification of Configuration Changes
  // declined invitation
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
