/*
 * jab_iq.c     -- Jabber protocol IQ-related fonctions
 *
 * Copyright (C) 2005-2008 Mikael Berthe <mikael@lilotux.net>
 * Some parts initially came from the centericq project:
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

#include <sys/utsname.h>
#include <glib.h>

#include "jabglue.h"
#include "jab_priv.h"
#include "roster.h"
#include "utils.h"
#include "screen.h"
#include "settings.h"
#include "hbuf.h"
#include "commands.h"
#include "hooks.h"

#ifdef ENABLE_HGCSET
# include "hgcset.h"
#endif


// Bookmarks for IQ:private storage
xmlnode bookmarks;
// Roster notes for IQ:private storage
xmlnode rosternotes;

static GSList *iqs_list;

time_t iqlast; // last message/status change time

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

static void handle_iq_command_set_status(jconn conn, char *from,
                                         const char *id, xmlnode xmldata);

static void handle_iq_command_leave_groupchats(jconn conn, char *from,
                                               const char *id, xmlnode xmldata);

typedef void (*adhoc_command_callback)(jconn, char*, const char*, xmlnode);

inline double seconds_since_last_use(void);

struct adhoc_command {
  char *name;
  char *description;
  bool only_for_self;
  adhoc_command_callback callback;
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

const struct adhoc_status adhoc_status_list[] = {
  {"online", "Online", "avail"},
  {"chat", "Chat", "free"},
  {"away", "Away", "away"},
  {"xd", "Extended away", "notavail"},
  {"dnd", "Do not disturb", "dnd"},
  {"invisible", "Invisible", "invisible"},
  {"offline", "Offline", "offline"},
  {NULL, NULL, NULL},
};

//  entity_version()
// Return a static version string for Entity Capabilities.
// It should be specific to the client version, please change the id
// if you alter mcabber's disco support (or add something to the version
// number) so that it doesn't conflict with the official client.
const char *entity_version(void)
{
  static char *ver;
  const char *PVERSION = PACKAGE_VERSION; // "+xxx";

  if (ver)
    return ver;

#ifdef HGCSET
  ver = g_strdup_printf("%s-%s", PVERSION, HGCSET);
#else
  ver = g_strdup(PVERSION);
#endif

  return ver;
}

//  iqs_new(type, namespace, prefix, timeout)
// Create a query (GET, SET) IQ structure.  This function should not be used
// for RESULT packets.
eviqs *iqs_new(guint8 type, const char *ns, const char *prefix, time_t tmout)
{
  static guint iqs_idn;
  eviqs *new_iqs;
  time_t now_t;

  iqs_idn++;

  new_iqs = g_new0(eviqs, 1);
  time(&now_t);
  new_iqs->ts_create = now_t;
  if (tmout)
    new_iqs->ts_expire = now_t + tmout;
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
  int retval = 0;

  i = iqs_find(iqid);
  if (!i) return -1;

  // IQ processing
  // Note: If xml_result is NULL, this is a timeout
  if (i->callback)
    retval = (*i->callback)(i, xml_result, iqcontext);

  iqs_del(iqid);
  return retval;
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

static void handle_iq_roster(xmlnode x)
{
  xmlnode y;
  const char *fjid, *name, *group, *sub, *ask;
  char *cleanalias;
  enum subscr esub;
  int need_refresh = FALSE;
  guint roster_type;

  for (y = xmlnode_get_tag(x, "item"); y; y = xmlnode_get_nextsibling(y)) {
    char *name_tmp = NULL;

    fjid = xmlnode_get_attrib(y, "jid");
    name = xmlnode_get_attrib(y, "name");
    sub = xmlnode_get_attrib(y, "subscription");
    ask = xmlnode_get_attrib(y, "ask");

    group = xmlnode_get_tag_data(y, "group");

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
}

//  This callback is reached when mcabber receives the first roster update
// after the connection.
static int iqscallback_gotroster(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode x;
  char *ns;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return -1;

  // Only execute the hook if the roster has been successfully retrieved
  if (iqcontext != IQS_CONTEXT_RESULT)
    return 0;

  x = xmlnode_get_tag(xml_result, "query");
  if (!x)
    return -1;

  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_ROSTER))
    handle_iq_roster(x);

  // Post-login stuff
  jb_setprevstatus();
  hook_execute_internal("hook-post-connect");

  return 0;
}

static void request_roster(void)
{
  eviqs *iqn = iqs_new(JPACKET__GET, NS_ROSTER, "Roster", IQS_DEFAULT_TIMEOUT);
  iqn->callback = &iqscallback_gotroster;
  jab_send(jc, iqn->xmldata);
}

static int iqscallback_version(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return -1;

  ansqry = xmlnode_get_tag(xml_result, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result!");
    return 0;
  }
  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:version result (no sender name).");
    return 0;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:version result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  p = xmlnode_get_tag_data(ansqry, "name");
  if (p) {
    buf = g_strdup_printf("Name:    %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "version");
  if (p) {
    buf = g_strdup_printf("Version: %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "os");
  if (p) {
    buf = g_strdup_printf("OS:      %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  return 0;
}

void request_version(const char *fulljid)
{
  eviqs *iqn;

  iqn = iqs_new(JPACKET__GET, NS_VERSION, "version", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", fulljid);
  iqn->callback = &iqscallback_version;
  jab_send(jc, iqn->xmldata);
}

static int iqscallback_time(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return -1;

  ansqry = xmlnode_get_tag(xml_result, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:time result!");
    return 0;
  }
  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:time result (no sender name).");
    return 0;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:time result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  p = xmlnode_get_tag_data(ansqry, "utc");
  if (p) {
    buf = g_strdup_printf("UTC:  %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "tz");
  if (p) {
    buf = g_strdup_printf("TZ:   %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  p = xmlnode_get_tag_data(ansqry, "display");
  if (p) {
    buf = g_strdup_printf("Time: %s", p);
    scr_WriteIncomingMessage(bjid, buf,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
    g_free(buf);
  }
  return 0;
}

void request_time(const char *fulljid)
{
  eviqs *iqn;

  iqn = iqs_new(JPACKET__GET, NS_TIME, "time", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", fulljid);
  iqn->callback = &iqscallback_time;
  jab_send(jc, iqn->xmldata);
}

static int iqscallback_last(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return -1;

  ansqry = xmlnode_get_tag(xml_result, "query");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:last result!");
    return 0;
  }
  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:last result (no sender name).");
    return 0;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:last result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // bjid should now really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  p = xmlnode_get_attrib(ansqry, "seconds");
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
  p = xmlnode_get_data(ansqry);
  if (p) {
    buf = g_strdup_printf("Status message: %s", p);
    scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
    g_free(buf);
  }
  return 0;
}

void request_last(const char *fulljid)
{
  eviqs *iqn;

  iqn = iqs_new(JPACKET__GET, NS_LAST, "last", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", fulljid);
  iqn->callback = &iqscallback_last;
  jab_send(jc, iqn->xmldata);
}

static void display_vcard_item(const char *bjid, const char *label,
                               enum vcard_attr vcard_attrib, const char *text)
{
  char *buf;

  if (!text || !bjid || !label)
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

static void handle_vcard_node(const char *barejid, xmlnode vcardnode)
{
  xmlnode x;
  const char *p;

  x = xmlnode_get_firstchild(vcardnode);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *data;
    enum vcard_attr vcard_attrib = 0;

    p = xmlnode_get_name(x);
    data = xmlnode_get_data(x);
    if (!p || !data)
      continue;

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
      data = xmlnode_get_tag_data(x, "FAMILY");
      display_vcard_item(barejid, "Family Name", vcard_attrib, data);
      data = xmlnode_get_tag_data(x, "GIVEN");
      display_vcard_item(barejid, "Given Name", vcard_attrib, data);
      data = xmlnode_get_tag_data(x, "MIDDLE");
      display_vcard_item(barejid, "Middle Name", vcard_attrib, data);
    } else if (!strcmp(p, "ORG")) {
      data = xmlnode_get_tag_data(x, "ORGNAME");
      display_vcard_item(barejid, "Organisation name", vcard_attrib, data);
      data = xmlnode_get_tag_data(x, "ORGUNIT");
      display_vcard_item(barejid, "Organisation unit", vcard_attrib, data);
    } else {
      // The HOME, WORK and PREF attributes are common to the remaining fields
      // (ADR, TEL & EMAIL)
      if (xmlnode_get_tag(x, "HOME"))
        vcard_attrib |= vcard_home;
      if (xmlnode_get_tag(x, "WORK"))
        vcard_attrib |= vcard_work;
      if (xmlnode_get_tag(x, "PREF"))
        vcard_attrib |= vcard_pref;
      if (!strcmp(p, "ADR")) {          // Address
        if (xmlnode_get_tag(x, "POSTAL"))
          vcard_attrib |= vcard_postal;
        data = xmlnode_get_tag_data(x, "EXTADD");
        display_vcard_item(barejid, "Addr (ext)", vcard_attrib, data);
        data = xmlnode_get_tag_data(x, "STREET");
        display_vcard_item(barejid, "Street", vcard_attrib, data);
        data = xmlnode_get_tag_data(x, "LOCALITY");
        display_vcard_item(barejid, "Locality", vcard_attrib, data);
        data = xmlnode_get_tag_data(x, "REGION");
        display_vcard_item(barejid, "Region", vcard_attrib, data);
        data = xmlnode_get_tag_data(x, "PCODE");
        display_vcard_item(barejid, "Postal code", vcard_attrib, data);
        data = xmlnode_get_tag_data(x, "CTRY");
        display_vcard_item(barejid, "Country", vcard_attrib, data);
      } else if (!strcmp(p, "TEL")) {   // Telephone
        data = xmlnode_get_tag_data(x, "NUMBER");
        if (data) {
          if (xmlnode_get_tag(x, "VOICE"))
            vcard_attrib |= vcard_voice;
          if (xmlnode_get_tag(x, "FAX"))
            vcard_attrib |= vcard_fax;
          if (xmlnode_get_tag(x, "CELL"))
            vcard_attrib |= vcard_cell;
          display_vcard_item(barejid, "Phone", vcard_attrib, data);
        }
      } else if (!strcmp(p, "EMAIL")) { // Email
        if (xmlnode_get_tag(x, "INTERNET"))
          vcard_attrib |= vcard_inet;
        data = xmlnode_get_tag_data(x, "USERID");
        display_vcard_item(barejid, "Email", vcard_attrib, data);
      }
    }
  }
}

static int iqscallback_vcard(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  xmlnode ansqry;
  char *p;
  char *bjid;
  char *buf;

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return -1;

  // Display IQ result sender...
  p = xmlnode_get_attrib(xml_result, "from");
  if (!p) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid IQ:vCard result (no sender name).");
    return 0;
  }
  bjid = p;

  buf = g_strdup_printf("Received IQ:vCard result from <%s>", bjid);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);

  // Get the vCard node
  ansqry = xmlnode_get_tag(xml_result, "vCard");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOGNORM, "Empty IQ:vCard result!");
    g_free(buf);
    return 0;
  }

  // bjid should really be the "bare JID", let's strip the resource
  p = strchr(bjid, JID_RESOURCE_SEPARATOR);
  if (p) *p = '\0';

  scr_WriteIncomingMessage(bjid, buf, 0, HBB_PREFIX_INFO, 0);
  g_free(buf);

  // Get result data...
  handle_vcard_node(bjid, ansqry);
  return 0;
}

void request_vcard(const char *bjid)
{
  eviqs *iqn;

  // Create a new IQ structure.  We use NULL for the namespace because
  // we'll have to use a special tag, not the usual "query" one.
  iqn = iqs_new(JPACKET__GET, NULL, "vcard", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", bjid);
  // Remove the useless <query/> tag, and insert a vCard one.
  xmlnode_hide(xmlnode_get_tag(iqn->xmldata, "query"));
  xmlnode_put_attrib(xmlnode_insert_tag(iqn->xmldata, "vCard"),
                     "xmlns", NS_VCARD);
  iqn->callback = &iqscallback_vcard;
  jab_send(jc, iqn->xmldata);
}

static void storage_bookmarks_parse_conference(xmlnode xmldata)
{
  const char *fjid, *name, *autojoin;
  const char *pstatus, *awhois;
  char *bjid;
  GSList *room_elt;

  fjid = xmlnode_get_attrib(xmldata, "jid");
  if (!fjid)
    return;
  name = xmlnode_get_attrib(xmldata, "name");
  autojoin = xmlnode_get_attrib(xmldata, "autojoin");
  awhois = xmlnode_get_attrib(xmldata, "autowhois");
  pstatus = xmlnode_get_tag_data(xmldata, "print_status");

  bjid = jidtodisp(fjid); // Bare jid

  // Make sure this is a room (it can be a conversion user->room)
  room_elt = roster_find(bjid, jidsearch, 0);
  if (!room_elt) {
    room_elt = roster_add_user(bjid, name, NULL, ROSTER_TYPE_ROOM,
                               sub_none, -1);
  } else {
    buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);
    /*
    // If the name is available, should we use it?
    // I don't think so, it would be confusing because this item is already
    // in the roster.
    if (name)
      buddy_setname(room_elt->data, name);
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
    if (!strcmp(awhois, "1"))
      i = autowhois_on;
    else if (!strcmp(awhois, "0"))
      i = autowhois_off;
    if (i != autowhois_default)
      buddy_setautowhois(room_elt->data, i);
  }

  // Is autojoin set?
  // If it is, we'll look up for more information (nick? password?) and
  // try to join the room.
  if (autojoin && !strcmp(autojoin, "1")) {
    char *nick, *passwd;
    char *tmpnick = NULL;
    nick = xmlnode_get_tag_data(xmldata, "nick");
    passwd = xmlnode_get_tag_data(xmldata, "password");
    if (!nick || !*nick)
      nick = tmpnick = default_muc_nickname(NULL);
    // Let's join now
    scr_LogPrint(LPRINT_LOGNORM, "Auto-join bookmark <%s>", bjid);
    jb_room_join(bjid, nick, passwd);
    g_free(tmpnick);
  }
  g_free(bjid);
}

static int iqscallback_storage_bookmarks(eviqs *iqp, xmlnode xml_result,
                                         guint iqcontext)
{
  xmlnode x, ansqry;
  char *p;

  if (iqcontext == IQS_CONTEXT_ERROR) {
    // No server support, or no bookmarks?
    p = xmlnode_get_name(xmlnode_get_firstchild(xml_result));
    if (p && !strcmp(p, "item-not-found")) {
      // item-no-found means the server has Private Storage, but it's
      // currently empty.
      xmlnode_free(bookmarks);
      bookmarks = xmlnode_new_tag("storage");
      xmlnode_put_attrib(bookmarks, "xmlns", "storage:bookmarks");
      // We return 0 so that the IQ error message be
      // not displayed, as it isn't a real error.
      return 0;
    }
    return -1; // Unhandled error
  }

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return 0;

  ansqry = xmlnode_get_tag(xml_result, "query");
  ansqry = xmlnode_get_tag(ansqry, "storage");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOG, "Invalid IQ:private result! (storage:bookmarks)");
    return 0;
  }

  // Walk through the storage tags
  x = xmlnode_get_firstchild(ansqry);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    p = xmlnode_get_name(x);
    // If the current node is a conference item, parse it and update the roster
    if (p && !strcmp(p, "conference"))
      storage_bookmarks_parse_conference(x);
  }
  // Copy the bookmarks node
  xmlnode_free(bookmarks);
  bookmarks = xmlnode_dup(ansqry);
  return 0;
}

static void request_storage_bookmarks(void)
{
  eviqs *iqn;
  xmlnode x;

  iqn = iqs_new(JPACKET__GET, NS_PRIVATE, "storage", IQS_DEFAULT_TIMEOUT);

  x = xmlnode_insert_tag(xmlnode_get_tag(iqn->xmldata, "query"), "storage");
  xmlnode_put_attrib(x, "xmlns", "storage:bookmarks");

  iqn->callback = &iqscallback_storage_bookmarks;
  jab_send(jc, iqn->xmldata);
}

static int iqscallback_storage_rosternotes(eviqs *iqp, xmlnode xml_result,
                                           guint iqcontext)
{
  xmlnode ansqry;

  if (iqcontext == IQS_CONTEXT_ERROR) {
    const char *p;
    // No server support, or no roster notes?
    p = xmlnode_get_name(xmlnode_get_firstchild(xml_result));
    if (p && !strcmp(p, "item-not-found")) {
      // item-no-found means the server has Private Storage, but it's
      // currently empty.
      xmlnode_free(rosternotes);
      rosternotes = xmlnode_new_tag("storage");
      xmlnode_put_attrib(rosternotes, "xmlns", "storage:rosternotes");
      // We return 0 so that the IQ error message be
      // not displayed, as it isn't a real error.
      return 0;
    }
    return -1; // Unhandled error
  }

  // Leave now if we cannot process xml_result
  if (!xml_result || iqcontext) return 0;

  ansqry = xmlnode_get_tag(xml_result, "query");
  ansqry = xmlnode_get_tag(ansqry, "storage");
  if (!ansqry) {
    scr_LogPrint(LPRINT_LOG, "Invalid IQ:private result! "
                 "(storage:rosternotes)");
    return 0;
  }
  // Copy the rosternotes node
  xmlnode_free(rosternotes);
  rosternotes = xmlnode_dup(ansqry);
  return 0;
}

static void request_storage_rosternotes(void)
{
  eviqs *iqn;
  xmlnode x;

  iqn = iqs_new(JPACKET__GET, NS_PRIVATE, "storage", IQS_DEFAULT_TIMEOUT);

  x = xmlnode_insert_tag(xmlnode_get_tag(iqn->xmldata, "query"), "storage");
  xmlnode_put_attrib(x, "xmlns", "storage:rosternotes");

  iqn->callback = &iqscallback_storage_rosternotes;
  jab_send(jc, iqn->xmldata);
}

int iqscallback_auth(eviqs *iqp, xmlnode xml_result, guint iqcontext)
{
  if (iqcontext == IQS_CONTEXT_ERROR)
    return -1;

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
    request_storage_bookmarks();
    request_storage_rosternotes();
    jstate = STATE_LOGGED;
  }
  return 0;
}

static void handle_iq_result(jconn conn, char *from, xmlnode xmldata)
{
  char *id = xmlnode_get_attrib(xmldata, "id");

  if (!id) {
    scr_LogPrint(LPRINT_LOG, "IQ result stanza with no ID, ignored.");
    return;
  }

  (void)iqs_callback(id, xmldata, IQS_CONTEXT_RESULT);
}

// FIXME  highly duplicated code
static void send_iq_not_implemented(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x, y, z;
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

// FIXME  highly duplicated code
static void send_iq_not_available(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x, y, z;
  // Not available.
  x = xmlnode_dup(xmldata);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_hide_attrib(x, "from");

  xmlnode_put_attrib(x, "type", TMSG_ERROR);
  y = xmlnode_insert_tag(x, TMSG_ERROR);
  xmlnode_put_attrib(y, "code", "503");
  xmlnode_put_attrib(y, "type", "cancel");
  z = xmlnode_insert_tag(y, "service-unavailable");
  xmlnode_put_attrib(z, "xmlns", NS_XMPP_STANZAS);

  jab_send(conn, x);
  xmlnode_free(x);
}

/*
static void send_iq_commands_bad_action(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x, y, z;

  x = xmlnode_dup(xmldata);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_hide_attrib(x, "from");

  xmlnode_put_attrib(x, "type", TMSG_ERROR);
  y = xmlnode_insert_tag(x, TMSG_ERROR);
  xmlnode_put_attrib(y, "code", "400");
  xmlnode_put_attrib(y, "type", "modify");
  z = xmlnode_insert_tag(y, "bad-request");
  xmlnode_put_attrib(z, "xmlns", NS_XMPP_STANZAS);
  z = xmlnode_insert_tag(y, "bad-action");
  xmlnode_put_attrib(z, "xmlns", NS_COMMANDS);

  jab_send(conn, x);
  xmlnode_free(x);
}
*/

