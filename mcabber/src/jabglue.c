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
#include "screen.h"
#include "utils.h"

#define JABBERPORT      5222
#define JABBERSSLPORT   5223

jconn jc;
static int s_id = 1;  // FIXME which use??
static int regmode, regdone;

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
  scr_LogPrint("%.03s: %s", ((io == 0) ? "OUT" : "IN"), buf);
}

void file_logger(jconn j, int io, const char *buf)
{
  ut_WriteLog("%.03s: %s\n", ((io == 0) ? "OUT" : "IN"), buf);
}

void big_logger(jconn j, int io, const char *buf)
{
  screen_logger(j, io, buf);
  file_logger(j, io, buf);
}

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

char *jidtodisp(const char *jid)
{
  char *ptr;
  char *alias = strdup(jid);
  if ((ptr = strchr(alias, '/')) != NULL) {
    *ptr = 0;
  }
  return alias;
}

jconn jb_connect(const char *servername, unsigned int port, int ssl,
                  const char *jid, const char *pass,
                  const char *resource)
{
  if (!port) {
    if (ssl)
      port = JABBERSSLPORT;
    else
      port = JABBERPORT;
  }

  if (jc)
    free(jc);

  //jc = jab_new(jid, pass, port, ssl);
  jc = jab_new("mctest@lilotux.net/mcabber", (char*)pass, (int)port, ssl);

  jab_logger(jc, big_logger);
  jab_packet_handler(jc, &packethandler);
  jab_state_handler(jc, &statehandler);

  if (jc->user) {
    //fonline = TRUE;
    scr_LogPrint("+ State_Connecting");
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

void jb_keepalive()
{
  if (jc) {
    // XXX Only if connected...
    jab_send_raw(jc, " ");
  }
}

void jb_main()
{
  xmlnode x, z;
  char *cid;

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
      // fonline = FALSE;
    }
  }

  if (!jc) {
    statehandler(jc, JCONN_STATE_OFF);
  } else if (jc->state == JCONN_STATE_OFF || jc->fd == -1) {
    statehandler(jc, JCONN_STATE_OFF);
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
  }

  /* TODO
  if (!add["prio"].empty())
    xmlnode_insert_cdata(xmlnode_insert_tag(x, "priority"),
            add["prio"].c_str(), (unsigned) -1);
  */

  if (!msg || !*msg) {
    msg  = "unknownStatus";
    //msg = imstatus2str(st);
  }

  xmlnode_insert_cdata(xmlnode_insert_tag(x, "status"), msg,
          (unsigned) -1);

  jab_send(jc, x);
  xmlnode_free(x);

  //sendvisibility();

  // XXX logger.putourstatus(proto, getstatus(), ourstatus = st);
}

void postlogin()
{
  //int i;

  //flogged = TRUE;
  //ourstatus = available;

  //setautostatus(jhook.manualstatus);

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
    const char *sub = xmlnode_get_attrib(y, "subscription");
    const char *name = xmlnode_get_attrib(y, "name");
    const char *group = 0;

    z = xmlnode_get_tag(y, "group");
    if (z) group = xmlnode_get_data(z);

    if (alias) {
      char *buddyname = jidtodisp(alias);
      if (buddyname) {
        scr_LogPrint("New buddy: %s", buddyname);
        free(buddyname);
      }
    }
  }

  postlogin();
}

void gotmessage(char *type, const char *from, const char *body,
        const char *enc)
{
  char *u, *h, *r;

  jidsplit(from, &u, &h, &r);
  if (*r)
    scr_LogPrint("There is an extra part in message: %s", *r);
  scr_WriteIncomingMessage(from, body);
}

void statehandler(jconn conn, int state)
{
  static int previous_state = -1;

  scr_LogPrint("StateHandler called (%d).\n", state);
  ut_WriteLog("StateHandler called (%d).\n", state);

  switch(state) {
    case JCONN_STATE_OFF:
        /*
           jhook.flogged = jhook.fonline = FALSE;

           if (previous_state != JCONN_STATE_OFF) {
           logger.putourstatus(jhook.proto, jhook.getstatus(), jhook.ourstatus = offline);
           jhook.log(logDisconnected);
           jhook.roster.clear();
           jhook.agents.clear();
           clist.setoffline(jhook.proto);
           face.update();
           }
           */
        break;

    case JCONN_STATE_CONNECTED:
        break;

    case JCONN_STATE_AUTH:
        break;

    case JCONN_STATE_ON:
        // if (regmode) jhook.fonline = TRUE;
        break;

    default:
        break;
  }
  previous_state = state;
}

