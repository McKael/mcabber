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

#include "../libjabber/jabber.h"
#include "jabglue.h"
#include "roster.h"
#include "screen.h"
#include "hooks.h"
#include "utils.h"

#define JABBERPORT      5222
#define JABBERSSLPORT   5223

jconn jc;
time_t LastPingTime;
unsigned int KeepaliveDelay;
static int s_id = 1;  // FIXME which use??
static int regmode, regdone;
unsigned char online;

char imstatus2char[imstatus_size] = {
    '_', 'o', 'i', 'f', 'd', 'c', 'n', 'a'
};

static enum {
  STATE_CONNECTING,
  STATE_GETAUTH,
  STATE_SENDAUTH,
  STATE_LOGGED
} jstate;


void statehandler(jconn, int);
void packethandler(jconn, jpacket);

void screen_logger(jconn j, int io, const char *buf)
{
  scr_LogPrint("%03s: %s", ((io == 0) ? "OUT" : "IN"), buf);
}

void file_logger(jconn j, int io, const char *buf)
{
  ut_WriteLog("%03s: %s\n", ((io == 0) ? "OUT" : "IN"), buf);
}

void big_logger(jconn j, int io, const char *buf)
{
  screen_logger(j, io, buf);
  file_logger(j, io, buf);
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

char *jidtodisp(const char *jid)
{
  char *ptr;
  char *alias = strdup(jid);
  if ((ptr = strchr(alias, '/')) != NULL) {
    *ptr = 0;
  }
  return alias;
}

jconn jb_connect(const char *jid, unsigned int port, int ssl, const char *pass)
{
  if (!port) {
    if (ssl)
      port = JABBERSSLPORT;
    else
      port = JABBERPORT;
  }

  if (jc)
    free(jc);

  jc = jab_new((char*)jid, (char*)pass, port, ssl);

  jab_logger(jc, file_logger);
  jab_packet_handler(jc, &packethandler);
  jab_state_handler(jc, &statehandler);

  if (jc->user) {
    online = TRUE;
    jstate = STATE_CONNECTING;
    statehandler(0, -1);
    jab_start(jc);
  }

  return jc;
}

void jb_disconnect(void)
{
  statehandler(jc, JCONN_STATE_OFF);
}

inline void jb_reset_keepalive()
{
  time(&LastPingTime);
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

void jb_main()
{
  xmlnode x, z;
  char *cid;

  if (!online)
    return;
  if (jc && jc->state == JCONN_STATE_CONNECTING) {
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
      scr_LogPrint("Unable to connect to the server");
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

void setjabberstatus(enum imstatus st, char *msg)
{
  xmlnode x = jutil_presnew(JPACKET__UNKNOWN, 0, 0);

  switch(st) {
    case away:
        xmlnode_insert_cdata(xmlnode_insert_tag(x, "show"), "away",
                (unsigned) -1);
        break;

    case occupied:
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

    default:
        break;
  }

  /* TODO
  if (!add["prio"].empty())
    xmlnode_insert_cdata(xmlnode_insert_tag(x, "priority"),
            add["prio"].c_str(), (unsigned) -1);
  */

  if (!msg || !*msg) {
    msg  = ""; // FIXME
    //msg = imstatus2str(st);
  }

  xmlnode_insert_cdata(xmlnode_insert_tag(x, "status"), msg,
          (unsigned) -1);

  jab_send(jc, x);
  xmlnode_free(x);

  //sendvisibility();

  // XXX logger.putourstatus(proto, getstatus(), ourstatus = st);
}

void jb_send_msg(const char *jid, const char *text)
{
  xmlnode x = jutil_msgnew(TMSG_CHAT, (char*)jid, 0, (char*)text);
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

  setjabberstatus(1, "I'm here!");
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
    const char *group = NULL;

    z = xmlnode_get_tag(y, "group");
    if (z) group = xmlnode_get_data(z);

    if (alias) {
      char *buddyname;
      if (name)
        buddyname = (char*)name;
      else
        buddyname = jidtodisp(alias);

      roster_add_user(alias, buddyname, group, ROSTER_TYPE_USER);
      if (!name)
        free(buddyname);
    }
  }

  postlogin();
}

void gotmessage(char *type, const char *from, const char *body,
        const char *enc)
{
  char *jid;

  /*
  //char *u, *h, *r;
  //jidsplit(from, &u, &h, &r);
  // Maybe we should remember the resource?
  if (r)
    scr_LogPrint("There is an extra part in message (resource?): %s", r);
  //scr_LogPrint("Msg from <%s>, type=%s", jidtodisp(from), type);
  */

  jid = jidtodisp(from);
  hk_message_in(jid, 0, body);
  free(jid);
}

void statehandler(jconn conn, int state)
{
  static int previous_state = -1;

  ut_WriteLog("StateHandler called (state=%d).\n", state);

  switch(state) {
    case JCONN_STATE_OFF:

        online = FALSE;

        if (previous_state != JCONN_STATE_OFF) {
          scr_LogPrint("+ JCONN_STATE_OFF");
          /*
           jhook.roster.clear();
           jhook.agents.clear();
          */
        }
        break;

    case JCONN_STATE_CONNECTED:
        scr_LogPrint("+ JCONN_STATE_CONNECTED");
        break;

    case JCONN_STATE_AUTH:
        scr_LogPrint("+ JCONN_STATE_AUTH");
        break;

    case JCONN_STATE_ON:
        scr_LogPrint("+ JCONN_STATE_ON");
        online = TRUE;
        break;

    case JCONN_STATE_CONNECTING:
        scr_LogPrint("+ JCONN_STATE_CONNECTING");
        break;

    default:
        break;
  }
  previous_state = state;
}

void packethandler(jconn conn, jpacket packet)
{
  char *p, *r;
  xmlnode x, y;
  // string from, type, body, enc, ns, id, u, h, s;
  char *from=NULL, *type=NULL, *body=NULL, *enc=NULL;
  char *ns=NULL;
  char *id=NULL;
  enum imstatus ust;
  // int npos;

  jb_reset_keepalive(); // reset keepalive delay
  jpacket_reset(packet);

  p = xmlnode_get_attrib(packet->x, "from"); if (p) from = p;
  p = xmlnode_get_attrib(packet->x, "type"); if (p) type = p;

  switch (packet->type) {
    case JPACKET_MESSAGE:
        {
          char *tmp = NULL;
          x = xmlnode_get_tag(packet->x, "body");
          p = xmlnode_get_data(x); if (p) body = p;

          if ((x = xmlnode_get_tag(packet->x, "subject")) != NULL)
            if ((p = xmlnode_get_data(x)) != NULL) {
              tmp = malloc(strlen(body)+strlen(p)+3);
              *tmp = '[';
              strcpy(tmp+1, p);
              strcat(tmp, "]\n");
              strcat(tmp, body);
              body = tmp;
            }

          /* there can be multiple <x> tags. we're looking for one with
             xmlns = jabber:x:encrypted */

          for (x = xmlnode_get_firstchild(packet->x); x; x = xmlnode_get_nextsibling(x)) {
            if ((p = xmlnode_get_name(x)) && !strcmp(p, "x"))
              if ((p = xmlnode_get_attrib(x, "xmlns")) &&
                      !strcasecmp(p, "jabber:x:encrypted"))
                if ((p = xmlnode_get_data(x)) != NULL) {
                  enc = p;
                  break;
                }
          }

          if (body)
            gotmessage(type, from, body, enc);
          if (tmp)
            free(tmp);
        }
        break;

    case JPACKET_IQ:
        if (!strcmp(type, "result")) {

          if ((p = xmlnode_get_attrib(packet->x, "id")) != NULL) {
            int iid = atoi(p);

            ut_WriteLog("iid = %d\n", iid);
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
              scr_LogPrint("Got VCARD");
              return;
            } else if (!strcmp(p, "versionreq")) {
              // jhook.gotversion(ic, packet->x); TODO
              scr_LogPrint("Got version");
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
                  // const char *service = xmlnode_get_tag_data(y, "service"); TODO
                  enum agtype atype = unknown;

                  if (xmlnode_get_tag(y, "groupchat")) atype = groupchat; else
                    if (xmlnode_get_tag(y, "transport")) atype = transport; else
                      if (xmlnode_get_tag(y, "search")) atype = search;

                  if (alias && name && desc) {
                    scr_LogPrint("Agent: %s / %s / %s / type=%d",
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
                scr_LogPrint("Got agent info");
              } else if (!strcmp(id, "Lookup")) {
                // jhook.gotsearchresults(packet->x); TODO
                scr_LogPrint("Got search results");
              } else if (!strcmp(id, "Register")) {
                x = jutil_iqnew(JPACKET__GET, NS_REGISTER);
                xmlnode_put_attrib(x, "to", from);
                xmlnode_put_attrib(x, "id", "Agent info");
                jab_send(conn, x);
                xmlnode_free(x);
              }

            }
          }
        } else if (!strcmp(type, "set")) {
        } else if (!strcmp(type, "error")) {
          char *name=NULL, *desc=NULL;
          int code = 0;

          x = xmlnode_get_tag(packet->x, "error");
          p = xmlnode_get_attrib(x, "code"); if (p) code = atoi(p);
          p = xmlnode_get_attrib(x, "id"); if (p) name = p;
          p = xmlnode_get_tag_data(packet->x, "error"); if (p) desc = p;

          switch(code) {
            case 401: /* Unauthorized */
            case 302: /* Redirect */
            case 400: /* Bad request */
            case 402: /* Payment Required */
            case 403: /* Forbidden */
            case 404: /* Not Found */
            case 405: /* Not Allowed */
            case 406: /* Not Acceptable */
            case 407: /* Registration Required */
            case 408: /* Request Timeout */
            case 409: /* Conflict */
            case 500: /* Internal Server Error */
            case 501: /* Not Implemented */
            case 502: /* Remote Server Error */
            case 503: /* Service Unavailable */
            case 504: /* Remote Server Timeout */
            default:
                /*
                if (!regmode) {
                  face.log(desc.empty() ?
                          _("+ [jab] error %d") :
                          _("+ [jab] error %d: %s"),
                          code, desc.c_str());

                  if (!jhook.flogged && code != 501) {
                    close(jc->fd);
                    jc->fd = -1;
                  }

                } else {
                  jhook.regerr = desc;

                }
                */
          }
          scr_LogPrint("Error code from server (%d)", code);

        }
        break;

    case JPACKET_PRESENCE:
        x = xmlnode_get_tag(packet->x, "show");
        ust = available;

        if (x) {
          p = xmlnode_get_data(x); if (p) ns = p;

          if (ns) {
            if (!strcmp(ns, "away"))      ust = away;
            else if (!strcmp(ns, "dnd"))  ust = dontdisturb;
            else if (!strcmp(ns, "xa"))   ust = notavail;
            else if (!strcmp(ns, "chat")) ust = freeforchat;
          }
        }

        if (type && !strcmp(type, "unavailable")) {
          ust = offline;
        }

        r = jidtodisp(from);
        /*
        if (ust != roster_getstatus(r))
          scr_LogPrint("Buddy status has changed: [%c>%c] <%s>",
                  imstatus2char[roster_getstatus(r)], imstatus2char[ust], r);
        roster_setstatus(r, ust);
        */
        if (ust != roster_getstatus(r))
          hk_statuschange(r, 0, ust);
        free(r);
        buddylist_build();
        scr_DrawRoster();
        /*
        if (x = xmlnode_get_tag(packet->x, "status"))
          if (p = xmlnode_get_data(x))
            scr_LogPrint("Away msg: %s", p);
        */
        break;

    case JPACKET_S10N:
        scr_LogPrint("Received subscription packet");
        if (type) scr_LogPrint("Type=%s", type);

        if (!strcmp(type, "subscribe")) {
          int isagent;
          r = jidtodisp(from);
          isagent = (roster_gettype(r) & ROSTER_TYPE_AGENT) != 0;
          free(r);
          scr_LogPrint("isagent=%d", isagent); // XXX DBG
          if (!isagent) {
            scr_LogPrint("<%s> wants to subscribe "
                         "to your network presence updates", from);
          } else {
            x = jutil_presnew(JPACKET__SUBSCRIBED, from, 0);
            jab_send(jc, x);
            xmlnode_free(x);
          }
        } else if (!strcmp(type, "unsubscribe")) {
          x = jutil_presnew(JPACKET__UNSUBSCRIBED, from, 0);
          jab_send(jc, x);
          xmlnode_free(x);
          scr_LogPrint("<%s> has unsubscribed to your presence updates", from);
        }
        break;

    default:
        break;
  }
}