static void send_iq_forbidden(jconn conn, char *from, xmlnode xmldata)
{
  xmlnode x, y, z;

  x = xmlnode_dup(xmldata);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_hide_attrib(x, "from");

  xmlnode_put_attrib(x, "type", TMSG_ERROR);
  y = xmlnode_insert_tag(x, TMSG_ERROR);
  xmlnode_put_attrib(y, "code", "403");
  xmlnode_put_attrib(y, "type", "cancel");
  z = xmlnode_insert_tag(y, "forbidden");
  xmlnode_put_attrib(z, "xmlns", NS_XMPP_STANZAS);

  jab_send(conn, x);
  xmlnode_free(x);
}

static void send_iq_commands_malformed_action(jconn conn, char *from,
                                              xmlnode xmldata)
{
  xmlnode x, y, z;

  x = xmlnode_dup(xmldata);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_hide_attrib(x, "from");

  xmlnode_put_attrib(x, "type", TMSG_ERROR);
  y = xmlnode_insert_tag(x, TMSG_ERROR);
  xmlnode_put_attrib(y, "code", "400");
  xmlnode_put_attrib(y, "type", "modify");
  z = xmlnode_insert_tag(y, "bad-request");
  xmlnode_put_attrib(z, "xmlns", NS_XMPP_STANZAS);
  z = xmlnode_insert_tag(y, "malformed-action");
  xmlnode_put_attrib(z, "xmlns", NS_COMMANDS);

  jab_send(conn, x);
  xmlnode_free(x);
}

