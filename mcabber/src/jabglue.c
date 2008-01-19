/*
 * jabglue.c    -- Jabber protocol handling
 *
 * Copyright (C) 2005-2008 Mikael Berthe <mikael@lilotux.net>
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

#include "../libjabber/jabber.h"
#include "jabglue.h"
#include "jab_priv.h"
#include "roster.h"
#include "screen.h"
#include "hooks.h"
#include "utils.h"
#include "settings.h"
#include "hbuf.h"
#include "histolog.h"
#include "commands.h"
#include "pgp.h"
#include "otr.h"

#define JABBERPORT      5222
#define JABBERSSLPORT   5223

#define RECONNECTION_TIMEOUT    60L

jconn jc;
guint AutoConnection;
enum enum_jstate jstate;

char imstatus2char[imstatus_size+1] = {
    '_', 'o', 'f', 'd', 'n', 'a', 'i', '\0'
};

static char *imstatus_showmap[] = {
  "",
  "",
  "chat",
  "dnd",
  "xa",
  "away",
  ""
};

static time_t LastPingTime;
static unsigned int KeepaliveDelay;
static enum imstatus mystatus = offline;
static enum imstatus mywantedstatus = available;
static gchar *mystatusmsg;
static unsigned char online;

static void statehandler(jconn, int);
static void packethandler(jconn, jpacket);
static void handle_state_events(char* from, xmlnode xmldata);

static int evscallback_invitation(eviqs *evp, guint evcontext);


static void logger(jconn j, int io, const char *buf)
{
  scr_LogPrint(LPRINT_DEBUG, "%03s: %s", ((io == 0) ? "OUT" : "IN"), buf);
}

//  jidtodisp(jid)
// Strips the resource part from the jid
// The caller should g_free the result after use.
char *jidtodisp(const char *fjid)
{
  char *ptr;
  char *alias;

  alias = g_strdup(fjid);

  if ((ptr = strchr(alias, JID_RESOURCE_SEPARATOR)) != NULL) {
    *ptr = 0;
  }
  return alias;
}

char *compose_jid(const char *username, const char *servername,
                  const char *resource)
{
  char *fjid;

  if (!strchr(username, JID_DOMAIN_SEPARATOR)) {
    fjid = g_strdup_printf("%s%c%s%c%s", username,
                           JID_DOMAIN_SEPARATOR, servername,
                           JID_RESOURCE_SEPARATOR, resource);
  } else {
    fjid = g_strdup_printf("%s%c%s", username,
                           JID_RESOURCE_SEPARATOR, resource);
  }
  return fjid;
}

unsigned char jb_getonline(void)
{
  return online;
}

jconn jb_connect(const char *fjid, const char *server, unsigned int port,
                 int ssl, const char *pass)
{
  if (!port) {
    if (ssl)
      port = JABBERSSLPORT;
    else
      port = JABBERPORT;
  }

  jb_disconnect();

  if (!fjid) return jc;

  jc = jab_new((char*)fjid, (char*)pass, (char*)server, port, ssl);

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

  if (online) {
    // Launch pre-disconnect internal hook
    hook_execute_internal("hook-pre-disconnect");
    // Announce it to  everyone else
    jb_setstatus(offline, NULL, "", FALSE);
    // End the XML flow
    jb_send_raw("</stream:stream>");
    /*
    // Free status message
    g_free(mystatusmsg);
    mystatusmsg = NULL;
    */
  }

  // Announce it to the user
  statehandler(jc, JCONN_STATE_OFF);

  jab_delete(jc);
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

//  check_connection()
// Check if we've been disconnected for a while (predefined timeout),
// and if so try to reconnect.
static void check_connection(void)
{
  static time_t disconnection_timestamp = 0L;
  time_t now;

  // Maybe we're voluntarily offline...
  if (!AutoConnection)
    return;

  // Are we totally disconnected?
  if (jc && jc->state != JCONN_STATE_OFF) {
    disconnection_timestamp = 0L;
    return;
  }

  time(&now);
  if (!disconnection_timestamp) {
    disconnection_timestamp = now;
    return;
  }

  // If the reconnection_timeout is reached, try to reconnect.
  if (now > disconnection_timestamp + RECONNECTION_TIMEOUT) {
    mcabber_connect();
    disconnection_timestamp = 0L;
  }
}

