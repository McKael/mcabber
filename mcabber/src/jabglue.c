/*
 * jabglue.c    -- Jabber protocol handling
 *
 * Copyright (C) 2005 Mikael Berthe <bmikael@lists.lilotux.net>
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

#define _GNU_SOURCE  /* We need glibc for strptime */
#include "../libjabber/jabber.h"
#include "jabglue.h"
#include "roster.h"
#include "screen.h"
#include "hooks.h"
#include "utils.h"
#include "settings.h"
#include "hbuf.h"
#include "histolog.h"

#define JABBERPORT      5222
#define JABBERSSLPORT   5223

#define JABBER_AGENT_GROUP "Jabber Agents"

jconn jc;
static time_t LastPingTime;
static unsigned int KeepaliveDelay;
static unsigned int prio;
static int s_id;
static int regmode, regdone;
static enum imstatus mystatus = offline;
static gchar *mystatusmsg;
static unsigned char online;

char imstatus2char[imstatus_size+1] = {
    '_', 'o', 'i', 'f', 'd', 'n', 'a', '\0'
};

static enum {
  STATE_CONNECTING,
  STATE_GETAUTH,
  STATE_SENDAUTH,
  STATE_LOGGED
} jstate;


void statehandler(jconn, int);
void packethandler(jconn, jpacket);

static void logger(jconn j, int io, const char *buf)
{
  scr_LogPrint(LPRINT_DEBUG, "%03s: %s", ((io == 0) ? "OUT" : "IN"), buf);
}

/*
static void jidsplit(const char *jid, char **user, char **host,
        char **res)
{
  char *tmp, *ptr;
  tmp = strdup(jid);

  if ((ptr = strchr(tmp, '/')) != NULL) {
    *res = strdup(ptr+1);
    *ptr = 0;
  } else
    *res = NULL;

  if ((ptr = strchr(tmp, '@')) != NULL) {
    *host = strdup(ptr+1);
    *ptr = 0;
  } else
    *host = NULL;

  *user = strdup(tmp);
  free(tmp);
}
*/

//  jidtodisp(jid)
// Strips the resource part from the jid
// The caller should g_free the result after use.
char *jidtodisp(const char *jid)
{
  char *ptr;
  char *alias;

  alias = g_strdup(jid);

  if ((ptr = strchr(alias, '/')) != NULL) {
    *ptr = 0;
  }
  return alias;
}

char *compose_jid(const char *username, const char *servername,
        const char *resource)
{
  char *jid = g_new(char, 3 +
                    strlen(username) + strlen(servername) + strlen(resource));
  strcpy(jid, username);
  if (!strchr(jid, '@')) {
    strcat(jid, "@");
    strcat(jid, servername);
  }
  strcat(jid, "/");
  strcat(jid, resource);
  return jid;
}

inline unsigned char jb_getonline(void)
{
  return online;
}

jconn jb_connect(const char *jid, const char *server, unsigned int port,
                 int ssl, const char *pass)
{
  char *utf8_jid;
  if (!port) {
    if (ssl)
      port = JABBERSSLPORT;
    else
      port = JABBERPORT;
  }

  jb_disconnect();

  utf8_jid = to_utf8(jid);
  if (!utf8_jid) return jc;

  s_id = 1;
  jc = jab_new(utf8_jid, (char*)pass, (char*)server, port, ssl);
  g_free(utf8_jid);

  /* These 3 functions can deal with a NULL jc, no worry... */
  jab_logger(jc, logger);
  jab_packet_handler(jc, &packethandler);
  jab_state_handler(jc, &statehandler);

  if (jc && jc->user) {
    online = TRUE;
    jstate = STATE_CONNECTING;
    statehandler(0, -1);
    jab_start(jc);
  }

  return jc;
}

void jb_disconnect(void)
{
  if (!jc) return;

  // announce it to  everyone else
  jb_setstatus(offline, NULL, "");

  // announce it to the user
  statehandler(jc, JCONN_STATE_OFF);

  jab_delete(jc);
  //free(jc); XXX
  jc = NULL;
}

inline void jb_reset_keepalive()
{
  time(&LastPingTime);
}

void jb_send_raw(const char *str)
{
  if (jc && online && str)
    jab_send_raw(jc, str);
}

void jb_keepalive()
{
  if (jc && online)
    jab_send_raw(jc, "  \t  ");
  jb_reset_keepalive();
}

void jb_set_keepalive_delay(unsigned int delay)
{
  KeepaliveDelay = delay;
}

inline void jb_set_priority(unsigned int priority)
{
  prio = priority;
}