static void handle_iq_commands_list(jconn conn, char *from, const char *id,
                                    xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  jid requester_jid;
  const struct adhoc_command *command;
  const char *node;
  bool from_self;

  x = jutil_iqnew(JPACKET__RESULT, NS_DISCO_ITEMS);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "query");

  node = xmlnode_get_attrib(xmlnode_get_tag(xmldata, "query"), "node");
  if (node)
    xmlnode_put_attrib(myquery, "node", node);

  requester_jid = jid_new(conn->p, xmlnode_get_attrib(xmldata, "from"));
  from_self = !jid_cmpx(conn->user, requester_jid, JID_USER | JID_SERVER);

  for (command = adhoc_command_list ; command->name ; command++) {
    if (!command->only_for_self || from_self) {
      xmlnode item;
      item = xmlnode_insert_tag(myquery, "item");
      xmlnode_put_attrib(item, "node", command->name);
      xmlnode_put_attrib(item, "name", command->description);
      xmlnode_put_attrib(item, "jid", jid_full(conn->user));
    }
  }

  jab_send(jc, x);
  xmlnode_free(x);
}

static void xmlnode_insert_dataform_result_message(xmlnode node, char *message)
{
  xmlnode x, field, value;

  x = xmlnode_insert_tag(node, "x");
  xmlnode_put_attrib(x, "type", "result");
  xmlnode_put_attrib(x, "xmlns", "jabber:x:data");

  field = xmlnode_insert_tag(x, "field");
  xmlnode_put_attrib(field, "type", "text-single");
  xmlnode_put_attrib(field, "var", "message");

  value = xmlnode_insert_tag(field, "value");
  xmlnode_insert_cdata(value, message, -1);
}