void jb_main()
{
  time_t now;
  fd_set fds;
  long tmout;
  struct timeval tv;
  static time_t last_eviqs_check = 0L;

  if (!online) {
    safe_usleep(10000);
    check_connection();
    return;
  }

  if (jc && jc->state == JCONN_STATE_CONNECTING) {
    safe_usleep(75000);
    jab_start(jc);
    return;
  }

  FD_ZERO(&fds);
  FD_SET(0, &fds);
  FD_SET(jc->fd, &fds);

  tv.tv_sec = 60;
  tv.tv_usec = 0;

  time(&now);

  if (KeepaliveDelay) {
    if (now >= LastPingTime + (time_t)KeepaliveDelay)
      tv.tv_sec = 0;
    else
      tv.tv_sec = LastPingTime + (time_t)KeepaliveDelay - now;
  }

  // Check auto-away timeout
  tmout = scr_GetAutoAwayTimeout(now);
  if (tv.tv_sec > tmout)
    tv.tv_sec = tmout;

#if defined JEP0022 || defined JEP0085
  // Check composing timeout
  tmout = scr_GetChatStatesTimeout(now);
  if (tv.tv_sec > tmout)
    tv.tv_sec = tmout;
#endif

  if (!tv.tv_sec)
    tv.tv_usec = 350000;

  scr_DoUpdate();
  if (select(jc->fd + 1, &fds, NULL, NULL, &tv) > 0) {
    if (FD_ISSET(jc->fd, &fds))
      jab_poll(jc, 0);
  }

  if (jstate == STATE_CONNECTING) {
    if (jc) {
      eviqs *iqn;
      xmlnode z;

      iqn = iqs_new(JPACKET__GET, NS_AUTH, "auth", IQS_DEFAULT_TIMEOUT);
      iqn->callback = &iqscallback_auth;

      z = xmlnode_insert_tag(xmlnode_get_tag(iqn->xmldata, "query"),
                             "username");
      xmlnode_insert_cdata(z, jc->user->user, (unsigned) -1);
      jab_send(jc, iqn->xmldata);

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

  time(&now);

  // Check for EV & IQ requests timeouts
  if (now > last_eviqs_check + 20) {
    iqs_check_timeout(now);
    evs_check_timeout(now);
    last_eviqs_check = now;
  }

  // Keepalive
  if (KeepaliveDelay) {
    if (now > LastPingTime + (time_t)KeepaliveDelay)
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

inline void update_last_use(void)
{
  iqlast = time(NULL);
}

//  insert_entity_capabilities(presence_stanza)
// Entity Capabilities (XEP-0115)
static void insert_entity_capabilities(xmlnode x)
{
  xmlnode y;
  const char *ver = entity_version();
  char *exts, *exts2;

  exts = NULL;

  y = xmlnode_insert_tag(x, "c");
  xmlnode_put_attrib(y, "xmlns", NS_CAPS);
  xmlnode_put_attrib(y, "node", MCABBER_CAPS_NODE);
  xmlnode_put_attrib(y, "ver", ver);
#ifdef JEP0085
  if (!chatstates_disabled) {
    exts2 = g_strjoin(" ", "csn", exts, NULL);
    g_free(exts);
    exts = exts2;
  }
#endif
  if (!settings_opt_get_int("iq_last_disable")) {
    exts2 = g_strjoin(" ", "iql", exts, NULL);
    g_free(exts);
    exts = exts2;
  }
  if (exts) {
    xmlnode_put_attrib(y, "ext", exts);
    g_free(exts);
  }
}

static void roompresence(gpointer room, void *presencedata)
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
  jb_setstatus(pres->st, to, pres->msg, TRUE);
  g_free(to);
}

//  presnew(status, recipient, message)
// Create an xmlnode with default presence attributes
// Note: the caller must free the node after use
static xmlnode presnew(enum imstatus st, const char *recipient,
                       const char *msg)
{
  unsigned int prio;
  xmlnode x;

  x = jutil_presnew(JPACKET__UNKNOWN, 0, 0);

  if (recipient) {
    xmlnode_put_attrib(x, "to", recipient);
  }

  switch(st) {
    case away:
    case notavail:
    case dontdisturb:
    case freeforchat:
        xmlnode_insert_cdata(xmlnode_insert_tag(x, "show"),
                             imstatus_showmap[st], (unsigned) -1);
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

  prio = settings_opt_get_int("priority");
  if (prio) {
    char strprio[8];
    snprintf(strprio, 8, "%d", (int)prio);
    xmlnode_insert_cdata(xmlnode_insert_tag(x, "priority"),
                         strprio, (unsigned) -1);
  }

  if (msg)
    xmlnode_insert_cdata(xmlnode_insert_tag(x, "status"), msg, (unsigned) -1);

  return x;
}

void jb_setstatus(enum imstatus st, const char *recipient, const char *msg,
                  int do_not_sign)
{
  xmlnode x;

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
  if (online) {
    const char *s_msg = (st != invisible ? msg : NULL);
    x = presnew(st, recipient, s_msg);
    insert_entity_capabilities(x); // Entity Capabilities (XEP-0115)
#ifdef HAVE_GPGME
    if (!do_not_sign && gpg_enabled()) {
      char *signature;
      signature = gpg_sign(s_msg ? s_msg : "");
      if (signature) {
        xmlnode y;
        y = xmlnode_insert_tag(x, "x");
        xmlnode_put_attrib(y, "xmlns", NS_SIGNED);
        xmlnode_insert_cdata(y, signature, (unsigned) -1);
        g_free(signature);
      }
    }
#endif
    jab_send(jc, x);
    xmlnode_free(x);
  }

  // If we didn't change our _global_ status, we are done
  if (recipient) return;

  if (online) {
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

//  jb_setprevstatus()
// Set previous status.  This wrapper function is used after a disconnection.
inline void jb_setprevstatus(void)
{
  jb_setstatus(mywantedstatus, NULL, mystatusmsg, FALSE);
}

//  new_msgid()
// Generate a new id string.  The caller should free it.
// The caller must free the string when no longer needed.
static char *new_msgid(void)
{
  static guint msg_idn;
  time_t now;
  time(&now);
  if (!msg_idn)
    srand(now);
  msg_idn += 1U + (unsigned int) (9.0 * (rand() / (RAND_MAX + 1.0)));
  return g_strdup_printf("%u%d", msg_idn, (int)(now%10L));
}

//  jb_send_msg(jid, text, type, subject, msgid, *encrypted, type_overwrite)
// When encrypted is not NULL, the function set *encrypted to 1 if the
// message has been PGP-encrypted.  If encryption enforcement is set and
// encryption fails, *encrypted is set to -1.
void jb_send_msg(const char *fjid, const char *text, int type,
                 const char *subject, const char *msgid, gint *encrypted,
                 const char *type_overwrite)
{
  xmlnode x;
  const gchar *strtype;
#ifdef HAVE_LIBOTR
  int otr_msg = 0;
#endif
#if defined HAVE_GPGME || defined JEP0022 || defined JEP0085
  char *rname, *barejid;
  GSList *sl_buddy;
#endif
#if defined JEP0022 || defined JEP0085
  xmlnode event;
  guint use_jep85 = 0;
  struct jep0085 *jep85 = NULL;
#endif
#if defined JEP0022
  gchar *nmsgid = NULL;
#endif
  gchar *enc = NULL;

  if (encrypted)
    *encrypted = 0;

  if (!online) return;

  if (type_overwrite)
    strtype = type_overwrite;
  else {
    if (type == ROSTER_TYPE_ROOM)
      strtype = TMSG_GROUPCHAT;
    else
      strtype = TMSG_CHAT;
  }

#if defined HAVE_GPGME || defined HAVE_LIBOTR || defined JEP0022 || defined JEP0085
  rname = strchr(fjid, JID_RESOURCE_SEPARATOR);
  barejid = jidtodisp(fjid);
  sl_buddy = roster_find(barejid, jidsearch, ROSTER_TYPE_USER);

  // If we can get a resource name, we use it.  Else we use NULL,
  // which hopefully will give us the most likely resource.
  if (rname)
    rname++;

#ifdef HAVE_LIBOTR
  if (otr_enabled()) {
    if (msgid && strcmp(msgid, "otrinject") == 0)
      msgid = NULL;
    else if (type == ROSTER_TYPE_USER) {
      otr_msg = otr_send((char **)&text, barejid);
      if (!text) {
        g_free(barejid);
        if (encrypted)
          *encrypted = -1;
        return;
      }
    }
    if (otr_msg && encrypted) {
      *encrypted = 1;
    }
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

  x = jutil_msgnew((char*)strtype, (char*)fjid, NULL,
                   (enc ? "This message is PGP-encrypted." : (char*)text));
  if (subject) {
    xmlnode y;
    y = xmlnode_insert_tag(x, "subject");
    xmlnode_insert_cdata(y, subject, (unsigned) -1);
  }
  if (enc) {
    xmlnode y;
    y = xmlnode_insert_tag(x, "x");
    xmlnode_put_attrib(y, "xmlns", NS_ENCRYPTED);
    xmlnode_insert_cdata(y, enc, (unsigned) -1);
    if (encrypted)
      *encrypted = 1;
    g_free(enc);
  }

#if defined JEP0022 || defined JEP0085
  // If typing notifications are disabled, we can skip all this stuff...
  if (chatstates_disabled || type == ROSTER_TYPE_ROOM)
    goto jb_send_msg_no_chatstates;

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
    event = xmlnode_insert_tag(x, "active");
    xmlnode_put_attrib(event, "xmlns", NS_CHATSTATES);
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
    event = xmlnode_insert_tag(x, "x");
    xmlnode_put_attrib(event, "xmlns", NS_EVENT);
    xmlnode_insert_tag(event, "composing");

    if (sl_buddy)
      jep22 = buddy_resource_jep22(sl_buddy->data, rname);
    if (jep22)
      jep22->last_state_sent = ROSTER_EVENT_ACTIVE;

    // An id is mandatory when using JEP-0022.
    if (!msgid && (text || subject)) {
      msgid = nmsgid = new_msgid();
      // Let's update last_msgid_sent
      // (We do not update it when the msgid is provided by the caller,
      // because this is probably a special message...)
      if (jep22) {
        g_free(jep22->last_msgid_sent);
        jep22->last_msgid_sent = g_strdup(msgid);
      }
    }
  }
#endif

jb_send_msg_no_chatstates:
  xmlnode_put_attrib(x, "id", msgid);

  if (mystatus != invisible)
    update_last_use();
  jab_send(jc, x);
  xmlnode_free(x);
#if defined JEP0022
  g_free(nmsgid);
#endif

  jb_reset_keepalive();
}


#ifdef JEP0085
//  jb_send_jep85_chatstate()
// Send a JEP-85 chatstate.
static void jb_send_jep85_chatstate(const char *bjid, const char *resname,
                                    guint state)
{
  xmlnode x;
  xmlnode event;
  GSList *sl_buddy;
  const char *chattag;
  char *rjid, *fjid = NULL;
  struct jep0085 *jep85 = NULL;

  if (!online) return;

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
  x = jutil_msgnew(TMSG_CHAT, rjid, NULL, NULL);

  event = xmlnode_insert_tag(x, chattag);
  xmlnode_put_attrib(event, "xmlns", NS_CHATSTATES);

  jab_send(jc, x);
  xmlnode_free(x);

  g_free(fjid);
  jb_reset_keepalive();
}
#endif

#ifdef JEP0022
//  jb_send_jep22_event()
// Send a JEP-22 message event (delivered, composing...).
static void jb_send_jep22_event(const char *fjid, guint type)
{
  xmlnode x;
  xmlnode event;
  const char *msgid;
  char *rname, *barejid;
  GSList *sl_buddy;
  struct jep0022 *jep22 = NULL;
  guint jep22_state;

  if (!online) return;

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

  x = jutil_msgnew(TMSG_CHAT, (char*)fjid, NULL, NULL);

  event = xmlnode_insert_tag(x, "x");
  xmlnode_put_attrib(event, "xmlns", NS_EVENT);
  if (type == ROSTER_EVENT_DELIVERED)
    xmlnode_insert_tag(event, "delivered");
  else if (type == ROSTER_EVENT_COMPOSING)
    xmlnode_insert_tag(event, "composing");
  xmlnode_put_attrib(event, "id", msgid);

  jab_send(jc, x);
  xmlnode_free(x);

  jb_reset_keepalive();
}
#endif

//  jb_send_chatstate(buddy, state)
// Send a chatstate or event (JEP-22/85) according to the buddy's capabilities.
// The message is sent to one of the resources with the highest priority.
#if defined JEP0022 || defined JEP0085
void jb_send_chatstate(gpointer buddy, guint chatstate)
{
  const char *bjid;
#ifdef JEP0085
  GSList *resources, *p_res, *p_next;
  struct jep0085 *jep85 = NULL;;
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
        jb_send_jep85_chatstate(bjid, p_res->data, chatstate);
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
    jb_send_jep22_event(bjid, chatstate);
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

//  jb_subscr_send_auth(jid)
// Allow jid to receive our presence updates
void jb_subscr_send_auth(const char *bjid)
{
  xmlnode x;

  x = jutil_presnew(JPACKET__SUBSCRIBED, (char *)bjid, NULL);
  jab_send(jc, x);
  xmlnode_free(x);
}

//  jb_subscr_cancel_auth(jid)
// Cancel jid's subscription to our presence updates
void jb_subscr_cancel_auth(const char *bjid)
{
  xmlnode x;

  x = jutil_presnew(JPACKET__UNSUBSCRIBED, (char *)bjid, NULL);
  jab_send(jc, x);
  xmlnode_free(x);
}

//  jb_subscr_request_auth(jid)
// Request a subscription to jid's presence updates
void jb_subscr_request_auth(const char *bjid)
{
  xmlnode x;

  x = jutil_presnew(JPACKET__SUBSCRIBE, (char *)bjid, NULL);
  jab_send(jc, x);
  xmlnode_free(x);
}

//  jb_subscr_request_cancel(jid)
// Request to cancel jour subscription to jid's presence updates
void jb_subscr_request_cancel(const char *bjid)
{
  xmlnode x;

  x = jutil_presnew(JPACKET__UNSUBSCRIBE, (char *)bjid, NULL);
  jab_send(jc, x);
  xmlnode_free(x);
}

// Note: the caller should check the jid is correct
void jb_addbuddy(const char *bjid, const char *name, const char *group)
{
  xmlnode y, z;
  eviqs *iqn;
  char *cleanjid;

  if (!online) return;

  cleanjid = jidtodisp(bjid); // Stripping resource, just in case...

  // We don't check if the jabber user already exists in the roster,
  // because it allows to re-ask for notification.

  iqn = iqs_new(JPACKET__SET, NS_ROSTER, NULL, IQS_DEFAULT_TIMEOUT);
  y = xmlnode_insert_tag(xmlnode_get_tag(iqn->xmldata, "query"), "item");

  xmlnode_put_attrib(y, "jid", cleanjid);

  if (name)
    xmlnode_put_attrib(y, "name", name);

  if (group) {
    z = xmlnode_insert_tag(y, "group");
    xmlnode_insert_cdata(z, group, (unsigned) -1);
  }

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX

  jb_subscr_request_auth(cleanjid);

  roster_add_user(cleanjid, name, group, ROSTER_TYPE_USER, sub_pending, -1);
  g_free(cleanjid);
  buddylist_build();

  update_roster = TRUE;
}

void jb_delbuddy(const char *bjid)
{
  xmlnode y, z;
  eviqs *iqn;
  char *cleanjid;

  if (!online) return;

  cleanjid = jidtodisp(bjid); // Stripping resource, just in case...

  // If the current buddy is an agent, unsubscribe from it
  if (roster_gettype(cleanjid) == ROSTER_TYPE_AGENT) {
    scr_LogPrint(LPRINT_LOGNORM, "Unregistering from the %s agent", cleanjid);

    iqn = iqs_new(JPACKET__SET, NS_REGISTER, NULL, IQS_DEFAULT_TIMEOUT);
    xmlnode_put_attrib(iqn->xmldata, "to", cleanjid);
    y = xmlnode_get_tag(iqn->xmldata, "query");
    xmlnode_insert_tag(y, "remove");
    jab_send(jc, iqn->xmldata);
    iqs_del(iqn->id); // XXX
  }

  // Cancel the subscriptions
  jb_subscr_cancel_auth(cleanjid);    // Cancel "from"
  jb_subscr_request_cancel(cleanjid); // Cancel "to"

  // Ask for removal from roster
  iqn = iqs_new(JPACKET__SET, NS_ROSTER, NULL, IQS_DEFAULT_TIMEOUT);
  y = xmlnode_get_tag(iqn->xmldata, "query");
  z = xmlnode_insert_tag(y, "item");
  xmlnode_put_attrib(z, "jid", cleanjid);
  xmlnode_put_attrib(z, "subscription", "remove");
  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX

  roster_del_user(cleanjid);
  g_free(cleanjid);
  buddylist_build();

  update_roster = TRUE;
}

void jb_updatebuddy(const char *bjid, const char *name, const char *group)
{
  xmlnode y;
  eviqs *iqn;
  char *cleanjid;

  if (!online) return;

  // XXX We should check name's and group's correctness

  cleanjid = jidtodisp(bjid); // Stripping resource, just in case...

  iqn = iqs_new(JPACKET__SET, NS_ROSTER, NULL, IQS_DEFAULT_TIMEOUT);
  y = xmlnode_insert_tag(xmlnode_get_tag(iqn->xmldata, "query"), "item");
  xmlnode_put_attrib(y, "jid", cleanjid);
  xmlnode_put_attrib(y, "name", name);

  if (group) {
    y = xmlnode_insert_tag(y, "group");
    xmlnode_insert_cdata(y, group, (unsigned) -1);
  }

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
  g_free(cleanjid);
}

void jb_request(const char *fjid, enum iqreq_type reqtype)
{
  GSList *resources, *p_res;
  GSList *roster_elt;
  void (*request_fn)(const char *);
  const char *strreqtype;

  if (reqtype == iqreq_version) {
    request_fn = &request_version;
    strreqtype = "version";
  } else if (reqtype == iqreq_time) {
    request_fn = &request_time;
    strreqtype = "time";
  } else if (reqtype == iqreq_last) {
    request_fn = &request_last;
    strreqtype = "last";
  } else if (reqtype == iqreq_vcard) {
    // Special case
  } else
    return;

  // vCard request
  if (reqtype == iqreq_vcard) {
    request_vcard(fjid);
    scr_LogPrint(LPRINT_NORMAL, "Sent vCard request to <%s>", fjid);
    return;
  }

  if (strchr(fjid, JID_RESOURCE_SEPARATOR)) {
    // This is a full JID
    (*request_fn)(fjid);
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fjid);
    return;
  }

  // The resource has not been specified
  roster_elt = roster_find(fjid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_ROOM);
  if (!roster_elt) {
    scr_LogPrint(LPRINT_NORMAL, "No known resource for <%s>...", fjid);
    (*request_fn)(fjid); // Let's send a request anyway...
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fjid);
    return;
  }

  // Send a request to each resource
  resources = buddy_getresources(roster_elt->data);
  if (!resources) {
    scr_LogPrint(LPRINT_NORMAL, "No known resource for <%s>...", fjid);
    (*request_fn)(fjid); // Let's send a request anyway...
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fjid);
  }
  for (p_res = resources ; p_res ; p_res = g_slist_next(p_res)) {
    gchar *fulljid;
    fulljid = g_strdup_printf("%s/%s", fjid, (char*)p_res->data);
    (*request_fn)(fulljid);
    scr_LogPrint(LPRINT_NORMAL, "Sent %s request to <%s>", strreqtype, fulljid);
    g_free(fulljid);
    g_free(p_res->data);
  }
  g_slist_free(resources);
}

// Join a MUC room
void jb_room_join(const char *room, const char *nickname, const char *passwd)
{
  xmlnode x, y;
  gchar *roomid;
  GSList *room_elt;

  if (!online || !room) return;
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
  x = presnew(mystatus, roomid, mystatusmsg);
  y = xmlnode_insert_tag(x, "x");
  xmlnode_put_attrib(y, "xmlns", "http://jabber.org/protocol/muc");
  if (passwd) {
    xmlnode_insert_cdata(xmlnode_insert_tag(y, "password"), passwd,
                         (unsigned) -1);
  }

  jab_send(jc, x);
  xmlnode_free(x);
  jb_reset_keepalive();
  g_free(roomid);
}

// Unlock a MUC room
// room syntax: "room@server"
void jb_room_unlock(const char *room)
{
  xmlnode y, z;
  eviqs *iqn;

  if (!online || !room) return;

  iqn = iqs_new(JPACKET__SET, "http://jabber.org/protocol/muc#owner",
                "unlock", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", room);
  y = xmlnode_get_tag(iqn->xmldata, "query");
  z = xmlnode_insert_tag(y, "x");
  xmlnode_put_attrib(z, "xmlns", "jabber:x:data");
  xmlnode_put_attrib(z, "type", "submit");

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
  jb_reset_keepalive();
}

// Destroy a MUC room
// room syntax: "room@server"
void jb_room_destroy(const char *room, const char *venue, const char *reason)
{
  xmlnode y, z;
  eviqs *iqn;

  if (!online || !room) return;

  iqn = iqs_new(JPACKET__SET, "http://jabber.org/protocol/muc#owner",
                "destroy", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", room);
  y = xmlnode_get_tag(iqn->xmldata, "query");
  z = xmlnode_insert_tag(y, "destroy");

  if (venue && *venue)
    xmlnode_put_attrib(z, "jid", venue);

  if (reason) {
    y = xmlnode_insert_tag(z, "reason");
    xmlnode_insert_cdata(y, reason, (unsigned) -1);
  }

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
  jb_reset_keepalive();
}

// Change role or affiliation of a MUC user
// room syntax: "room@server"
// Either the jid or the nickname must be set (when banning, only the jid is
// allowed)
// ra: new role or affiliation
//     (ex. role none for kick, affil outcast for ban...)
// The reason can be null
// Return 0 if everything is ok
int jb_room_setattrib(const char *roomid, const char *fjid, const char *nick,
                      struct role_affil ra, const char *reason)
{
  xmlnode y, z;
  eviqs *iqn;

  if (!online || !roomid) return 1;
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

  iqn = iqs_new(JPACKET__SET, "http://jabber.org/protocol/muc#admin",
                "roleaffil", IQS_DEFAULT_TIMEOUT);
  xmlnode_put_attrib(iqn->xmldata, "to", roomid);
  xmlnode_put_attrib(iqn->xmldata, "type", "set");
  y = xmlnode_get_tag(iqn->xmldata, "query");
  z = xmlnode_insert_tag(y, "item");

  if (fjid) {
    xmlnode_put_attrib(z, "jid", fjid);
  } else { // nickname
    xmlnode_put_attrib(z, "nick", nick);
  }

  if (ra.type == type_affil)
    xmlnode_put_attrib(z, "affiliation", straffil[ra.val.affil]);
  else if (ra.type == type_role)
    xmlnode_put_attrib(z, "role", strrole[ra.val.role]);

  if (reason) {
    y = xmlnode_insert_tag(z, "reason");
    xmlnode_insert_cdata(y, reason, (unsigned) -1);
  }

  jab_send(jc, iqn->xmldata);
  iqs_del(iqn->id); // XXX
  jb_reset_keepalive();

  return 0;
}

// Invite a user to a MUC room
// room syntax: "room@server"
// reason can be null.
void jb_room_invite(const char *room, const char *fjid, const char *reason)
{
  xmlnode x, y, z;

  if (!online || !room || !fjid) return;

  x = jutil_msgnew(NULL, (char*)room, NULL, NULL);

  y = xmlnode_insert_tag(x, "x");
  xmlnode_put_attrib(y, "xmlns", "http://jabber.org/protocol/muc#user");

  z = xmlnode_insert_tag(y, "invite");
  xmlnode_put_attrib(z, "to", fjid);

  if (reason) {
    y = xmlnode_insert_tag(z, "reason");
    xmlnode_insert_cdata(y, reason, (unsigned) -1);
  }

  jab_send(jc, x);
  xmlnode_free(x);
  jb_reset_keepalive();
}

//  jb_is_bookmarked(roomjid)
// Return TRUE if there's a bookmark for the given jid.
guint jb_is_bookmarked(const char *bjid)
{
  xmlnode x;

  if (!bookmarks)
    return FALSE;

  // Walk through the storage bookmark tags
  x = xmlnode_get_firstchild(bookmarks);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *p = xmlnode_get_name(x);
    // If the node is a conference item, check the jid.
    if (p && !strcmp(p, "conference")) {
      const char *fjid = xmlnode_get_attrib(x, "jid");
      if (fjid && !strcasecmp(bjid, fjid))
        return TRUE;
    }
  }
  return FALSE;
}

//  jb_get_bookmark_nick(roomjid)
// Return the room nickname if it is present in a bookmark.
const char *jb_get_bookmark_nick(const char *bjid)
{
  xmlnode x;

  if (!bookmarks || !bjid)
    return NULL;

  // Walk through the storage bookmark tags
  x = xmlnode_get_firstchild(bookmarks);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *p = xmlnode_get_name(x);
    // If the node is a conference item, check the jid.
    if (p && !strcmp(p, "conference")) {
      const char *fjid = xmlnode_get_attrib(x, "jid");
      if (fjid && !strcasecmp(bjid, fjid))
        return xmlnode_get_tag_data(x, "nick");
    }
  }
  return NULL;
}


//  jb_get_all_storage_bookmarks()
// Return a GSList with all storage bookmarks.
// The caller should g_free the list (not the MUC jids).
GSList *jb_get_all_storage_bookmarks(void)
{
  xmlnode x;
  GSList *sl_bookmarks = NULL;

  // If we have no bookmarks, probably the server doesn't support them.
  if (!bookmarks)
    return NULL;

  // Walk through the storage bookmark tags
  x = xmlnode_get_firstchild(bookmarks);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *p = xmlnode_get_name(x);
    // If the node is a conference item, let's add the note to our list.
    if (p && !strcmp(p, "conference")) {
      const char *fjid = xmlnode_get_attrib(x, "jid");
      if (!fjid)
        continue;
      sl_bookmarks = g_slist_append(sl_bookmarks, (char*)fjid);
    }
  }
  return sl_bookmarks;
}

//  jb_set_storage_bookmark(roomid, name, nick, passwd, autojoin,
//                          printstatus, autowhois)
// Update the private storage bookmarks: add a conference room.
// If name is nil, we remove the bookmark.
void jb_set_storage_bookmark(const char *roomid, const char *name,
                             const char *nick, const char *passwd,
                             int autojoin, enum room_printstatus pstatus,
                             enum room_autowhois awhois)
{
  xmlnode x;
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
  x = xmlnode_get_firstchild(bookmarks);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *p = xmlnode_get_name(x);
    // If the current node is a conference item, see if we have to replace it.
    if (p && !strcmp(p, "conference")) {
      const char *fjid = xmlnode_get_attrib(x, "jid");
      if (!fjid)
        continue;
      if (!strcmp(fjid, roomid)) {
        // We've found a bookmark for this room.  Let's hide it and we'll
        // create a new one.
        xmlnode_hide(x);
        changed = TRUE;
        if (!name)
          scr_LogPrint(LPRINT_LOGNORM, "Deleting bookmark...");
      }
    }
  }

  // Let's create a node/bookmark for this roomid, if the name is not NULL.
  if (name) {
    x = xmlnode_insert_tag(bookmarks, "conference");
    xmlnode_put_attrib(x, "jid", roomid);
    xmlnode_put_attrib(x, "name", name);
    xmlnode_put_attrib(x, "autojoin", autojoin ? "1" : "0");
    if (nick)
      xmlnode_insert_cdata(xmlnode_insert_tag(x, "nick"), nick, -1);
    if (passwd)
      xmlnode_insert_cdata(xmlnode_insert_tag(x, "password"), passwd, -1);
    if (pstatus)
      xmlnode_insert_cdata(xmlnode_insert_tag(x, "print_status"),
                           strprintstatus[pstatus], -1);
    if (awhois)
      xmlnode_put_attrib(x, "autowhois", (awhois == autowhois_on ? "1" : "0"));
    changed = TRUE;
    scr_LogPrint(LPRINT_LOGNORM, "Updating bookmarks...");
  }

  if (!changed)
    return;

  if (online)
    send_storage_bookmarks();
  else
    scr_LogPrint(LPRINT_LOGNORM,
                 "Warning: you're not connected to the server.");
}

static struct annotation *parse_storage_rosternote(xmlnode notenode)
{
  const char *p;
  struct annotation *note = g_new0(struct annotation, 1);
  p = xmlnode_get_attrib(notenode, "cdate");
  if (p)
    note->cdate = from_iso8601(p, 1);
  p = xmlnode_get_attrib(notenode, "mdate");
  if (p)
    note->mdate = from_iso8601(p, 1);
  note->text = g_strdup(xmlnode_get_data(notenode));
  note->jid = g_strdup(xmlnode_get_attrib(notenode, "jid"));
  return note;
}

//  jb_get_all_storage_rosternotes()
// Return a GSList with all storage annotations.
// The caller should g_free the list and its contents.
GSList *jb_get_all_storage_rosternotes(void)
{
  xmlnode x;
  GSList *sl_notes = NULL;

  // If we have no rosternotes, probably the server doesn't support them.
  if (!rosternotes)
    return NULL;

  // Walk through the storage rosternotes tags
  x = xmlnode_get_firstchild(rosternotes);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *p;
    struct annotation *note;
    p = xmlnode_get_name(x);

    // We want a note item
    if (!p || strcmp(p, "note"))
      continue;
    // Just in case, check the jid...
    if (!xmlnode_get_attrib(x, "jid"))
      continue;
    // Ok, let's add the note to our list
    note = parse_storage_rosternote(x);
    sl_notes = g_slist_append(sl_notes, note);
  }
  return sl_notes;
}

//  jb_get_storage_rosternotes(barejid, silent)
// Return the annotation associated with this jid.
// If silent is TRUE, no warning is displayed when rosternotes is disabled
// The caller should g_free the string and structure after use.
struct annotation *jb_get_storage_rosternotes(const char *barejid, int silent)
{
  xmlnode x;

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
  x = xmlnode_get_firstchild(rosternotes);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *fjid;
    const char *p;
    p = xmlnode_get_name(x);
    // We want a note item
    if (!p || strcmp(p, "note"))
      continue;
    // Just in case, check the jid...
    fjid = xmlnode_get_attrib(x, "jid");
    if (fjid && !strcmp(fjid, barejid)) // We've found a note for this contact.
      return parse_storage_rosternote(x);
  }
  return NULL;  // No note found
}