void jb_main()
{
  xmlnode x, z;
  char *cid;

  if (!online) {
    safe_usleep(10000);
    return;
  }

  if (jc && jc->state == JCONN_STATE_CONNECTING) {
    safe_usleep(75000);
    jab_start(jc);
    return;
  }

  jab_poll(jc, 50);

  if (jstate == STATE_CONNECTING) {
    if (jc) {
      x = jutil_iqnew(JPACKET__GET, NS_AUTH);
      cid = jab_getid(jc);
      xmlnode_put_attrib(x, "id", cid);
      // id = atoi(cid);

      z = xmlnode_insert_tag(xmlnode_get_tag(x, "query"), "username");
      xmlnode_insert_cdata(z, jc->user->user, (unsigned) -1);
      jab_send(jc, x);
      xmlnode_free(x);

      jstate = STATE_GETAUTH;
    }

    if (!jc || jc->state == JCONN_STATE_OFF) {
      scr_LogPrint(LPRINT_LOGNORM, "Unable to connect to the server");
      online = FALSE;
    }
  }

  if (!jc) {
    statehandler(jc, JCONN_STATE_OFF);
  } else if (jc->state == JCONN_STATE_OFF || jc->fd == -1) {
    statehandler(jc, JCONN_STATE_OFF);
  }

  // Keepalive
  if (KeepaliveDelay) {
    time_t now;
    time(&now);
    if (now > LastPingTime + KeepaliveDelay)
      jb_keepalive();
  }
}

inline enum imstatus jb_getstatus()
{
  return mystatus;
}

inline const char *jb_getstatusmsg()
{
  return mystatusmsg;
}

void jb_setstatus(enum imstatus st, const char *recipient, const char *msg)
{
  xmlnode x;
  gchar *utf8_msg;

  if (!online) return;

  x = jutil_presnew(JPACKET__UNKNOWN, 0, 0);

  if (recipient)
    xmlnode_put_attrib(x, "to", recipient);

  switch(st) {
    case away:
        xmlnode_insert_cdata(xmlnode_insert_tag(x, "show"), "away",
                (unsigned) -1);
        break;

    case dontdisturb:
        xmlnode_insert_cdata(xmlnode_insert_tag(x, "show"), "dnd",
                (unsigned) -1);
        break;

    case freeforchat:
        xmlnode_insert_cdata(xmlnode_insert_tag(x, "show"), "chat",
                (unsigned) -1);
        break;

    case notavail:
        xmlnode_insert_cdata(xmlnode_insert_tag(x, "show"), "xa",
                (unsigned) -1);
        break;

    case invisible:
        xmlnode_put_attrib(x, "type", "invisible");
        break;

    case offline:
        xmlnode_put_attrib(x, "type", "unavailable");
        break;

    default:
        break;
  }

  if (prio) {
    char strprio[8];
    snprintf(strprio, 8, "%u", prio);
    xmlnode_insert_cdata(xmlnode_insert_tag(x, "priority"),
            strprio, (unsigned) -1);
  }

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

  utf8_msg = to_utf8(msg);
  xmlnode_insert_cdata(xmlnode_insert_tag(x, "status"), utf8_msg,
          (unsigned) -1);

  jab_send(jc, x);
  g_free(utf8_msg);
  xmlnode_free(x);

  // If we didn't change our _global_ status, we are done
  if (recipient) return;

  // Buddy per buddy invisibility handling
  //sendvisibility();

  // We'll need to update the roster if we switch to/from offline because
  // we don't know the presences of buddies when offline...
  if (mystatus == offline || st == offline)
    update_roster = TRUE;

  hk_mystatuschange(0, mystatus, st, msg);
  mystatus = st;
  if (msg != mystatusmsg) {
    if (mystatusmsg)
      g_free(mystatusmsg);
    if (*msg)
      mystatusmsg = g_strdup(msg);
    else
      mystatusmsg = NULL;
  }
}

void jb_send_msg(const char *jid, const char *text, int type,
                 const char *subject)
{
  xmlnode x;
  gchar *strtype;
  gchar *buffer = to_utf8(text);

  if (!online) return;

  if (type == ROSTER_TYPE_ROOM)
    strtype = TMSG_GROUPCHAT;
  else
    strtype = TMSG_CHAT;

  x = jutil_msgnew(strtype, (char*)jid, NULL, (char*)buffer);
  if (subject) {
    xmlnode y;
    char *bs = to_utf8(subject);
    y = xmlnode_insert_tag(x, "subject");
    xmlnode_insert_cdata(y, bs, (unsigned) -1);
    if (bs) g_free(bs);
  }
  jab_send(jc, x);
  xmlnode_free(x);
  g_free(buffer);
  jb_reset_keepalive();
}

