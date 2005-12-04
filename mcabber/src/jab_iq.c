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

#include "jabglue.h"
#include "jab_priv.h"
#include "roster.h"
#include "utils.h"
#include "logprint.h"

int s_id; // XXX

static void gotloggedin(void)
{
  xmlnode x;

  x = jutil_iqnew(JPACKET__GET, NS_AGENTS);
  xmlnode_put_attrib(x, "id", "Agent List");
  jab_send(jc, x);
  xmlnode_free(x);

  x = jutil_iqnew(JPACKET__GET, NS_ROSTER);
  xmlnode_put_attrib(x, "id", "Roster");
  jab_send(jc, x);
  xmlnode_free(x);
}

static void gotroster(xmlnode x)
{
  xmlnode y;
  const char *jid, *name, *group;
  char *buddyname;
  char *cleanalias;

  for (y = xmlnode_get_tag(x, "item"); y; y = xmlnode_get_nextsibling(y)) {
    gchar *name_noutf8 = NULL;
    gchar *group_noutf8 = NULL;

    jid = xmlnode_get_attrib(y, "jid");
    name = xmlnode_get_attrib(y, "name");
    //sub = xmlnode_get_attrib(y, "subscription"); // TODO Not used

    group = xmlnode_get_tag_data(y, "group");

    if (!jid)
      continue;

    buddyname = cleanalias = jidtodisp(jid);

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

    roster_add_user(cleanalias, buddyname, group_noutf8, ROSTER_TYPE_USER);
    if (name_noutf8)  g_free(name_noutf8);
    if (group_noutf8) g_free(group_noutf8);
    g_free(cleanalias);
  }

  // Post-login stuff
  jb_setstatus(available, NULL, NULL);
  buddylist_build();
}

static void gotagents(jconn conn, xmlnode x)
{
  xmlnode y;
  const char *alias;
  const char *name, *desc;

  y = xmlnode_get_tag(x, "agent");

  for (; y; y = xmlnode_get_nextsibling(y)) {
    enum agtype atype = unknown;

    alias = xmlnode_get_attrib(y, "jid");
    if (!alias)
      continue;

    name = xmlnode_get_tag_data(y, "name");
    desc = xmlnode_get_tag_data(y, "description");
    // TODO
    // service = xmlnode_get_tag_data(y, "service");

    if (xmlnode_get_tag(y, TMSG_GROUPCHAT))   atype = groupchat;
    else if (xmlnode_get_tag(y, "transport")) atype = transport;
    else if (xmlnode_get_tag(y, "search"))    atype = search;

    if (atype == transport) {
      char *cleanjid = jidtodisp(alias);
      roster_add_user(cleanjid, NULL, JABBER_AGENT_GROUP,
                      ROSTER_TYPE_AGENT);
      g_free(cleanjid);
    }
    if (alias && name && desc) {
      scr_LogPrint(LPRINT_LOGNORM, "Agent: %s / %s / %s / type=%d",
                   alias, name, desc, atype);

      if (atype == search) {
        x = jutil_iqnew (JPACKET__GET, NS_SEARCH);
        xmlnode_put_attrib(x, "to", alias);
        xmlnode_put_attrib(x, "id", "Agent info");
        jab_send(conn, x);
        xmlnode_free(x);
      }

      if (xmlnode_get_tag(y, "register")) {
        x = jutil_iqnew (JPACKET__GET, NS_REGISTER);
        xmlnode_put_attrib(x, "to", alias);
        xmlnode_put_attrib(x, "id", "Agent info");
        jab_send(conn, x);
        xmlnode_free(x);
      }
    }
  }

  /*
  if (find(jhook.agents.begin(), jhook.agents.end(), DEFAULT_CONFSERV) == jhook.agents.end())
    jhook.agents.insert(jhook.agents.begin(), agent(DEFAULT_CONFSERV, DEFAULT_CONFSERV,
                _("Default Jabber conference server"), agent::atGroupchat));

  */
}