//  jb_set_storage_rosternotes(barejid, note)
// Update the private storage rosternotes: add/delete a note.
// If note is nil, we remove the existing note.
void jb_set_storage_rosternotes(const char *barejid, const char *note)
{
  xmlnode x;
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
  x = xmlnode_get_firstchild(rosternotes);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    const char *p = xmlnode_get_name(x);
    // If the current node is a conference item, see if we have to replace it.
    if (p && !strcmp(p, "note")) {
      const char *fjid = xmlnode_get_attrib(x, "jid");
      if (!fjid)
        continue;
      if (!strcmp(fjid, barejid)) {
        // We've found a note for this jid.  Let's hide it and we'll
        // create a new one.
        cdate = xmlnode_get_attrib(x, "cdate");
        xmlnode_hide(x);
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
    x = xmlnode_insert_tag(rosternotes, "note");
    xmlnode_put_attrib(x, "jid", barejid);
    xmlnode_put_attrib(x, "cdate", cdate);
    xmlnode_put_attrib(x, "mdate", mdate);
    xmlnode_insert_cdata(x, note, -1);
    changed = TRUE;
  }

  if (!changed)
    return;

  if (online)
    send_storage_rosternotes();
  else
    scr_LogPrint(LPRINT_LOGNORM,
                 "Warning: you're not connected to the server.");
}

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
                            xmlnode xmldata, const char *text)
{
#ifdef HAVE_GPGME
  char *p, *key;
  GSList *sl_buddy;
  struct pgp_data *res_pgpdata;
  gpgme_sigsum_t sigsum;

  // All parameters must be valid
  if (!(xmldata && barejid && rname && text))
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

  p = xmlnode_get_name(xmldata);
  if (!p || strcmp(p, "x"))
    return; // We expect "<x xmlns='jabber:x:signed'>"

  // Get signature
  p = xmlnode_get_data(xmldata);
  if (!p)
    return;

  key = gpg_verify(p, text, &sigsum);
  if (key) {
    const char *expectedkey;
    char *buf;
    g_free(res_pgpdata->sign_keyid);
    res_pgpdata->sign_keyid = key;
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

static void gotmessage(char *type, const char *from, const char *body,
                       const char *enc, const char *subject, time_t timestamp,
                       xmlnode xmldata_signed)
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
  if (xmldata_signed && gpg_enabled())
    check_signature(bjid, rname, xmldata_signed, decrypted_pgp);
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
  if (type && !strcmp(type, "groupchat") && !roster_getnickname(bjid)) {
    // It shouldn't happen, probably a server issue
    GSList *room_elt;
    char *mbuf;

    mbuf = g_strdup_printf("Unexpected groupchat packet!");
    scr_LogPrint(LPRINT_LOGNORM, "%s", mbuf);
    scr_WriteIncomingMessage(bjid, mbuf, 0, HBB_PREFIX_INFO, 0);
    g_free(mbuf);

    // Send back an unavailable packet
    jb_setstatus(offline, bjid, "", TRUE);

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
  if (!settings_opt_get_int("block_unsubscribed") ||
      (roster_getsubscription(bjid) & sub_from) ||
      (type && strcmp(type, "chat")) ||
      ((s = settings_opt_get("server")) != NULL && !strcasecmp(bjid, s))) {
    gchar *fullbody = NULL;
    if (subject) {
      if (body)
        fullbody = g_strdup_printf("[%s]\n%s", subject, body);
      else
        fullbody = g_strdup_printf("[%s]\n", subject);
      body = fullbody;
    }
    hk_message_in(bjid, rname, timestamp, body, type,
                  ((decrypted_pgp || otr_msg) ? TRUE : FALSE));
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

static const char *defaulterrormsg(int code)
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
  char *sdesc;
  int code = 0;
  char *s;
  const char *p;

  if (!x) return;

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
  if (s && *s) desc = s;

  // If we still have no description, let's give up
  if (!desc)
    return;

  // Strip trailing newlines
  sdesc = g_strdup(desc);
  for (s = sdesc; *s; s++) ;
  if (s > sdesc)
    s--;
  while (s >= sdesc && (*s == '\n' || *s == '\r'))
    *s-- = '\0';

  scr_LogPrint(LPRINT_LOGNORM, "Error code from server: %d %s", code, sdesc);
  g_free(sdesc);
}

static void statehandler(jconn conn, int state)
{
  static int previous_state = -1;

  scr_LogPrint(LPRINT_DEBUG, "StateHandler called (state=%d).", state);

  switch(state) {
    case JCONN_STATE_OFF:
        if (previous_state != JCONN_STATE_OFF)
          scr_LogPrint(LPRINT_LOGNORM, "[Jabber] Not connected to the server");

        // Sometimes the state isn't correctly updated
        if (jc)
          jc->state = JCONN_STATE_OFF;
        online = FALSE;
        mystatus = offline;
        // Free bookmarks
        xmlnode_free(bookmarks);
        bookmarks = NULL;
        // Free roster
        roster_free();
        xmlnode_free(rosternotes);
        rosternotes = NULL;
        // Update display
        update_roster = TRUE;
        scr_UpdateBuddyWindow();
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
        update_last_use();
        // We set AutoConnection to true after the 1st successful connection
        AutoConnection = TRUE;
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

inline static xmlnode xml_get_xmlns(xmlnode xmldata, const char *xmlns)
{
  xmlnode x;
  char *p;

  x = xmlnode_get_firstchild(xmldata);
  for ( ; x; x = xmlnode_get_nextsibling(x)) {
    if ((p = xmlnode_get_attrib(x, "xmlns")) && !strcmp(p, xmlns))
      break;
  }
  return x;
}

static time_t xml_get_timestamp(xmlnode xmldata)
{
  xmlnode x;
  char *p;

  x = xml_get_xmlns(xmldata, NS_XMPP_DELAY);
  if (x && !strcmp(xmlnode_get_name(x), "delay") &&
      (p = xmlnode_get_attrib(x, "stamp")) != NULL)
    return from_iso8601(p, 1);
  x = xml_get_xmlns(xmldata, NS_DELAY);
  if ((p = xmlnode_get_attrib(x, "stamp")) != NULL)
    return from_iso8601(p, 1);
  return 0;
}

//  muc_get_item_info(...)
// Get room member's information from xmlndata.
// The variables must be initialized before calling this function,
// because they are not touched if the relevant information is missing.
static void muc_get_item_info(const char *from, xmlnode xmldata,
                              enum imrole *mbrole, enum imaffiliation *mbaffil,
                              const char **mbjid, const char **mbnick,
                              const char **actorjid, const char **reason)
{
  xmlnode y, z;
  char *p;

  y = xmlnode_get_tag(xmldata, "item");
  if (!y)
    return;

  p = xmlnode_get_attrib(y, "affiliation");
  if (p) {
    if (!strcmp(p, "owner"))        *mbaffil = affil_owner;
    else if (!strcmp(p, "admin"))   *mbaffil = affil_admin;
    else if (!strcmp(p, "member"))  *mbaffil = affil_member;
    else if (!strcmp(p, "outcast")) *mbaffil = affil_outcast;
    else if (!strcmp(p, "none"))    *mbaffil = affil_none;
    else scr_LogPrint(LPRINT_LOGNORM, "<%s>: Unknown affiliation \"%s\"",
                      from, p);
  }
  p = xmlnode_get_attrib(y, "role");
  if (p) {
    if (!strcmp(p, "moderator"))        *mbrole = role_moderator;
    else if (!strcmp(p, "participant")) *mbrole = role_participant;
    else if (!strcmp(p, "visitor"))     *mbrole = role_visitor;
    else if (!strcmp(p, "none"))        *mbrole = role_none;
    else scr_LogPrint(LPRINT_LOGNORM, "<%s>: Unknown role \"%s\"",
                      from, p);
  }
  *mbjid = xmlnode_get_attrib(y, "jid");
  *mbnick = xmlnode_get_attrib(y, "nick");
  // For kick/ban, there can be actor and reason tags
  *reason = xmlnode_get_tag_data(y, "reason");
  z = xmlnode_get_tag(y, "actor");
  if (z)
    *actorjid = xmlnode_get_attrib(z, "jid");
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

static void handle_presence_muc(const char *from, xmlnode xmldata,
                                const char *roomjid, const char *rname,
                                enum imstatus ust, char *ustmsg,
                                time_t usttime, char bpprio)
{
  xmlnode y;
  char *p;
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
    jb_setstatus(offline, roomjid, "", TRUE);
    scr_DrawRoster();
    return;
  }

  // Get the status code
  // 201: a room has been created
  // 301: the user has been banned from the room
  // 303: new room nickname
  // 307: the user has been kicked from the room
  // 321,322,332: the user has been removed from the room
  y = xmlnode_get_tag(xmldata, "status");
  if (y) {
    p = xmlnode_get_attrib(y, "code");
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
    jb_room_unlock(roomjid);

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
  if (!mbnick && ust == offline) {
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
      scr_UpdateChatStatus(FALSE);
      update_roster = TRUE;
    }

    // The message depends on _who_ left, and _how_
    if (how) {
      gchar *mbuf_end;
      // Forced leave
      if (actorjid) {
        mbuf_end = g_strdup_printf("%s from %s by <%s>.\nReason: %s",
                                   (how == ban ? "banned" : "kicked"),
                                   roomjid, actorjid, reason);
      } else {
        mbuf_end = g_strdup_printf("%s from %s.",
                                   (how == ban ? "banned" : "kicked"),
                                   roomjid);
      }
      if (we_left)
        mbuf = g_strdup_printf("You have been %s", mbuf_end);
      else
        mbuf = g_strdup_printf("%s has been %s", rname, mbuf_end);

      g_free(mbuf_end);
    } else {
      // Natural leave
      if (we_left) {
        xmlnode destroynode = xmlnode_get_tag(xmldata, "destroy");
        if (destroynode) {
          if ((reason = xmlnode_get_tag_data(destroynode, "reason"))) {
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
    room_whois(room_elt->data, joiner_nick, FALSE);
    g_free(joiner_nick);
  }

  scr_DrawRoster();
}

static void handle_packet_presence(jconn conn, char *type, char *from,
                                   xmlnode xmldata)
{
  char *p, *r;
  char *ustmsg;
  const char *rname;
  enum imstatus ust;
  char bpprio;
  time_t timestamp = 0L;
  xmlnode muc_packet;

  rname = strchr(from, JID_RESOURCE_SEPARATOR);
  if (rname) rname++;

  r = jidtodisp(from);

  // Check for MUC presence packet
  muc_packet = xml_get_xmlns(xmldata, "http://jabber.org/protocol/muc#user");

  if (type && !strcmp(type, TMSG_ERROR)) {
    xmlnode x;
    scr_LogPrint(LPRINT_LOGNORM, "Error presence packet from <%s>", r);
    x = xmlnode_get_tag(xmldata, TMSG_ERROR);
    display_server_error(x);

    // Let's check it isn't a nickname conflict.
    // XXX Note: We should handle the <conflict/> string condition.
    if ((p = xmlnode_get_attrib(x, "code")) != NULL) {
      if (atoi(p) == 409) {
        // 409 = conlict (nickname is in use or registered by another user)
        // If we are not inside this room, we should reset the nickname
        GSList *room_elt = roster_find(r, jidsearch, 0);
        if (room_elt && !buddy_getinsideroom(room_elt->data))
          buddy_setnickname(room_elt->data, NULL);
      }
    }

    g_free(r);
    return;
  }

  p = xmlnode_get_tag_data(xmldata, "priority");
  if (p && *p) bpprio = (gchar)atoi(p);
  else         bpprio = 0;

  ust = available;
  p = xmlnode_get_tag_data(xmldata, "show");
  if (p) {
    if (!strcmp(p, "away"))      ust = away;
    else if (!strcmp(p, "dnd"))  ust = dontdisturb;
    else if (!strcmp(p, "xa"))   ust = notavail;
    else if (!strcmp(p, "chat")) ust = freeforchat;
  }

  if (type && !strcmp(type, "unavailable"))
    ust = offline;

  ustmsg = xmlnode_get_tag_data(xmldata, "status");

  // Timestamp?
  timestamp = xml_get_timestamp(xmldata);

  if (muc_packet) {
    // This is a MUC presence message
    handle_presence_muc(from, muc_packet, r, rname,
                        ust, ustmsg, timestamp, bpprio);
  } else {
    // Not a MUC message, so this is a regular buddy...
    // Call hk_statuschange() if status has changed or if the
    // status message is different
    const char *m;
    m = roster_getstatusmsg(r, rname);
    if ((ust != roster_getstatus(r, rname)) ||
        (!ustmsg && m && m[0]) || (ustmsg && (!m || strcmp(ustmsg, m))))
      hk_statuschange(r, rname, bpprio, timestamp, ust, ustmsg);
    // Presence signature processing
    if (!ustmsg)
      ustmsg = ""; // Some clients omit the <status/> element :-(
    check_signature(r, rname, xml_get_xmlns(xmldata, NS_SIGNED), ustmsg);
  }

  g_free(r);
}

//  got_invite(from, to, reason, passwd)
// This function should be called when receiving an invitation from user
// "from", to enter the room "to".  Optional reason and room password can
// be provided.
static void got_invite(char* from, char *to, char* reason, char* passwd)
{
  eviqs *evn;
  event_muc_invitation *invitation;
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

  evn = evs_new(EVS_TYPE_INVITATION, EVS_MAX_TIMEOUT);
  if (evn) {
    evn->callback = &evscallback_invitation;
    invitation = g_new(event_muc_invitation, 1);
    invitation->to = g_strdup(to);
    invitation->from = g_strdup(from);
    invitation->passwd = g_strdup(passwd);
    invitation->reason = g_strdup(reason);
    evn->data = invitation;
    evn->desc = g_strdup_printf("<%s> invites you to %s ", from, to);
    g_string_printf(sbuf, "Please use /event %s accept|reject", evn->id);
  } else {
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
static void got_muc_message(char *from, xmlnode x)
{
  xmlnode invite = xmlnode_get_tag(x, "invite");
  if (invite)
  {
    char* invite_from;
    char *reason = NULL;
    char *password = NULL;
    xmlnode r;

    invite_from = xmlnode_get_attrib(invite, "from");
    r = xmlnode_get_tag(invite, "reason");
    if (r)
      reason = xmlnode_get_tag_data(r, NULL);
    r = xmlnode_get_tag(invite, "password");
    if (r)
      password = xmlnode_get_tag_data(r, NULL);
    if (invite_from)
      got_invite(invite_from, from, reason, password);
  }
  // TODO
  // handle status code = 100 ( not anonymous )
  // handle status code = 170 ( changement de config )
  // 10.2.1 Notification of Configuration Changes
  // declined invitation
}

static void handle_packet_message(jconn conn, char *type, char *from,
                                  xmlnode xmldata)
{
  char *p, *r, *s;
  xmlnode x;
  char *body = NULL;
  char *enc = NULL;
  char *subject = NULL;
  time_t timestamp = 0L;

  body = xmlnode_get_tag_data(xmldata, "body");

  x = xml_get_xmlns(xmldata, NS_ENCRYPTED);
  if (x && (p = xmlnode_get_data(x)) != NULL)
    enc = p;

  p = xmlnode_get_tag_data(xmldata, "subject");
  if (p != NULL) {
    if (!type || strcmp(type, TMSG_GROUPCHAT)) {  // Chat message
      subject = p;
    } else {                                      // Room topic
      GSList *roombuddy;
      gchar *mbuf;
      gchar *subj = p;
      // Get the room (s) and the nickname (r)
      s = g_strdup(from);
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
  timestamp = xml_get_timestamp(xmldata);

  if (type && !strcmp(type, TMSG_ERROR)) {
    x = xmlnode_get_tag(xmldata, TMSG_ERROR);
    display_server_error(x);
#if defined JEP0022 || defined JEP0085
    // If the JEP85/22 support is probed, set it back to unknown so that
    // we probe it again.
    chatstates_reset_probed(from);
#endif
  } else {
    handle_state_events(from, xmldata);
  }
  if (from && (body || subject))
    gotmessage(type, from, body, enc, subject, timestamp,
               xml_get_xmlns(xmldata, NS_SIGNED));

  if (from) {
    x = xml_get_xmlns(xmldata, "http://jabber.org/protocol/muc#user");
    if (x && !strcmp(xmlnode_get_name(x), "x"))
      got_muc_message(from, x);
  }
}

static void handle_state_events(char *from, xmlnode xmldata)
{
#if defined JEP0022 || defined JEP0085
  xmlnode state_ns = NULL;
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
  bjid  = jidtodisp(from);
  sl_buddy = roster_find(bjid, jidsearch, ROSTER_TYPE_USER);
  g_free(bjid);

  /* XXX Actually that's wrong, since it filters out server "offline"
     messages (for JEP-0022).  This JEP is (almost) deprecated so
     we don't really care. */
  if (!sl_buddy || !rname++) {
    return;
  }

  /* Let's see chich JEP the contact uses.  If possible, we'll use
     JEP-85, if not we'll look for JEP-22 support. */
  events = buddy_resource_getevents(sl_buddy->data, rname);

  jep85 = buddy_resource_jep85(sl_buddy->data, rname);
  if (jep85) {
    state_ns = xml_get_xmlns(xmldata, NS_CHATSTATES);
    if (state_ns)
      which_jep = JEP_85;
  }

  if (which_jep != JEP_85) { /* Fall back to JEP-0022 */
    jep22 = buddy_resource_jep22(sl_buddy->data, rname);
    if (jep22) {
      state_ns = xml_get_xmlns(xmldata, NS_EVENT);
      if (state_ns)
        which_jep = JEP_22;
    }
  }

  if (!which_jep) { /* Sender does not use chat states */
    return;
  }

  body = xmlnode_get_tag_data(xmldata, "body");

  if (which_jep == JEP_85) { /* JEP-0085 */
    const char *p;
    jep85->support = CHATSTATES_SUPPORT_OK;

    p = xmlnode_get_name(state_ns);
    if (!strcmp(p, "composing")) {
      jep85->last_state_rcvd = ROSTER_EVENT_COMPOSING;
    } else if (!strcmp(p, "active")) {
      jep85->last_state_rcvd = ROSTER_EVENT_ACTIVE;
    } else if (!strcmp(p, "paused")) {
      jep85->last_state_rcvd = ROSTER_EVENT_PAUSED;
    } else if (!strcmp(p, "inactive")) {
      jep85->last_state_rcvd = ROSTER_EVENT_INACTIVE;
    } else if (!strcmp(p, "gone")) {
      jep85->last_state_rcvd = ROSTER_EVENT_GONE;
    }
    events = jep85->last_state_rcvd;
  } else {              /* JEP-0022 */
#ifdef JEP0022
    const char *msgid;
    jep22->support = CHATSTATES_SUPPORT_OK;
    jep22->last_state_rcvd = ROSTER_EVENT_NONE;

    msgid = xmlnode_get_attrib(xmldata, "id");

    if (xmlnode_get_tag(state_ns, "composing")) {
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

    if (xmlnode_get_tag(state_ns, "delivered")) {
      jep22->last_state_rcvd |= ROSTER_EVENT_DELIVERED;

      // Do we have to send back an ACK?
      if (body)
        jb_send_jep22_event(from, ROSTER_EVENT_DELIVERED);
    }
#endif
  }

  buddy_resource_setevents(sl_buddy->data, rname, events);

  update_roster = TRUE;
#endif
}

static int evscallback_subscription(eviqs *evp, guint evcontext)
{
  char *barejid;
  char *buf;

  if (evcontext == EVS_CONTEXT_TIMEOUT) {
    scr_LogPrint(LPRINT_LOGNORM, "Event %s timed out, cancelled.",
                 evp->id);
    return 0;
  }
  if (evcontext == EVS_CONTEXT_CANCEL) {
    scr_LogPrint(LPRINT_LOGNORM, "Event %s cancelled.", evp->id);
    return 0;
  }
  if (!(evcontext & EVS_CONTEXT_USER))
    return 0;

  // Sanity check
  if (!evp->data) {
    // Shouldn't happen, data should be set to the barejid.
    scr_LogPrint(LPRINT_LOGNORM, "Error in evs callback.");
    return 0;
  }

  // Ok, let's work now.
  // evcontext: 0, 1 == reject, accept

  barejid = evp->data;

  if (evcontext & ~EVS_CONTEXT_USER) {
    // Accept subscription request
    jb_subscr_send_auth(barejid);
    buf = g_strdup_printf("<%s> is allowed to receive your presence updates",
                          barejid);
  } else {
    // Reject subscription request
    jb_subscr_cancel_auth(barejid);
    buf = g_strdup_printf("<%s> won't receive your presence updates", barejid);
    if (settings_opt_get_int("delete_on_reject")) {
      // Remove the buddy from the roster if there is no current subscription
      if (roster_getsubscription(barejid) == sub_none)
        jb_delbuddy(barejid);
    }
  }
  scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_INFO, 0);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
  g_free(buf);
  return 0;
}

static void decline_invitation(event_muc_invitation *invitation, char *reason)
{
  // cut and paste from jb_room_invite
  xmlnode x,y,z;

  if (!invitation) return;
  if (!invitation->to || !invitation->from) return;

  x = jutil_msgnew(NULL, (char*)invitation->to, NULL, NULL);

  y = xmlnode_insert_tag(x, "x");
  xmlnode_put_attrib(y, "xmlns", "http://jabber.org/protocol/muc#user");

  z = xmlnode_insert_tag(y, "decline");
  xmlnode_put_attrib(z, "to", invitation->from);

  if (reason) {
    y = xmlnode_insert_tag(z, "reason");
    xmlnode_insert_cdata(y, reason, (unsigned) -1);
  }

  jab_send(jc, x);
  xmlnode_free(x);
  jb_reset_keepalive();
}

static int evscallback_invitation(eviqs *evp, guint evcontext)
{
  event_muc_invitation *invitation = evp->data;

  // Sanity check
  if (!invitation) {
    // Shouldn't happen.
    scr_LogPrint(LPRINT_LOGNORM, "Error in evs callback.");
    return 0;
  }

  if (evcontext == EVS_CONTEXT_TIMEOUT) {
    scr_LogPrint(LPRINT_LOGNORM, "Event %s timed out, cancelled.", evp->id);
    goto evscallback_invitation_free;
  }
  if (evcontext == EVS_CONTEXT_CANCEL) {
    scr_LogPrint(LPRINT_LOGNORM, "Event %s cancelled.", evp->id);
    goto evscallback_invitation_free;
  }
  if (!(evcontext & EVS_CONTEXT_USER))
    goto evscallback_invitation_free;
  // Ok, let's work now.
  // evcontext: 0, 1 == reject, accept

  if (evcontext & ~EVS_CONTEXT_USER) {
    char *nickname = default_muc_nickname(invitation->to);
    jb_room_join(invitation->to, nickname, invitation->passwd);
    g_free(nickname);
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "Invitation to %s refused.", invitation->to);
    decline_invitation(invitation, NULL);
  }

evscallback_invitation_free:
  g_free(invitation->to);
  g_free(invitation->from);
  g_free(invitation->passwd);
  g_free(invitation->reason);
  g_free(invitation);
  evp->data = NULL;
  return 0;
}

static void handle_packet_s10n(jconn conn, char *type, char *from,
                               xmlnode xmldata)
{
  char *r;
  char *buf;
  int newbuddy;

  r = jidtodisp(from);

  newbuddy = !roster_find(r, jidsearch, 0);

  if (!strcmp(type, "subscribe")) {
    /* The sender wishes to subscribe to our presence */
    char *msg;
    int isagent;
    eviqs *evn;

    isagent = (roster_gettype(r) & ROSTER_TYPE_AGENT) != 0;
    msg = xmlnode_get_tag_data(xmldata, "status");

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
  } else if (!strcmp(type, "unsubscribe")) {
    /* The sender is unsubscribing from our presence */
    jb_subscr_cancel_auth(from);
    buf = g_strdup_printf("<%s> is unsubscribing from your "
                          "presence updates", from);
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);
  } else if (!strcmp(type, "subscribed")) {
    /* The sender has allowed us to receive their presence */
    buf = g_strdup_printf("<%s> has allowed you to receive their "
                          "presence updates", from);
    scr_WriteIncomingMessage(r, buf, 0, HBB_PREFIX_INFO, 0);
    scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
    g_free(buf);
  } else if (!strcmp(type, "unsubscribed")) {
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
    scr_LogPrint(LPRINT_LOGNORM, "Received unrecognized packet from <%s>, "
                 "type=%s", from, (type ? type : ""));
    newbuddy = FALSE;
  }

  if (newbuddy)
    update_roster = TRUE;
  g_free(r);
}

static void packethandler(jconn conn, jpacket packet)
{
  char *p;
  char *from=NULL, *type=NULL;

  jb_reset_keepalive(); // reset keepalive timeout
  jpacket_reset(packet);

  if (!packet->type) {
    scr_LogPrint(LPRINT_LOG, "Packet type = 0");
    return;
  }

  p = xmlnode_get_attrib(packet->x, "type");
  if (p) type = p;

  p = xmlnode_get_attrib(packet->x, "from");
  if (p) from = p;

  if (!from && packet->type != JPACKET_IQ) {
    scr_LogPrint(LPRINT_LOGNORM, "Error in stream packet");
    return;
  }

  switch (packet->type) {
    case JPACKET_MESSAGE:
        handle_packet_message(conn, type, from, packet->x);
        break;

    case JPACKET_IQ:
        handle_packet_iq(conn, type, from, packet->x);
        break;

    case JPACKET_PRESENCE:
        handle_packet_presence(conn, type, from, packet->x);
        break;

    case JPACKET_S10N:
        handle_packet_s10n(conn, type, from, packet->x);
        break;

    default:
        scr_LogPrint(LPRINT_LOG, "Unhandled packet type (%d)", packet->type);
  }
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