// Note: the caller should check the jid is correct
void jb_addbuddy(const char *jid, const char *name, const char *group)
{
  xmlnode x, y, z;
  char *cleanjid;

  if (!online) return;

  // We don't check if the jabber user already exists in the roster,
  // because it allows to re-ask for notification.

  //x = jutil_presnew(JPACKET__SUBSCRIBE, jid, NULL);
  x = jutil_presnew(JPACKET__SUBSCRIBE, (char*)jid, "online");
  jab_send(jc, x);
  xmlnode_free(x);

  x = jutil_iqnew(JPACKET__SET, NS_ROSTER);
  y = xmlnode_get_tag(x, "query");
  z = xmlnode_insert_tag(y, "item");
  xmlnode_put_attrib(z, "jid", jid);

  if (name) {
    gchar *name_utf8 = to_utf8(name);
    z = xmlnode_insert_tag(z, "name");
    xmlnode_insert_cdata(z, name_utf8, (unsigned) -1);
    g_free(name_utf8);
  }

  if (group) {
    char *group_utf8 = to_utf8(group);
    z = xmlnode_insert_tag(z, "group");
    xmlnode_insert_cdata(z, group_utf8, (unsigned) -1);
    g_free(group_utf8);
  }

  jab_send(jc, x);
  xmlnode_free(x);

  cleanjid = jidtodisp(jid);
  roster_add_user(cleanjid, name, group, ROSTER_TYPE_USER);
  g_free(cleanjid);
  buddylist_build();

  update_roster = TRUE;
}

void jb_delbuddy(const char *jid)
{
  xmlnode x, y, z;
  char *cleanjid;

  if (!online) return;

  cleanjid = jidtodisp(jid);

  // If the current buddy is an agent, unsubscribe from it
  if (roster_gettype(cleanjid) == ROSTER_TYPE_AGENT) {
    scr_LogPrint(LPRINT_LOGNORM, "Unregistering from the %s agent", cleanjid);

    x = jutil_iqnew(JPACKET__SET, NS_REGISTER);
    xmlnode_put_attrib(x, "to", cleanjid);
    y = xmlnode_get_tag(x, "query");
    xmlnode_insert_tag(y, "remove");
    jab_send(jc, x);
    xmlnode_free(x);
  }

  // Unsubscribe this buddy from our presence notification
  x = jutil_presnew(JPACKET__UNSUBSCRIBE, cleanjid, 0);
  jab_send(jc, x);
  xmlnode_free(x);

  // Ask for removal from roster
  x = jutil_iqnew(JPACKET__SET, NS_ROSTER);
  y = xmlnode_get_tag(x, "query");
  z = xmlnode_insert_tag(y, "item");
  xmlnode_put_attrib(z, "jid", cleanjid);
  xmlnode_put_attrib(z, "subscription", "remove");
  jab_send(jc, x);
  xmlnode_free(x);

  roster_del_user(cleanjid);
  g_free(cleanjid);
  buddylist_build();

  update_roster = TRUE;
}

void jb_updatebuddy(const char *jid, const char *name, const char *group)
{
  xmlnode x, y;
  char *cleanjid;
  gchar *name_utf8;

  if (!online) return;

  // XXX We should check name's and group's correctness

  cleanjid = jidtodisp(jid);
  name_utf8 = to_utf8(name);

  x = jutil_iqnew(JPACKET__SET, NS_ROSTER);
  y = xmlnode_insert_tag(xmlnode_get_tag(x, "query"), "item");
  xmlnode_put_attrib(y, "jid", cleanjid);
  xmlnode_put_attrib(y, "name", name_utf8);

  if (group) {
    gchar *group_utf8 = to_utf8(group);
    y = xmlnode_insert_tag(y, "group");
    xmlnode_insert_cdata(y, group_utf8, (unsigned) -1);
    g_free(group_utf8);
  }

  jab_send(jc, x);
  xmlnode_free(x);
  g_free(name_utf8);
  g_free(cleanjid);
}

// Join a MUC room
void jb_room_join(const char *room, const char *nickname)
{
  xmlnode x, y;
  gchar *roomid, *utf8_nickname;

  if (!online || !room) return;
  if (!nickname)        return;

  utf8_nickname = to_utf8(nickname);
  roomid = g_strdup_printf("%s/%s", room, utf8_nickname);
  g_free(utf8_nickname);
  if (check_jid_syntax(roomid)) {
    scr_LogPrint(LPRINT_NORMAL, "<%s/%s> is not a valid Jabber room", room,
                 nickname);
    g_free(roomid);
    return;
  }

  // Send the XML request
  x = jutil_presnew(JPACKET__UNKNOWN, 0, 0);
  xmlnode_put_attrib(x, "from", jid_full(jc->user));
  xmlnode_put_attrib(x, "to", roomid);
  y = xmlnode_insert_tag(x, "x");
  xmlnode_put_attrib(y, "xmlns", "http://jabber.org/protocol/muc");

  jab_send(jc, x);
  xmlnode_free(x);
  jb_reset_keepalive();
  g_free(roomid);
}