void packethandler(jconn conn, jpacket packet)
{
  char *p;
  xmlnode x; // , y;
  // string from, type, body, enc, ns, id, u, h, s;
  char *from=NULL, *type=NULL, *body=NULL, *enc=NULL;
  char *ns=NULL;
  char *id=NULL;
  // imstatus ust;
  // int npos;
  // bool isagent;

  scr_LogPrint("Received a packet");
  ut_WriteLog("Received a packet\n");

  jpacket_reset(packet);

  p = xmlnode_get_attrib(packet->x, "from"); if (p) from = p;
  p = xmlnode_get_attrib(packet->x, "type"); if (p) type = p;
  //imcontact ic(jidtodisp(from), jhook.proto);

  switch (packet->type) {
    case JPACKET_MESSAGE:
        x = xmlnode_get_tag(packet->x, "body");
        p = xmlnode_get_data(x); if (p) body = p;

        if ((x = xmlnode_get_tag(packet->x, "subject")) != NULL)
          if ((p = xmlnode_get_data(x)) != NULL) {
            char *tmp = malloc(strlen(body)+strlen(p)+3);
            strcpy(tmp, p);
            strcat(tmp, ": ");
            strcat(tmp, body);
            body = tmp; // XXX check it is free'd later...
          }

        /* there can be multiple <x> tags. we're looking for one with
           xmlns = jabber:x:encrypted */

        for (x = xmlnode_get_firstchild(packet->x); x; x = xmlnode_get_nextsibling(x)) {
          if ((p = xmlnode_get_name(x)) && !strcmp(p, "x"))
            if ((p = xmlnode_get_attrib(x, "xmlns")) && !strcasecmp(p, "jabber:x:encrypted"))
              if ((p = xmlnode_get_data(x)) != NULL) {
                enc = p;
                break;
              }
        }

        // FIXME:
        if (body) {
          scr_LogPrint("Message received");
          scr_LogPrint("Type: %s", type);
          gotmessage(type, from, body, enc);
        }

        break;

    case JPACKET_IQ:
        if (!strcmp(type, "result")) {
          scr_LogPrint("Received a result packet");
          ut_WriteLog("Received a result packet\n");

          if (p = xmlnode_get_attrib(packet->x, "id")) {
            int iid = atoi(p);

            ut_WriteLog("iid = %d\n", iid);
            if (iid == s_id) {
              if (!regmode) {
                if (jstate == STATE_GETAUTH) {
                  if (x = xmlnode_get_tag(packet->x, "query"))
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

          if (x = xmlnode_get_tag(packet->x, "query")) {
            p = xmlnode_get_attrib(x, "xmlns"); if (p) ns = p;

            if (!strcmp(ns, NS_ROSTER)) {
              gotroster(x);
            } else if (!strcmp(ns, NS_AGENTS)) {
              /* TODO...
              for (y = xmlnode_get_tag(x, "agent"); y; y = xmlnode_get_nextsibling(y)) {
                const char *alias = xmlnode_get_attrib(y, "jid");

                if (alias) {
                  const char *name = xmlnode_get_tag_data(y, "name");
                  const char *desc = xmlnode_get_tag_data(y, "description");
                  const char *service = xmlnode_get_tag_data(y, "service");
                  agent::agent_type atype = agent::atUnknown;

                  if (xmlnode_get_tag(y, "groupchat")) atype = agent::atGroupchat; else
                    if (xmlnode_get_tag(y, "transport")) atype = agent::atTransport; else
                      if (xmlnode_get_tag(y, "search")) atype = agent::atSearch;

                  if (alias && name && desc) {
                    jhook.agents.push_back(agent(alias, name, desc, atype));

                    if (atype == agent::atSearch) {
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
          int code;

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
        //ust = available;

        if (x) {
          p = xmlnode_get_data(x); if (p) ns = p;

          if (ns) {
            scr_LogPrint("New status: %s", ns);
            /*
            if (ns == "away") ust = away; else
              if (ns == "dnd") ust = dontdisturb; else
                if (ns == "xa") ust = notavail; else
                  if (ns == "chat") ust = freeforchat;
            */
          }
        }

        if (!strcmp(type, "unavailable")) {
          scr_LogPrint("New status: unavailable/offline");
          // XXX
          //  ust = offline;
        }

        /*
        jidsplit(from, u, h, s);
        id = u + "@" + h;

        if (clist.get(imcontact((string) "#" + id, jhook.proto))) {
          if (ust == offline) {
            vector<string>::iterator im = find(jhook.chatmembers[id].begin(), jhook.chatmembers[id].end(), s);
            if (im != jhook.chatmembers[id].end())
              jhook.chatmembers[id].erase(im);

          } else {
            jhook.chatmembers[id].push_back(s);

          }

        } else {
          icqcontact *c = clist.get(ic);

          if (c)
            if (c->getstatus() != ust) {
              if (c->getstatus() == offline)
                jhook.awaymsgs[ic.nickname] = "";

              logger.putonline(c, c->getstatus(), ust);
              c->setstatus(ust);

              if (x = xmlnode_get_tag(packet->x, "status"))
                if (p = xmlnode_get_data(x))
                  jhook.awaymsgs[ic.nickname] = p;

#ifdef HAVE_GPGME
              if (x = xmlnode_get_tag(packet->x, "x"))
                if (p = xmlnode_get_attrib(x, "xmlns"))
                  if ((string) p == "jabber:x:signed")
                    if (p = xmlnode_get_data(x))
                      c->setpgpkey(pgp.verify(p, jhook.awaymsgs[ic.nickname]));
#endif

            }
        }
        */
        break;

    case JPACKET_S10N:
        scr_LogPrint("Received subscription packet");
        /*
        isagent = find(jhook.agents.begin(), jhook.agents.end(), from) != jhook.agents.end();

        if (type == "subscribe") {
          if (!isagent) {
            em.store(imauthorization(ic, imevent::incoming,
                        imauthorization::Request, _("The user wants to subscribe to your network presence updates")));

          } else {
            auto_ptr<char> cfrom(strdup(from.c_str()));
            x = jutil_presnew(JPACKET__SUBSCRIBED, cfrom.get(), 0);
            jab_send(jc, x);
            xmlnode_free(x);
          }

        } else if (type == "unsubscribe") {
          auto_ptr<char> cfrom(strdup(from.c_str()));
          x = jutil_presnew(JPACKET__UNSUBSCRIBED, cfrom.get(), 0);
          jab_send(jc, x);
          xmlnode_free(x);
          em.store(imnotification(ic, _("The user has removed you from his contact list (unsubscribed you, using the Jabber language)")));

        }
        */

        break;

    default:
        break;
  }
}