static char *generate_session_id(char *prefix)
{
  char *result;
  static int counter = 0;
  counter++;
  // TODO better use timestamp?
  result = g_strdup_printf("%s-%i", prefix, counter);
  return result;
}

static void handle_iq_command_set_status(jconn conn, char *from, const char *id,
                                          xmlnode xmldata)
{
  char *action, *node, *sessionid;
  xmlnode iq, command, x, y;
  const struct adhoc_status *s;

  x = xmlnode_get_tag(xmldata, "command");
  action = xmlnode_get_attrib(x, "action");
  node = xmlnode_get_attrib(x, "node");
  sessionid = xmlnode_get_attrib(x, "sessionid");

  iq = xmlnode_new_tag("iq");
  command = xmlnode_insert_tag(iq, "command");
  xmlnode_put_attrib(command, "node", node);
  xmlnode_put_attrib(command, "xmlns", NS_COMMANDS);

  if (!sessionid) {
    xmlnode value;

    sessionid = generate_session_id("set-status");
    xmlnode_put_attrib(command, "sessionid", sessionid);
    g_free(sessionid);
    sessionid = NULL;
    xmlnode_put_attrib(command, "status", "executing");

    x = xmlnode_insert_tag(command, "x");
    xmlnode_put_attrib(x, "type", "form");
    xmlnode_put_attrib(x, "xmlns", "jabber:x:data");

    y = xmlnode_insert_tag(x, "title");
    xmlnode_insert_cdata(y, "Change Status", -1);

    y = xmlnode_insert_tag(x, "instructions");
    xmlnode_insert_cdata(y, "Choose the status and status message", -1);

    // TODO see if factorisation is possible
    // (with xmlnode_insert_dataform_result_message)
    y = xmlnode_insert_tag(x, "field");
    xmlnode_put_attrib(y, "type", "hidden");
    xmlnode_put_attrib(y, "var", "FORM_TYPE");

    value = xmlnode_insert_tag(y, "value");
    xmlnode_insert_cdata(value, "http://jabber.org/protocol/rc", -1);

    y = xmlnode_insert_tag(x, "field");
    xmlnode_put_attrib(y, "type", "list-single");
    xmlnode_put_attrib(y, "var", "status");
    xmlnode_put_attrib(y, "label", "Status");
    xmlnode_insert_tag(y, "required");

    value = xmlnode_insert_tag(y, "value");
    // TODO current status
    xmlnode_insert_cdata(value, "online", -1);
    for (s = adhoc_status_list; s->name; s++) {
        xmlnode option = xmlnode_insert_tag(y, "option");
        value = xmlnode_insert_tag(option, "value");
        xmlnode_insert_cdata(value, s->name, -1);
        xmlnode_put_attrib(option, "label", s->description);
    }
    // TODO add priority ?
    // I do not think this is useful, user should not have to care of the
    // priority like gossip and gajim do (misc)
    y = xmlnode_insert_tag(x, "field");
    xmlnode_put_attrib(y, "type", "text-single");
    xmlnode_put_attrib(y, "var", "status-message");
    xmlnode_put_attrib(y, "label", "Message");
  } else if (action && !strcmp(action, "cancel")) {
    xmlnode_put_attrib(command, "status", "canceled");
  } else  { // (if sessionid and not canceled)
    y = xmlnode_get_tag(x, "x?xmlns=jabber:x:data");
    if (y) {
      char *value, *message;
      value = xmlnode_get_tag_data(xmlnode_get_tag(y, "field?var=status"),
                                   "value");
      message = xmlnode_get_tag_data(xmlnode_get_tag(y,
                                   "field?var=status-message"), "value");
      if (value) {
        for (s = adhoc_status_list; s->name && strcmp(s->name, value); s++);
        if (s->name) {
          char *status = g_strdup_printf("%s %s", s->status,
                                         message ? message : "");
          cmd_setstatus(NULL, status);
          g_free(status);
          xmlnode_put_attrib(command, "status", "completed");
          xmlnode_put_attrib(iq, "type", "result");
          xmlnode_insert_dataform_result_message(command,
                                                 "Status has been changed");
        }
      }
    }
  }
  if (sessionid)
    xmlnode_put_attrib(command, "sessionid", sessionid);
  xmlnode_put_attrib(iq, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_put_attrib(iq, "id", id);
  jab_send(jc, iq);
  xmlnode_free(iq);
}

static void _callback_foreach_buddy_groupchat(gpointer rosterdata, void *param)
{
  xmlnode value, option;
  xmlnode *field;
  const char *room_jid, *nickname;
  char *desc;

  room_jid = buddy_getjid(rosterdata);
  if (!room_jid) return;
  nickname = buddy_getnickname(rosterdata);
  if (!nickname) return;
  field = param;

  option = xmlnode_insert_tag(*field, "option");
  value = xmlnode_insert_tag(option, "value");
  xmlnode_insert_cdata(value, room_jid, -1);
  desc = g_strdup_printf("%s on %s", nickname, room_jid);
  xmlnode_put_attrib(option, "label", desc);
  g_free(desc);
}

static void handle_iq_command_leave_groupchats(jconn conn, char *from,
                                               const char *id, xmlnode xmldata)
{
  char *action, *node, *sessionid;
  xmlnode iq, command, x;

  x = xmlnode_get_tag(xmldata, "command");
  action = xmlnode_get_attrib(x, "action");
  node = xmlnode_get_attrib(x, "node");
  sessionid = xmlnode_get_attrib(x, "sessionid");

  iq = xmlnode_new_tag("iq");
  command = xmlnode_insert_tag(iq, "command");
  xmlnode_put_attrib(command, "node", node);
  xmlnode_put_attrib(command, "xmlns", NS_COMMANDS);

  if (!sessionid) {
    xmlnode title, instructions, field, value;

    sessionid = generate_session_id("leave-groupchats");
    xmlnode_put_attrib(command, "sessionid", sessionid);
    g_free(sessionid);
    sessionid = NULL;
    xmlnode_put_attrib(command, "status", "executing");

    x = xmlnode_insert_tag(command, "x");
    xmlnode_put_attrib(x, "type", "form");
    xmlnode_put_attrib(x, "xmlns", "jabber:x:data");

    title = xmlnode_insert_tag(x, "title");
    xmlnode_insert_cdata(title, "Leave groupchat(s)", -1);

    instructions = xmlnode_insert_tag(x, "instructions");
    xmlnode_insert_cdata(instructions, "What groupchats do you want to leave?",
                         -1);

    field = xmlnode_insert_tag(x, "field");
    xmlnode_put_attrib(field, "type", "hidden");
    xmlnode_put_attrib(field, "var", "FORM_TYPE");

    value = xmlnode_insert_tag(field, "value");
    xmlnode_insert_cdata(value, "http://jabber.org/protocol/rc", -1);

    field = xmlnode_insert_tag(x, "field");
    xmlnode_put_attrib(field, "type", "list-multi");
    xmlnode_put_attrib(field, "var", "groupchats");
    xmlnode_put_attrib(field, "label", "Groupchats: ");
    xmlnode_insert_tag(field, "required");

    foreach_buddy(ROSTER_TYPE_ROOM, &_callback_foreach_buddy_groupchat, &field);
  } else if (action && !strcmp(action, "cancel")) {
    xmlnode_put_attrib(command, "status", "canceled");
  } else  { // (if sessionid and not canceled)
    xmlnode form = xmlnode_get_tag(x, "x?xmlns=jabber:x:data");
    if (form) {
      xmlnode x, gc;

      xmlnode_put_attrib(command, "status", "completed");
      gc = xmlnode_get_tag(form, "field?var=groupchats");

      for (x = xmlnode_get_firstchild(gc) ; x ; x = xmlnode_get_nextsibling(x))
      {
        char* to_leave = xmlnode_get_tag_data(x, "value");
        if (to_leave) {
          GList* b = buddy_search_jid(to_leave);
          if (b)
            cmd_room_leave(b->data, "Requested by remote command");
        }
      }
      xmlnode_put_attrib(iq, "type", "result");
      xmlnode_insert_dataform_result_message(command,
                                             "Groupchats have been left");
    }
  }
  if (sessionid)
    xmlnode_put_attrib(command, "sessionid", sessionid);
  xmlnode_put_attrib(iq, "to", xmlnode_get_attrib(xmldata, "from"));
  xmlnode_put_attrib(iq, "id", id);
  jab_send(jc, iq);
  xmlnode_free(iq);
}

static void handle_iq_commands(jconn conn, char *from, const char *id,
                               xmlnode xmldata)
{
  jid requester_jid;
  xmlnode x;
  const struct adhoc_command *command;

  requester_jid = jid_new(conn->p, xmlnode_get_attrib(xmldata, "from"));
  x = xmlnode_get_tag(xmldata, "command");
  if (!jid_cmpx(conn->user, requester_jid, JID_USER | JID_SERVER) ) {
    char *action, *node;
    action = xmlnode_get_attrib(x, "action");
    node = xmlnode_get_attrib(x, "node");
    // action can be NULL, in which case it seems to take the default,
    // ie execute
    if (!action || !strcmp(action, "execute") || !strcmp(action, "cancel")
        || !strcmp(action, "next") || !strcmp(action, "complete")) {
      for (command = adhoc_command_list; command->name; command++) {
        if (!strcmp(node, command->name))
          command->callback(conn, from, id, xmldata);
      }
      // "prev" action will get there, as we do not implement it,
      // and do not authorize it
    } else {
      send_iq_commands_malformed_action(conn, from, xmldata);
    }
  } else {
    send_iq_forbidden(conn, from, xmldata);
  }
}

static void handle_iq_disco_items(jconn conn, char *from, const char *id,
                                  xmlnode xmldata)
{
  xmlnode x;
  const char *node;
  x = xmlnode_get_tag(xmldata, "query");
  node = xmlnode_get_attrib(x, "node");
  if (node) {
    if (!strcmp(node, NS_COMMANDS)) {
      handle_iq_commands_list(conn, from, id, xmldata);
    } else {
      send_iq_not_implemented(conn, from, xmldata);
    }
  } else {
    // not sure about this one
    send_iq_not_implemented(conn, from, xmldata);
  }
}

//  disco_info_set_ext(ansquery, ext)
// Add features attributes to ansquery for extension ext.
static void disco_info_set_ext(xmlnode ansquery, const char *ext)
{
  char *nodename;
  nodename = g_strdup_printf("%s#%s", MCABBER_CAPS_NODE, ext);
  xmlnode_put_attrib(ansquery, "node", nodename);
  g_free(nodename);
  if (!strcasecmp(ext, "csn")) {
    // I guess it's ok to send this even if it's not compiled in.
    xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                       "var", NS_CHATSTATES);
  }
  if (!strcasecmp(ext, "iql")) {
    // I guess it's ok to send this even if it's not compiled in.
    xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                       "var", NS_LAST);
  }
}