// Unlock a MUC room
// room syntax: "room@server"
void jb_room_unlock(const char *room)
{
  xmlnode x, y, z;

  if (!online || !room) return;

  x = jutil_iqnew(JPACKET__SET, "http://jabber.org/protocol/muc#owner");
  xmlnode_put_attrib(x, "id", "unlock1"); // XXX
  xmlnode_put_attrib(x, "to", room);
  y = xmlnode_get_tag(x, "query");
  z = xmlnode_insert_tag(y, "x");
  xmlnode_put_attrib(z, "xmlns", "jabber:x:data");
  xmlnode_put_attrib(z, "type", "submit");

  jab_send(jc, x);
  xmlnode_free(x);
  jb_reset_keepalive();
}


// Invite a user to a MUC room
// room syntax: "room@server"
// reason can be null.
void jb_room_invite(const char *room, const char *jid, const char *reason)
{
  xmlnode x, y, z;
  gchar *utf8_reason;

  if (!online || !room || !jid) return;

  if (!reason) reason = "";

  x = jutil_msgnew(NULL, (char*)room, NULL, NULL);

  y = xmlnode_insert_tag(x, "x");
  xmlnode_put_attrib(y, "xmlns", "http://jabber.org/protocol/muc#user");

  z = xmlnode_insert_tag(y, "invite");
  xmlnode_put_attrib(z, "to", jid);

  utf8_reason = to_utf8(reason);
  y = xmlnode_insert_tag(z, "reason");
  xmlnode_insert_cdata(y, utf8_reason, (unsigned) -1);
  g_free(utf8_reason);

  jab_send(jc, x);
  xmlnode_free(x);
  jb_reset_keepalive();
}

void postlogin()
{
  //int i;

  //flogged = TRUE;
  //ourstatus = available;

  //setautostatus(jhook.manualstatus);

  jb_setstatus(available, NULL, NULL);
  buddylist_build();
  /*
  for (i = 0; i < clist.count; i++) {
    c = (icqcontact *) clist.at(i);

    if (c->getdesc().pname == proto)
      if (ischannel(c))
        if (c->getbasicinfo().requiresauth)
          c->setstatus(available);
  }
  */

  /*
  agents.insert(agents.begin(), agent("vcard", "Jabber VCard", "", agent::atStandard));
  agents.begin()->params[agent::ptRegister].enabled = TRUE;

  string buf;
  ifstream f(conf.getconfigfname("jabber-infoset").c_str());

  if (f.is_open()) {
    icqcontact *c = clist.get(contactroot);

    c->clear();
    icqcontact::basicinfo bi = c->getbasicinfo();
    icqcontact::reginfo ri = c->getreginfo();

    ri.service = agents.begin()->name;
    getstring(f, buf); c->setnick(buf);
    getstring(f, buf); bi.email = buf;
    getstring(f, buf); bi.fname = buf;
    getstring(f, buf); bi.lname = buf;
    f.close();

    c->setbasicinfo(bi);
    c->setreginfo(ri);

    sendupdateuserinfo(*c);
    unlink(conf.getconfigfname("jabber-infoset").c_str());
  }
  */
}

void gotloggedin(void)
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

void gotroster(xmlnode x)
{
  xmlnode y, z;

  for (y = xmlnode_get_tag(x, "item"); y; y = xmlnode_get_nextsibling(y)) {
    const char *alias = xmlnode_get_attrib(y, "jid");
    //const char *sub = xmlnode_get_attrib(y, "subscription"); // TODO Not used
    const char *name = xmlnode_get_attrib(y, "name");
    char *group = NULL;

    z = xmlnode_get_tag(y, "group");
    if (z) group = xmlnode_get_data(z);

    if (alias) {
      char *buddyname;
      char *cleanalias = jidtodisp(alias);
      gchar *name_noutf8 = NULL;
      gchar *group_noutf8 = NULL;

      buddyname = cleanalias;
      if (name) {
        name_noutf8 = from_utf8(name);
        if (name_noutf8) buddyname = name_noutf8;
      }

      if (group)
        group_noutf8 = from_utf8(group);

      roster_add_user(cleanalias, buddyname, group_noutf8, ROSTER_TYPE_USER);
      if (name_noutf8)  g_free(name_noutf8);
      if (group_noutf8) g_free(group_noutf8);
      g_free(cleanalias);
    }
  }

  postlogin();
}

