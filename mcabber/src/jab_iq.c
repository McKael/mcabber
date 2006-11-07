/*
 * jab_iq.c     -- Jabber protocol IQ-related fonctions
 *
 * Copyright (C) 2005, 2006 Mikael Berthe <bmikael@lists.lilotux.net>
 * Some parts initially came from the centericq project:
 * Copyright (C) 2002-2005 by Konstantin Klyagin <konst@konst.org.ua>
 * Some small parts come from the Gaim project <http://gaim.sourceforge.net/>
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

#include <sys/utsname.h>
#include <glib.h>

#include "jabglue.h"
#include "jab_priv.h"
#include "roster.h"
#include "utils.h"
#include "screen.h"
#include "settings.h"
#include "hbuf.h"


static GSList *iqs_list;


//  iqs_new(type, namespace, prefix, timeout)
// Create a query (GET, SET) IQ structure.  This function should not be used
// for RESULT packets.
eviqs *iqs_new(guint8 type, const char *ns, const char *prefix, time_t timeout)
{
  static guint iqs_idn;
  eviqs *new_iqs;
  time_t now_t;

  iqs_idn++;

  new_iqs = g_new0(eviqs, 1);
  time(&now_t);
  new_iqs->ts_create = now_t;
  if (timeout)
    new_iqs->ts_expire = now_t + timeout;
  new_iqs->type = type;
  new_iqs->xmldata = jutil_iqnew(type, (char*)ns);
  if (prefix)
    new_iqs->id = g_strdup_printf("%s_%d", prefix, iqs_idn);
  else
    new_iqs->id = g_strdup_printf("%d", iqs_idn);
  xmlnode_put_attrib(new_iqs->xmldata, "id", new_iqs->id);

  iqs_list = g_slist_append(iqs_list, new_iqs);
  return new_iqs;
}

int iqs_del(const char *iqid)
{
  GSList *p;
  eviqs *i;

  if (!iqid) return 1;

  for (p = iqs_list; p; p = g_slist_next(p)) {
    i = p->data;
    if (!strcmp(iqid, i->id))
      break;
  }
  if (p) {
    g_free(i->id);
    xmlnode_free(i->xmldata);
    g_free(i->data);
    g_free(i);
    iqs_list = g_slist_remove(iqs_list, p->data);
    return 0; // Ok, deleted
  }
  return -1;  // Not found
}

static eviqs *iqs_find(const char *iqid)
{
  GSList *p;
  eviqs *i;

  if (!iqid) return NULL;

  for (p = iqs_list; p; p = g_slist_next(p)) {
    i = p->data;
    if (!strcmp(iqid, i->id))
      return i;
  }
  return NULL;
}

//  iqs_callback(iqid, xml_result, iqcontext)
// Callback processing for the iqid message.
// If we've received an answer, xml_result should point to the xmldata packet.
// If this is a timeout, xml_result should be NULL.
// Return 0 in case of success, -1 if the iqid hasn't been found.
int iqs_callback(const char *iqid, xmlnode xml_result, guint iqcontext)
{
  eviqs *i;

  i = iqs_find(iqid);
  if (!i) return -1;

  // IQ processing
  // Note: If xml_result is NULL, this is a timeout
  if (i->callback)
    (*i->callback)(i, xml_result, iqcontext);

  iqs_del(iqid);
  return 0;
}

void iqs_check_timeout(time_t now_t)
{
  GSList *p;
  eviqs *i;

  p = iqs_list;
  while (p) {
    i = p->data;
    // We must get next IQ eviqs element now because the current one
    // could be freed.
    p = g_slist_next(p);

    if ((!i->ts_expire && now_t > i->ts_create + (time_t)IQS_MAX_TIMEOUT) ||
        (i->ts_expire && now_t > i->ts_expire)) {
      iqs_callback(i->id, NULL, IQS_CONTEXT_TIMEOUT);
    }
  }
}

void jb_iqs_display_list(void)
{
  GSList *p;
  eviqs *i;

  scr_LogPrint(LPRINT_LOGNORM, "IQ list:");
  for (p = iqs_list; p; p = g_slist_next(p)) {
    i = p->data;
    scr_LogPrint(LPRINT_LOGNORM, "Id [%s]", i->id);
  }
  scr_LogPrint(LPRINT_LOGNORM, "End of IQ list.");
}

static void request_roster(void)
{
  eviqs *iqn = iqs_new(JPACKET__GET, NS_ROSTER, "Roster", IQS_DEFAULT_TIMEOUT);
  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
}

static void handle_iq_roster(xmlnode x)
{
  xmlnode y;
  const char *jid, *name, *group, *sub, *ask;
  char *cleanalias;
  enum subscr esub;
  int need_refresh = FALSE;
  guint roster_type;

  for (y = xmlnode_get_tag(x, "item"); y; y = xmlnode_get_nextsibling(y)) {

    jid = xmlnode_get_attrib(y, "jid");
    name = xmlnode_get_attrib(y, "name");
    sub = xmlnode_get_attrib(y, "subscription");
    ask = xmlnode_get_attrib(y, "ask");

    group = xmlnode_get_tag_data(y, "group");

    if (!jid)
      continue;

    cleanalias = jidtodisp(jid);

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

    if (!name)
      name = cleanalias;

    // Tricky... :-\  My guess is that if there is no JID_DOMAIN_SEPARATOR,
    // this is an agent.
    if (strchr(cleanalias, JID_DOMAIN_SEPARATOR))
      roster_type = ROSTER_TYPE_USER;
    else
      roster_type = ROSTER_TYPE_AGENT;

    roster_add_user(cleanalias, name, group, roster_type, esub);

    g_free(cleanalias);
  }

  buddylist_build();
  update_roster = TRUE;
  if (need_refresh)
    scr_UpdateBuddyWindow();
}

static void iqscallback_version(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return;

  ansqry = xmlnode_get_tag(xml_result, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result!");
    return;
  }
  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result (no sender name).");
    return;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:version result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO);
  g_free(buf);

  // Get result data...
  p = xmlnode_get_tag_data(ansqry, "name");
  if (p) {
    buf = g_strdup_printf("Name:    %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_NONE);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "version");
  if (p) {
    buf = g_strdup_printf("Version: %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_NONE);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "os");
  if (p) {
    buf = g_strdup_printf("OS:      %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_NONE);
    g_free(buf);
  }
}

void request_version(const char *fulljid)
{
  eviqs *iqn;

  iqn = iqs_new(JPACKET__GET, NS_VERSION, "version", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", fulljid);
  iqn->callback = &iqscallback_version;
  jab_send(jc, iqn->xmldata);
}

static void iqscallback_time(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return;

  ansqry = xmlnode_get_tag(xml_result, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:time result!");
    return;
  }
  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:time result (no sender name).");
    return;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:time result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO);
  g_free(buf);

  // Get result data...
  p = xmlnode_get_tag_data(ansqry, "utc");
  if (p) {
    buf = g_strdup_printf("UTC:  %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_NONE);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "tz");
  if (p) {
    buf = g_strdup_printf("TZ:   %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_NONE);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "display");
  if (p) {
    buf = g_strdup_printf("Time: %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_NONE);
    g_free(buf);
  }
}

void request_time(const char *fulljid)
{
  eviqs *iqn;

  iqn = iqs_new(JPACKET__GET, NS_TIME, "time", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", fulljid);
  iqn->callback = &iqscallback_time;
  jab_send(jc, iqn->xmldata);
}

static void handle_vcard_node(const char *barejid, xmlnode vcardnode)
{
  xmlnode x;
  const char *p;
  char *buf;

  x = xmlnode_get_firstchild(vcardnode);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *title, *data;
    p = xmlnode_get_name(x);
    data = xmlnode_get_data(x);
    if (p && data) {
      title = NULL;
      if (!strcmp(p, "FN"))
        title = "Name";
      else if (!strcmp(p, "NICKNAME"))
        title = "Nickname";
      else if (!strcmp(p, "URL"))
        title = "URL";
      else if (!strcmp(p, "BDAY"))
        title = "Birthday";
      else if (!strcmp(p, "TZ"))
        title = "Timezone";
      else if (!strcmp(p, "TITLE"))
        title = "Title";
      else if (!strcmp(p, "ROLE"))
        title = "Role";
      else if (!strcmp(p, "DESC"))
        title = "Comment";
      else if (!strcmp(p, "N")) {
        data = xmlnode_get_tag_data(x, "FAMILY");
        if (data) {
          buf = g_strdup_printf("Family Name: %s", data);
          scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_NONE);
          g_free(buf);
        }
        data = xmlnode_get_tag_data(x, "GIVEN");
        if (data) {
          buf = g_strdup_printf("Given Name: %s", data);
          scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_NONE);
          g_free(buf);
        }
        data = xmlnode_get_tag_data(x, "MIDDLE");
        if (data) {
          buf = g_strdup_printf("Middle Name: %s", data);
          scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_NONE);
          g_free(buf);
        }
      } else if (!strcmp(p, "ADR")) {   // TODO
      } else if (!strcmp(p, "TEL")) {   // TODO
      } else if (!strcmp(p, "EMAIL")) {
        data = xmlnode_get_tag_data(x, "USERID");
        if (data)
          title = "Email"; // XXX
      } else if (!strcmp(p, "ORG")) {   // TODO
      }

      if (title) {
        buf = g_strdup_printf("%s: %s", title, data);
        scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_NONE);
        g_free(buf);
      }
    }
  }
}

static void iqscallback_vcard(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return;

  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:vCard result (no sender name).");
    return;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:vCard result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // Get the vCard node
  ansqry = xmlnode_get_tag(xml_result, "vCard");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Empty IQ:vCard result!");
    return;
  }

  // bjid should really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO);
  g_free(buf);

  // Get result data...
  handle_vcard_node(bjid, ansqry);
}

void request_vcard(const char *jid)
{
  eviqs *iqn;
  char *barejid;

  barejid = jidtodisp(jid);

  // Create a new IQ structure.  We use NULL for the namespace because
  // we'll have to use a special tag, not the usual "query" one.
  iqn = iqs_new(JPACKET__GET, NULL, "vcard", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", barejid);
  // Remove the useless <query/> tag, and insert a vCard one.
  xmlnode_hide(xmlnode_get_tag(iqn->xmldata, "query"));
  xmlnode_put_attrib(xmlnode_insert_tag(iqn->xmldata, "vCard"),
                     "xmlns", NS_VCARD);
  iqn->callback = &iqscallback_vcard;
  jab_send(jc, iqn->xmldata);

  g_free(barejid);
}

void iqscallback_auth(eviqs *iqp, xmlnode xml_result)
{
  if (jstate == STATE_GETAUTH) {
    eviqs *iqn;

    if (xml_result) {
      xmlnode x = xmlnode_get_tag(xml_result, "query");
      if (x && !xmlnode_get_tag(x, "digest"))
        jc->sid = 0;
    }

    iqn = iqs_new(JPACKET__SET, NS_AUTH, "auth", IQS_DEFAULT_TIMEOUT);
    iqn->callback = &iqscallback_auth;
    jab_auth_mcabber(jc, iqn->xmldata);
    jab_send(jc, iqn->xmldata);
    jstate = STATE_SENDAUTH;
  } else if (jstate == STATE_SENDAUTH) {
    request_roster();
    jstate = STATE_LOGGED;
  }
}

static void handle_iq_result(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x;
  char *id;
  char *ns;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id) {
    scr_LogPrint(LPRINT_LOG, "IQ result stanza with no ID, ignored.");
    return;
  }

  if (!iqs_callback(id, xmldata, IQS_CONTEXT_RESULT))
    return;

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

static void handle_iq_disco_info(jconn conn, char *from, const char *id,
                                 xmlnode xmldata)
{
  xmlnode x, y;
  xmlnode myquery;

  x = jutil_iqnew(JPACKET__RESULT, NS_DISCO_INFO);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "query");

  y = xmlnode_insert_tag(myquery, "identity");
  xmlnode_put_attrib(y, "category", "client");
  xmlnode_put_attrib(y, "type", "pc");
  xmlnode_put_attrib(y, "name", PACKAGE_NAME);

  xmlnode_put_attrib(xmlnode_insert_tag(myquery, "feature"),
                     "var", NS_DISCO_INFO);
  xmlnode_put_attrib(xmlnode_insert_tag(myquery, "feature"),
                     "var", NS_MUC);
  xmlnode_put_attrib(xmlnode_insert_tag(myquery, "feature"),
                     "var", NS_CHATSTATES);
  xmlnode_put_attrib(xmlnode_insert_tag(myquery, "feature"),
                     "var", NS_TIME);
  xmlnode_put_attrib(xmlnode_insert_tag(myquery, "feature"),
                     "var", NS_VERSION);

  jab_send(jc, x);
  xmlnode_free(x);
}

static void handle_iq_version(jconn conn, char *from, const char *id,
                              xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *os = NULL;
  char *ver = mcabber_version();

  scr_LogPrint(LPRINT_LOGNORM, "Received an IQ version request from <%s>",
               from);

  if (!settings_opt_get_int("iq_version_hide_os")) {
    struct utsname osinfo;
    uname(&osinfo);
    os = g_strdup_printf("%s %s %s", osinfo.sysname, osinfo.release,
                         osinfo.machine);
  }

  x = jutil_iqnew(JPACKET__RESULT, NS_VERSION);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "query");

  xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "name"), PACKAGE_NAME, -1);
  xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "version"), ver, -1);
  if (os) {
    xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "os"), os, -1);
    g_free(os);
  }

  g_free(ver);
  jab_send(jc, x);
  xmlnode_free(x);
}

// This function borrows some code from the Gaim project
static void handle_iq_time(jconn conn, char *from, const char *id,
                              xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *buf, *utf8_buf;
  time_t now_t;
  struct tm *now;

  time(&now_t);

  scr_LogPrint(LPRINT_LOGNORM, "Received an IQ time request from <%s>", from);

  buf = g_new0(char, 512);

  x = jutil_iqnew(JPACKET__RESULT, NS_TIME);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "query");

  now = gmtime(&now_t);

  strftime(buf, 512, "%Y%m%dT%T", now);
  xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "utc"), buf, -1);

  now = localtime(&now_t);

  strftime(buf, 512, "%Z", now);
  if ((utf8_buf = to_utf8(buf))) {
    xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "tz"), utf8_buf, -1);
    g_free(utf8_buf);
  }

  strftime(buf, 512, "%d %b %Y %T", now);
  if ((utf8_buf = to_utf8(buf))) {
    xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "display"), utf8_buf, -1);
    g_free(utf8_buf);
  }

  jab_send(jc, x);
  xmlnode_free(x);
  g_free(buf);
}

// This function borrows some code from the Gaim project
static void handle_iq_get(jconn conn, char *from, xmlnode xmldata)
{
  const char *id, *ns;
  xmlnode x, y, z;
  guint iq_not_implemented = FALSE;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id) {
    scr_LogPrint(LPRINT_LOG, "IQ get stanza with no ID, ignored.");
    return;
  }

  x = xmlnode_get_tag(xmldata, "query");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_DISCO_INFO)) {
    handle_iq_disco_info(conn, from, id, xmldata);
  } else if (ns && !strcmp(ns, NS_VERSION)) {
    handle_iq_version(conn, from, id, xmldata);
  } else if (ns && !strcmp(ns, NS_TIME)) {
    handle_iq_time(conn, from, id, xmldata);
  } else {
    iq_not_implemented = TRUE;
  }

  if (!iq_not_implemented)
    return;

  // Not implemented.
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
  const char *id, *ns;
  xmlnode x, y, z;
  guint iq_not_implemented = FALSE;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id)
    scr_LogPrint(LPRINT_LOG, "IQ set stanza with no ID...");

  x = xmlnode_get_tag(xmldata, "query");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_ROSTER)) {
    handle_iq_roster(x);
  } else {
    iq_not_implemented = TRUE;
  }

  if (!id) return;

  if (!iq_not_implemented) {
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
    iqs_callback(xmlnode_get_attrib(xmldata, "id"), NULL, IQS_CONTEXT_ERROR);
  }
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