//  disco_info_set_default(ansquery, entitycaps)
// Add features attributes to ansquery.  If entitycaps is TRUE, assume
// that we're answering an Entity Caps request (if not, the request was
// a basic discovery query).
// Please change the entity version string if you modify mcabber disco
// source code, so that it doesn't conflict with the upstream client.
static void disco_info_set_default(xmlnode ansquery, guint entitycaps)
{
  xmlnode y;
  char *eversion;

  eversion = g_strdup_printf("%s#%s", MCABBER_CAPS_NODE, entity_version());
  xmlnode_put_attrib(ansquery, "node", eversion);
  g_free(eversion);

  y = xmlnode_insert_tag(ansquery, "identity");
  xmlnode_put_attrib(y, "category", "client");
  xmlnode_put_attrib(y, "type", "pc");
  xmlnode_put_attrib(y, "name", PACKAGE_NAME);

  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_DISCO_INFO);
  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_MUC);
#ifdef JEP0085
  // Advertise ChatStates only if we're not using Entity Capabilities
  if (!entitycaps)
    xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                       "var", NS_CHATSTATES);
#endif
  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_TIME);
  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_XMPP_TIME);
  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_VERSION);
  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_PING);
  xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                     "var", NS_COMMANDS);
  if (!entitycaps)
    xmlnode_put_attrib(xmlnode_insert_tag(ansquery, "feature"),
                       "var", NS_LAST);
}