void gotmessage(char *type, const char *from, const char *body,
                const char *enc, time_t timestamp)
{
  char *jid;
  const char *rname;
  gchar *buffer = from_utf8(body);

  jid = jidtodisp(from);

  if (!buffer && body) {
    scr_LogPrint(LPRINT_LOGNORM, "Decoding of message from <%s> has failed",
                 from);
    scr_WriteIncomingMessage(jid, "Cannot display message: "
                             "UTF-8 conversion failure",
                             0, HBB_PREFIX_ERR | HBB_PREFIX_IN);
    g_free(jid);
    return;
  }

  rname = strchr(from, '/');
  if (rname) rname++;
  hk_message_in(jid, rname, timestamp, buffer, type);
  g_free(jid);
  g_free(buffer);
}

const char *defaulterrormsg(int code)
{
  const char *desc;

  switch(code) {
    case 401: desc = "Unauthorized";
              break;
    case 302: desc = "Redirect";
              break;
    case 400: desc = "Bad request";
              break;
    case 402: desc = "Payment Required";
              break;
    case 403: desc = "Forbidden";
              break;
    case 404: desc = "Not Found";
              break;
    case 405: desc = "Not Allowed";
              break;
    case 406: desc = "Not Acceptable";
              break;
    case 407: desc = "Registration Required";
              break;
    case 408: desc = "Request Timeout";
              break;
    case 409: desc = "Conflict";
              break;
    case 500: desc = "Internal Server Error";
              break;
    case 501: desc = "Not Implemented";
              break;
    case 502: desc = "Remote Server Error";
              break;
    case 503: desc = "Service Unavailable";
              break;
    case 504: desc = "Remote Server Timeout";
              break;
    default:
              desc = NULL;
  }

  return desc;
}

//  display_server_error(x)
// Display the error to the user
// x: error tag xmlnode pointer
void display_server_error(xmlnode x)
{
  const char *desc = NULL;
  int code = 0;
  char *s;
  const char *p;

  /* RFC3920:
   *    The <error/> element:
   *       o  MUST contain a child element corresponding to one of the defined
   *          stanza error conditions specified below; this element MUST be
   *          qualified by the 'urn:ietf:params:xml:ns:xmpp-stanzas' namespace.
   */
  p = xmlnode_get_name(xmlnode_get_firstchild(x));
  if (p)
    scr_LogPrint(LPRINT_LOGNORM, "Received error packet [%s]", p);

  // For backward compatibility
  if ((s = xmlnode_get_attrib(x, "code")) != NULL) {
    code = atoi(s);
    // Default message
    desc = defaulterrormsg(code);
  }

  // Error tag data is better, if available
  s = xmlnode_get_data(x);
  if (s && *s) desc = s;

  // And sometimes there is a text message
  s = xmlnode_get_tag_data(x, "text");
  if (s && *s) desc = s; // FIXME utf8??

  scr_LogPrint(LPRINT_LOGNORM, "Error code from server: %d %s", code, desc);
}

void statehandler(jconn conn, int state)
{
  static int previous_state = -1;

  scr_LogPrint(LPRINT_DEBUG, "StateHandler called (state=%d).", state);

  switch(state) {
    case JCONN_STATE_OFF:
        if (previous_state != JCONN_STATE_OFF)
          scr_LogPrint(LPRINT_LOGNORM, "[Jabber] Not connected to the server");

        online = FALSE;
        mystatus = offline;
        if (mystatusmsg) {
          g_free(mystatusmsg);
          mystatusmsg = NULL;
        }
        roster_free();
        update_roster = TRUE;
        break;

    case JCONN_STATE_CONNECTED:
        scr_LogPrint(LPRINT_LOGNORM, "[Jabber] Connected to the server");
        break;

    case JCONN_STATE_AUTH:
        scr_LogPrint(LPRINT_LOGNORM, "[Jabber] Authenticating to the server");
        break;

    case JCONN_STATE_ON:
        scr_LogPrint(LPRINT_LOGNORM, "[Jabber] Communication with the server "
                     "established");
        online = TRUE;
        break;

    case JCONN_STATE_CONNECTING:
        if (previous_state != state)
          scr_LogPrint(LPRINT_LOGNORM, "[Jabber] Connecting to the server");
        break;

    default:
        break;
  }
  previous_state = state;
}