static void handle_iq_result(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x;
  char *p;
  char *ns;

  if ((p = xmlnode_get_attrib(xmldata, "id")) != NULL) {
    int iid = atoi(p);

    //scr_LogPrint(LPRINT_DEBUG, "iid = %d", iid);
    scr_LogPrint(LPRINT_DEBUG, "iid = %d  sid = %d", iid, s_id); // XXX DBG XXX
    scr_LogPrint(LPRINT_DEBUG, "jstate = %d", jstate); // XXX DBG XXX
    if (iid == s_id) {
      if (jstate == STATE_GETAUTH) {
        if ((x = xmlnode_get_tag(xmldata, "query")) != NULL)
          if (!xmlnode_get_tag(x, "digest")) {
            jc->sid = 0;
          }

        scr_LogPrint(LPRINT_DEBUG, "jc = %p", jc); // XXX DBG XXX
        s_id = atoi(jab_auth(jc));
        jstate = STATE_SENDAUTH;
      } else if (jstate == STATE_SENDAUTH) {
        gotloggedin();
        jstate = STATE_LOGGED;
      }
      return;
    }

    if (!strcmp(p, "VCARDreq")) {
      x = xmlnode_get_firstchild(xmldata);
      if (!x) x = xmldata;

      //jhook.gotvcard(ic, x); TODO
      scr_LogPrint(LPRINT_LOGNORM, "Got VCARD");
      return;
    } else if (!strcmp(p, "versionreq")) {
      // jhook.gotversion(ic, xmldata); TODO
      scr_LogPrint(LPRINT_LOGNORM, "Got version");
      return;
    }
  }

  x = xmlnode_get_tag(xmldata, "query");
  if (!x) return;

  ns = xmlnode_get_attrib(x, "xmlns");
  if (!ns) return;

  if (!strcmp(ns, NS_ROSTER)) {
    gotroster(x);
  } else if (!strcmp(ns, NS_AGENTS)) {
    gotagents(conn, x);
  } else if (!strcmp(ns, NS_SEARCH) || !strcmp(ns, NS_REGISTER)) {
    char *id = xmlnode_get_attrib(xmldata, "id");
    if (!id) id = "";

    if (!strcmp(id, "Agent info")) {
      // jhook.gotagentinfo(xmldata); TODO
      scr_LogPrint(LPRINT_LOGNORM, "Got agent info");
    } else if (!strcmp(id, "Lookup")) {
      // jhook.gotsearchresults(xmldata); TODO
      scr_LogPrint(LPRINT_LOGNORM, "Got search results");
    } else if (!strcmp(id, "Register")) {
      if (!from)
        return;
      x = jutil_iqnew(JPACKET__GET, NS_REGISTER);
      xmlnode_put_attrib(x, "to", from);
      xmlnode_put_attrib(x, "id", "Agent info");
      jab_send(conn, x);
      xmlnode_free(x);
    }

  }
}

static void handle_iq_get(jconn conn, char *from, xmlnode xmldata)
{
  char *id;
  xmlnode x, y, z;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id) return;

  // Nothing implemented yet.
  x = xmlnode_new_tag("iq");
  xmlnode_put_attrib(x, "type", "result");
  xmlnode_put_attrib(x, "to", from);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "type", TMSG_ERROR);
  y = xmlnode_insert_tag(x, TMSG_ERROR);
  xmlnode_put_attrib(y, "code", "503");
  xmlnode_put_attrib(y, "type", "cancel");
  z = xmlnode_insert_tag(y, "feature-not-implemented");
  xmlnode_put_attrib(z, "xmlns",
                     "urn:ietf:params:xml:ns:xmpp-stanzas");
  jab_send(conn, x);
  xmlnode_free(x);
}

static void handle_iq_set(jconn conn, char *from, xmlnode xmldata)
{
  /* FIXME: send error */
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