static void handle_iq_disco_info(jconn conn, char *from, const char *id,
                                 xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *node;

  x = jutil_iqnew(JPACKET__RESULT, NS_DISCO_INFO);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "query");

  node = xmlnode_get_attrib(xmlnode_get_tag(xmldata, "query"), "node");
  if (node && startswith(node, MCABBER_CAPS_NODE "#", FALSE)) {
    const char *param = node+strlen(MCABBER_CAPS_NODE)+1;
    if (!strcmp(param, entity_version()))
      disco_info_set_default(myquery, TRUE);  // client#version
    else
      disco_info_set_ext(myquery, param);     // client#extension
  } else {
    // Basic discovery request
    disco_info_set_default(myquery, FALSE);
  }

  jab_send(jc, x);
  xmlnode_free(x);
}

double seconds_since_last_use(void)
{
  return difftime(time(NULL), iqlast);
}

static void handle_iq_last(jconn conn, char *from, const char *id,
                           xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *seconds;

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ last time request from <%s>",
                 from);
  }

  x = jutil_iqnew(JPACKET__RESULT, NS_LAST);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "query");
  seconds = g_strdup_printf("%.0f", seconds_since_last_use());
  xmlnode_put_attrib(myquery, "seconds", seconds);
  g_free(seconds);

  jab_send(jc, x);
  xmlnode_free(x);
}