void packethandler(jconn conn, jpacket packet)
{
  char *p, *r, *s;
  const char *m, *rname;
  xmlnode x, y;
  char *from=NULL, *type=NULL, *body=NULL, *enc=NULL;
  char *ns=NULL;
  char *id=NULL;
  enum imstatus ust;
  char bpprio;

  jb_reset_keepalive(); // reset keepalive timeout
  jpacket_reset(packet);

  p = xmlnode_get_attrib(packet->x, "type"); if (p) type = p;
  p = xmlnode_get_attrib(packet->x, "from");
  if (p) {   // Convert from UTF8
    // We need to be careful because from_utf8() can fail on some chars
    // Thus we only convert the resource part
    from = g_new0(char, strlen(p)+1);
    strcpy(from, p);
    r = strchr(from, '/');
    m = strchr(p, '/');
    if (m++) {
      s = from_utf8(m);
      if (s) {
        // The length should be enough because from_utf should only
        // reduce the string length
        strcpy(r+1, s);
        g_free(s);
      } else {
        *(r+1) = 0;
        scr_LogPrint(LPRINT_LOGNORM, "Decoding of message sender has failed");
      }
    }
  }

  if (!from && packet->type != JPACKET_IQ) {
    scr_LogPrint(LPRINT_LOGNORM, "Error in packet (could be UTF8-related)");
    return;
  }

  switch (packet->type) {
    case JPACKET_MESSAGE:
        {
          char *tmp = NULL;
          time_t timestamp = 0;

          body = xmlnode_get_tag_data(packet->x, "body");

          p = xmlnode_get_tag_data(packet->x, "subject");
          if (p != NULL) {
            if (type && !strcmp(type, TMSG_GROUPCHAT)) {  // Room topic
              gchar *mbuf;
              gchar *subj_noutf8 = from_utf8(p);
              // Get the room (s) and the nickname (r)
              s = g_strdup(from);
              r = strchr(s, '/');
              if (r) *r++ = 0;
              else   r = s;
              // Display inside the room window
              mbuf = g_strdup_printf("%s has set the topic to: %s", r,
                                     (subj_noutf8 ? subj_noutf8 : "(?)"));
              scr_WriteIncomingMessage(s, mbuf, 0,
                                       HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG);
              if (settings_opt_get_int("log_muc_conf"))
                hlog_write_message(s, 0, FALSE, mbuf);
              if (subj_noutf8) g_free(subj_noutf8);
              g_free(s);
              g_free(mbuf);
            } else {                                      // Chat message
              tmp = g_new(char, (body ? strlen(body) : 0) + strlen(p) + 4);
              *tmp = '[';
              strcpy(tmp+1, p);
              strcat(tmp, "]\n");
              if (body) strcat(tmp, body);
              body = tmp;
            }
          }

          /* there can be multiple <x> tags. we're looking for one with
             xmlns = jabber:x:encrypted */

          x = xmlnode_get_firstchild(packet->x);
          for ( ; x; x = xmlnode_get_nextsibling(x)) {
            if ((p = xmlnode_get_name(x)) && !strcmp(p, "x"))
              if ((p = xmlnode_get_attrib(x, "xmlns")) &&
                      !strcasecmp(p, "jabber:x:encrypted"))
                if ((p = xmlnode_get_data(x)) != NULL) {
                  enc = p;
                  break;
                }
          }

          // Timestamp?
          if ((x = xmlnode_get_tag(packet->x, "x")) != NULL) {
            if ((p = xmlnode_get_attrib(x, "stamp")) != NULL)
              timestamp = from_iso8601(p, 1);
          }

          if (type && !strcmp(type, TMSG_ERROR)) {
            if ((x = xmlnode_get_tag(packet->x, TMSG_ERROR)) != NULL)
              display_server_error(x);
          }
          if (from && body)
            gotmessage(type, from, body, enc, timestamp);
          if (tmp)
            g_free(tmp);
        }
        break;

    case JPACKET_IQ:
        if (!strcmp(type, "result")) {

          if ((p = xmlnode_get_attrib(packet->x, "id")) != NULL) {
            int iid = atoi(p);

            scr_LogPrint(LPRINT_DEBUG, "iid = %d", iid);
            if (iid == s_id) {
              if (!regmode) {
                if (jstate == STATE_GETAUTH) {
                  if ((x = xmlnode_get_tag(packet->x, "query")) != NULL)
                    if (!xmlnode_get_tag(x, "digest")) {
                      jc->sid = 0;
                    }

                  s_id = atoi(jab_auth(jc));
                  jstate = STATE_SENDAUTH;
                } else {
                  gotloggedin();
                  jstate = STATE_LOGGED;
                }
              } else {
                regdone = TRUE;
              }
              return;
            }

            if (!strcmp(p, "VCARDreq")) {
              x = xmlnode_get_firstchild(packet->x);
              if (!x) x = packet->x;

              //jhook.gotvcard(ic, x); TODO
              scr_LogPrint(LPRINT_LOGNORM, "Got VCARD");
              return;
            } else if (!strcmp(p, "versionreq")) {
              // jhook.gotversion(ic, packet->x); TODO
              scr_LogPrint(LPRINT_LOGNORM, "Got version");
              return;
            }
          }

          if ((x = xmlnode_get_tag(packet->x, "query")) != NULL) {
            p = xmlnode_get_attrib(x, "xmlns"); if (p) ns = p;

            if (!strcmp(ns, NS_ROSTER)) {
              gotroster(x);
            } else if (!strcmp(ns, NS_AGENTS)) {
              for (y = xmlnode_get_tag(x, "agent"); y; y = xmlnode_get_nextsibling(y)) {
                const char *alias = xmlnode_get_attrib(y, "jid");

                if (alias) {
                  const char *name = xmlnode_get_tag_data(y, "name");
                  const char *desc = xmlnode_get_tag_data(y, "description");
                  // TODO
                  // const char *service = xmlnode_get_tag_data(y, "service");
                  enum agtype atype = unknown;

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
                    scr_LogPrint(LPRINT_LOGNORM,
                                 "Agent: %s / %s / %s / type=%d",
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
              }

              /*
              if (find(jhook.agents.begin(), jhook.agents.end(), DEFAULT_CONFSERV) == jhook.agents.end())
                jhook.agents.insert(jhook.agents.begin(), agent(DEFAULT_CONFSERV, DEFAULT_CONFSERV,
                            _("Default Jabber conference server"), agent::atGroupchat));

              */
            } else if (!strcmp(ns, NS_SEARCH) || !strcmp(ns, NS_REGISTER)) {
              p = xmlnode_get_attrib(packet->x, "id"); id = p ? p : (char*)"";

              if (!strcmp(id, "Agent info")) {
                // jhook.gotagentinfo(packet->x); TODO
                scr_LogPrint(LPRINT_LOGNORM, "Got agent info");
              } else if (!strcmp(id, "Lookup")) {
                // jhook.gotsearchresults(packet->x); TODO
                scr_LogPrint(LPRINT_LOGNORM, "Got search results");
              } else if (!strcmp(id, "Register")) {
                x = jutil_iqnew(JPACKET__GET, NS_REGISTER);
                xmlnode_put_attrib(x, "to", from);
                xmlnode_put_attrib(x, "id", "Agent info");
                jab_send(conn, x);
                xmlnode_free(x);
              }

            }
          }
        } else if (!strcmp(type, "get")) {
          p = xmlnode_get_attrib(packet->x, "id");
          if (p) {
            xmlnode z;

            id = p;
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
        } else if (!strcmp(type, "set")) {
          /* FIXME: send error */
        } else if (!strcmp(type, TMSG_ERROR)) {
          if ((x = xmlnode_get_tag(packet->x, TMSG_ERROR)) != NULL)
            display_server_error(x);
        }
        break;

    case JPACKET_PRESENCE:
        r = jidtodisp(from);
        if (type && !strcmp(type, TMSG_ERROR)) {
          scr_LogPrint(LPRINT_LOGNORM, "Error presence packet from <%s>", r);
          if ((x = xmlnode_get_tag(packet->x, TMSG_ERROR)) != NULL)
            display_server_error(x);
          g_free(r);
          break;
        }

        p = xmlnode_get_tag_data(packet->x, "priority");
        if (p && *p) bpprio = (gchar)atoi(p);
        else         bpprio = 0;

        ust = available;
        p = xmlnode_get_tag_data(packet->x, "show");
        if (p) {
          if (!strcmp(p, "away"))      ust = away;
          else if (!strcmp(p, "dnd"))  ust = dontdisturb;
          else if (!strcmp(p, "xa"))   ust = notavail;
          else if (!strcmp(p, "chat")) ust = freeforchat;
        }

        if (type && !strcmp(type, "unavailable"))
          ust = offline;

        if ((x = xmlnode_get_tag(packet->x, "status")) != NULL)
          s = from_utf8(xmlnode_get_data(x));
        else
          s = NULL;

        // Call hk_statuschange() if status has changed or if the
        // status message is different
        rname = strchr(from, '/');
        if (rname) rname++;

        // Check for MUC presence packet
        // There can be multiple <x> tags!!
        x = xmlnode_get_firstchild(packet->x);
        for ( ; x; x = xmlnode_get_nextsibling(x)) {
          if ((p = xmlnode_get_name(x)) && !strcmp(p, "x"))
            if ((p = xmlnode_get_attrib(x, "xmlns")) &&
                !strcasecmp(p, "http://jabber.org/protocol/muc#user"))
              break;
        }
        if (x) {    // This is a MUC presence message
          enum imrole mbrole = role_none;
          const char *mbrjid = NULL;
          const char *mbnewnick = NULL;
          GSList *room_elt;
          int log_muc_conf = settings_opt_get_int("log_muc_conf");

          // Add room if it doesn't already exist
          room_elt = roster_find(r, jidsearch, 0);
          if (!room_elt)
            room_elt = roster_add_user(r, NULL, NULL, ROSTER_TYPE_ROOM);
          else // Make sure this is a room (it can be a conversion user->room)
            buddy_settype(room_elt->data, ROSTER_TYPE_ROOM);

          // Get room member's information
          y = xmlnode_get_tag(x, "item");
          if (y) {
            p = xmlnode_get_attrib(y, "role");
            if (p) {
              if (!strcmp(p, "moderator"))        mbrole = role_moderator;
              else if (!strcmp(p, "participant")) mbrole = role_participant;
              else if (!strcmp(p, "visitor"))     mbrole = role_visitor;
              else if (!strcmp(p, "none"))        mbrole = role_none;
              else scr_LogPrint(LPRINT_LOGNORM, "<%s>: Unknown role \"%s\"",
                                from, p);
            }
            p = xmlnode_get_attrib(y, "jid");
            if (p) mbrjid = p;
            p = xmlnode_get_attrib(y, "nick");
            if (p) mbnewnick = p;
          }

          // Check for nickname change
          y = xmlnode_get_tag(x, "status");
          if (y && mbnewnick) {
            p = xmlnode_get_attrib(y, "code");
            if (p && !strcmp(p, "303")) {
              gchar *mbuf;
              gchar *newname_noutf8 = from_utf8(mbnewnick);
              mbuf = g_strdup_printf("%s is now known as %s", rname,
                      (newname_noutf8 ? newname_noutf8 : "(?)"));
              scr_WriteIncomingMessage(r, mbuf, 0,
                                       HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG);
              if (log_muc_conf) hlog_write_message(r, 0, FALSE, mbuf);
              g_free(mbuf);
              if (newname_noutf8) {
                buddy_resource_setname(room_elt->data, rname, newname_noutf8);
                m = buddy_getnickname(room_elt->data);
                if (m && !strcmp(rname, m))
                  buddy_setnickname(room_elt->data, newname_noutf8);
                g_free(newname_noutf8);
              }
            }
          }

          // Check for departure/arrival
          if (!mbnewnick && mbrole == role_none) {
            gchar *mbuf;
            if (s)  mbuf = g_strdup_printf("%s has left: %s", rname, s);
            else    mbuf = g_strdup_printf("%s has left", rname);
            scr_WriteIncomingMessage(r, mbuf, 0,
                                     HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG);
            if (log_muc_conf) hlog_write_message(r, 0, FALSE, mbuf);
            g_free(mbuf);
          } else if (buddy_getstatus(room_elt->data, rname) == offline &&
                     ust != offline) {
            gchar *mbuf;
            if (buddy_getnickname(room_elt->data) == NULL) {
              buddy_setnickname(room_elt->data, rname);
              mbuf = g_strdup_printf("You have joined as \"%s\"", rname);
            } else {
              mbuf = g_strdup_printf("%s has joined", rname);
            }
            scr_WriteIncomingMessage(r, mbuf, 0,
                                     HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG);
            if (log_muc_conf) hlog_write_message(r, 0, FALSE, mbuf);
            g_free(mbuf);
          }

          // Update room member status
          if (rname)
            roster_setstatus(r, rname, bpprio, ust, s, mbrole, mbrjid);
          else
            scr_LogPrint(LPRINT_LOGNORM, "MUC DBG: no rname!"); /* DBG */

          g_free(r);
          if (s) g_free(s);

          buddylist_build();
          scr_DrawRoster();
          break;
        }

        // Not a MUC message, so this is a regular buddy...
        m = roster_getstatusmsg(r, rname);
        if ((ust != roster_getstatus(r, rname)) || (s && (!m || strcmp(s, m))))
          hk_statuschange(r, rname, bpprio, 0, ust, s);
        g_free(r);
        if (s) g_free(s);
        break;

    case JPACKET_S10N:
        scr_LogPrint(LPRINT_LOGNORM, "Received (un)subscription packet "
                     "(type=%s)", ((type) ? type : ""));

        if (!strcmp(type, "subscribe")) {
          int isagent;
          r = jidtodisp(from);
          isagent = (roster_gettype(r) & ROSTER_TYPE_AGENT) != 0;
          g_free(r);
          scr_LogPrint(LPRINT_LOGNORM, "isagent=%d", isagent); // XXX DBG
          if (!isagent) {
            scr_LogPrint(LPRINT_LOGNORM, "<%s> wants to subscribe "
                         "to your network presence updates", from);
            // FIXME we accept everybody...
            x = jutil_presnew(JPACKET__SUBSCRIBED, from, 0);
            jab_send(jc, x);
            xmlnode_free(x);
          } else {
            x = jutil_presnew(JPACKET__SUBSCRIBED, from, 0);
            jab_send(jc, x);
            xmlnode_free(x);
          }
        } else if (!strcmp(type, "unsubscribe")) {
          x = jutil_presnew(JPACKET__UNSUBSCRIBED, from, 0);
          jab_send(jc, x);
          xmlnode_free(x);
          scr_LogPrint(LPRINT_LOGNORM, "<%s> has unsubscribed to "
                       "your presence updates", from);
        }
        break;

    default:
        break;
  }
  g_free(from);
}

