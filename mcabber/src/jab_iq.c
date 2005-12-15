/*
 * jab_iq.c     -- Jabber protocol IQ-related fonctions
 *
 * Copyright (C) 2005 Mikael Berthe <bmikael@lists.lilotux.net>
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

#include <glib.h>

#include "jabglue.h"
#include "jab_priv.h"
#include "roster.h"
#include "utils.h"
#include "screen.h"

int s_id; // XXX

static void request_roster(void)
{
  xmlnode x = jutil_iqnew(JPACKET__GET, NS_ROSTER);
  xmlnode_put_attrib(x, "id", "Roster1"); // XXX
  jab_send(jc, x);
  xmlnode_free(x);
}

static void handle_iq_roster(xmlnode x)
{
  xmlnode y;
  const char *jid, *name, *group, *sub, *ask;
  char *buddyname;
  char *cleanalias;
  enum subscr esub;
  int need_refresh = FALSE;
  guint roster_type;

  for (y = xmlnode_get_tag(x, "item"); y; y = xmlnode_get_nextsibling(y)) {
    gchar *name_noutf8 = NULL;
    gchar *group_noutf8 = NULL;

    jid = xmlnode_get_attrib(y, "jid");
    name = xmlnode_get_attrib(y, "name");
    sub = xmlnode_get_attrib(y, "subscription");
    ask = xmlnode_get_attrib(y, "ask");

    group = xmlnode_get_tag_data(y, "group");

    if (!jid)
      continue;

    buddyname = cleanalias = jidtodisp(jid);

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

    if (name) {
      name_noutf8 = from_utf8(name);
      if (name_noutf8)
        buddyname = name_noutf8;
      else
        scr_LogPrint(LPRINT_LOG, "Decoding of buddy alias has failed: %s",
                     name);
    }

    if (group) {
      group_noutf8 = from_utf8(group);
      if (!group_noutf8)
        scr_LogPrint(LPRINT_LOG, "Decoding of buddy group has failed: %s",
                     group);
    }

    // Tricky... :-\  My guess is that if there is no '@', this is an agent
    if (strchr(cleanalias, '@'))
      roster_type = ROSTER_TYPE_USER;
    else
      roster_type = ROSTER_TYPE_AGENT;

    roster_add_user(cleanalias, buddyname, group_noutf8, roster_type, esub);

    if (name_noutf8)  g_free(name_noutf8);
    if (group_noutf8) g_free(group_noutf8);
    g_free(cleanalias);
  }

  buddylist_build();
  update_roster = TRUE;
  if (need_refresh)
    scr_ShowBuddyWindow();
}

static void handle_iq_result(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x;
  char *p;
  char *ns;

  p = xmlnode_get_attrib(xmldata, "id");
  if (!p) {
    scr_LogPrint(LPRINT_LOG, "IQ result stanza with no ID, ignored.");
    return;
  }

  if (atoi(p) == s_id) {  // Authentication  XXX
    if (jstate == STATE_GETAUTH) {
      if ((x = xmlnode_get_tag(xmldata, "query")) != NULL)
        if (!xmlnode_get_tag(x, "digest")) {
          jc->sid = 0;
        }

      s_id = atoi(jab_auth(jc));
      jstate = STATE_SENDAUTH;
    } else if (jstate == STATE_SENDAUTH) {
      request_roster();
      jstate = STATE_LOGGED;
    }
    return;
  }

  /*
  if (!strcmp(p, "VCARDreq")) {
    x = xmlnode_get_firstchild(xmldata);
    if (!x) x = xmldata;

    scr_LogPrint(LPRINT_LOGNORM, "Got VCARD");    // TODO
    return;
  } else if (!strcmp(p, "versionreq")) {
    scr_LogPrint(LPRINT_LOGNORM, "Got version");  // TODO
    return;
  }
  */

  x = xmlnode_get_tag(xmldata, "query");
  if (!x) return;

  ns = xmlnode_get_attrib(x, "xmlns");
  if (!ns) return;

  if (!strcmp(ns, NS_ROSTER)) {
    handle_iq_roster(x);

    // Post-login stuff
    // Usually we request the roster only at connection time
    // so we should be there only once.  (That's ugly, however)
    jb_setstatus(available, NULL, NULL);
  }
}

static void handle_iq_get(jconn conn, char *from, xmlnode xmldata)
{
  char *id;
  xmlnode x, y, z;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id) {
    scr_LogPrint(LPRINT_LOG, "IQ get stanza with no ID, ignored.");
    return;
  }

  // Nothing implemented yet.
  x = xmlnode_dup(xmldata);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_hide_attrib(x, "from");

  xmlnode_put_attrib(x, "type", TMSG_ERROR);
  y = xmlnode_insert_tag(x, TMSG_ERROR);
  xmlnode_put_attrib(y, "code", "501");
  xmlnode_put_attrib(y, "type", "cancel");
  z = xmlnode_insert_tag(y, "feature-not-implemented");
  xmlnode_put_attrib(z, "xmlns", NS_XMPP_STANZAS);

  jab_send(conn, x);
  xmlnode_free(x);
}

static void handle_iq_set(jconn conn, char *from, xmlnode xmldata)
{
  char *id, *ns;
  xmlnode x, y, z;
  guint iq_error = FALSE;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id)
    scr_LogPrint(LPRINT_LOG, "IQ set stanza with no ID...");

  x = xmlnode_get_tag(xmldata, "query");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_ROSTER)) {
    handle_iq_roster(x);
  } else {
    iq_error = TRUE;
  }

  if (!id) return;

  if (!iq_error) {
    x = xmlnode_new_tag("iq");
    xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
    xmlnode_put_attrib(x, "type", "result");
    xmlnode_put_attrib(x, "id", id);
  } else {
    /* Not implemented yet: send an error stanza */
    x = xmlnode_dup(xmldata);
    xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
    xmlnode_hide_attrib(x, "from");
    xmlnode_put_attrib(x, "type", "result");
    xmlnode_put_attrib(x, "type", TMSG_ERROR);
    y = xmlnode_insert_tag(x, TMSG_ERROR);
    xmlnode_put_attrib(y, "code", "501");
    xmlnode_put_attrib(y, "type", "cancel");
    z = xmlnode_insert_tag(y, "feature-not-implemented");
    xmlnode_put_attrib(z, "xmlns", NS_XMPP_STANZAS);
  }

  jab_send(conn, x);
  xmlnode_free(x);
}

void handle_packet_iq(jconn conn, char *type, char *from, xmlnode xmldata)
{
  if (!type)
    return;

  if (!strcmp(type, "result")) {
    handle_iq_result(conn, from, xmldata);
  } else if (!strcmp(type, "get")) {
    handle_iq_get(conn, from, xmldata);
  } else if (!strcmp(type, "set")) {
    handle_iq_set(conn, from, xmldata);
  } else if (!strcmp(type, TMSG_ERROR)) {
    xmlnode x = xmlnode_get_tag(xmldata, TMSG_ERROR);
    if (x)
      display_server_error(x);
  }
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