static void handle_iq_ping(jconn conn, char *from, const char *id,
                           xmlnode xmldata)
{
  xmlnode x;
  x = jutil_iqresult(xmldata);
  jab_send(jc, x);
}

static void handle_iq_version(jconn conn, char *from, const char *id,
                              xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *os = NULL;
  char *ver = mcabber_version();

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ version request from <%s>",
                 from);
  }

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

// This function borrows some code from the Pidgin project
static void handle_iq_time(jconn conn, char *from, const char *id,
                           xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *buf, *utf8_buf;
  time_t now_t;
  struct tm *now;

  time(&now_t);

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ time request from <%s>", from);
  }

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

// This function borrows some code from the Pidgin project
static void handle_iq_time202(jconn conn, char *from, const char *id,
                              xmlnode xmldata)
{
  xmlnode x;
  xmlnode myquery;
  char *buf, *utf8_buf;
  time_t now_t;
  struct tm *now;
  char const *sign;
  int diff = 0;

  time(&now_t);

  if (!settings_opt_get_int("iq_hide_requests")) {
    scr_LogPrint(LPRINT_LOGNORM, "Received an IQ time request from <%s>", from);
  }

  buf = g_new0(char, 512);

  x = jutil_iqnew(JPACKET__RESULT, NULL);
  xmlnode_hide(xmlnode_get_tag(x, "query"));
  xmlnode_put_attrib(xmlnode_insert_tag(x, "time"), "xmlns", NS_XMPP_TIME);
  xmlnode_put_attrib(x, "id", id);
  xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
  myquery = xmlnode_get_tag(x, "time");

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
    xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "tzo"), utf8_buf, -1);
    g_free(utf8_buf);
  }

  now = gmtime(&now_t);

  strftime(buf, 512, "%Y-%m-%dT%TZ", now);
  xmlnode_insert_cdata(xmlnode_insert_tag(myquery, "utc"), buf, -1);

  jab_send(jc, x);
  xmlnode_free(x);
  g_free(buf);
}

// This function borrows some code from the Pidgin project
static void handle_iq_get(jconn conn, char *from, xmlnode xmldata)
{
  const char *id, *ns;
  xmlnode x;
  guint iq_not_implemented = FALSE;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id) {
    scr_LogPrint(LPRINT_LOG, "IQ get stanza with no ID, ignored.");
    return;
  }

  x = xmlnode_get_tag(xmldata, "ping");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_PING)) {
    handle_iq_ping(conn, from, id, xmldata);
    return;
  }

  x = xmlnode_get_tag(xmldata, "time");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_XMPP_TIME)) {
    handle_iq_time202(conn, from, id, xmldata);
    return;
  }

  x = xmlnode_get_tag(xmldata, "query");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_DISCO_INFO)) {
    handle_iq_disco_info(conn, from, id, xmldata);
  } else if (ns && !strcmp(ns, NS_DISCO_ITEMS)) {
    handle_iq_disco_items(conn, from, id, xmldata);
  } else if (ns && !strcmp(ns, NS_VERSION)) {
    handle_iq_version(conn, from, id, xmldata);
  } else if (ns && !strcmp(ns, NS_LAST)) {
    if (!settings_opt_get_int("iq_last_disable") &&
        (!settings_opt_get_int("iq_last_disable_when_notavail") ||
         jb_getstatus() != notavail))
      handle_iq_last(conn, from, id, xmldata);
    else
      send_iq_not_available(conn, from, xmldata);
  } else if (ns && !strcmp(ns, NS_TIME)) {
    handle_iq_time(conn, from, id, xmldata);
  } else {
    iq_not_implemented = TRUE;
  }

  if (!iq_not_implemented)
    return;

  send_iq_not_implemented(conn, from, xmldata);
}

static void handle_iq_set(jconn conn, char *from, xmlnode xmldata)
{
  const char *id, *ns;
  xmlnode x;
  guint iq_not_implemented = FALSE;

  id = xmlnode_get_attrib(xmldata, "id");
  if (!id)
    scr_LogPrint(LPRINT_LOG, "IQ set stanza with no ID...");

  x = xmlnode_get_tag(xmldata, "query");
  ns = xmlnode_get_attrib(x, "xmlns");
  if (ns && !strcmp(ns, NS_ROSTER)) {
    handle_iq_roster(x);
  } else {
    x = xmlnode_get_tag(xmldata, "command");
    ns = xmlnode_get_attrib(x, "xmlns");
    if (ns && !strcmp(ns, NS_COMMANDS)) {
      handle_iq_commands(conn, from, id, xmldata);
      return;
    } else {
      iq_not_implemented = TRUE;
    }
  }

  if (!id) return;

  if (!iq_not_implemented) {
    x = xmlnode_new_tag("iq");
    xmlnode_put_attrib(x, "to", xmlnode_get_attrib(xmldata, "from"));
    xmlnode_put_attrib(x, "type", "result");
    xmlnode_put_attrib(x, "id", id);
    jab_send(conn, x);
    xmlnode_free(x);
  } else {
    send_iq_not_implemented(conn, from, xmldata);
  }
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
    // Display a message only if the error isn't caught by a callback.
    xmlnode x = xmlnode_get_tag(xmldata, TMSG_ERROR);
    if (iqs_callback(xmlnode_get_attrib(xmldata, "id"), x, IQS_CONTEXT_ERROR))
      display_server_error(x);
  }
}

//  send_storage_bookmarks()
// Send the current bookmarks node to update the server.
// Note: the sender should check we're online.
void send_storage_bookmarks(void)
{
  eviqs *iqn;

  if (!bookmarks) return;

  iqn = iqs_new(JPACKET__SET, NS_PRIVATE, "storage", IQS_DEFAULT_TIMEOUT);
  xmlnode_insert_node(xmlnode_get_tag(iqn->xmldata, "query"), bookmarks);

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
}

//  send_storage_rosternotes()
// Send the current rosternotes node to update the server.
// Note: the sender should check we're online.
void send_storage_rosternotes(void)
{
  eviqs *iqn;

  if (!rosternotes) return;

  iqn = iqs_new(JPACKET__SET, NS_PRIVATE, "storage", IQS_DEFAULT_TIMEOUT);
  xmlnode_insert_node(xmlnode_get_tag(iqn->xmldata, "query"), rosternotes);

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
