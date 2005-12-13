/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyrights
 *
 * Portions created by or assigned to Jabber.com, Inc. are
 * Copyright (c) 1999-2002 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 *
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 *
 * Acknowledgements
 *
 * Special thanks to the Jabber Open Source Contributors for their
 * suggestions and support of Jabber.
 *
 */

/**
 * @file jutil.c
 * @brief various utilities mainly for handling xmlnodes containing stanzas
 */

#include "jabber.h"

/**
 * utility for making presence stanzas
 *
 * @param type the type of the presence (one of the JPACKET__* contants)
 * @param to to whom the presence should be sent, NULL for a broadcast presence
 * @param status optional status (CDATA for the <status/> element, NULL for now <status/> element)
 * @return the xmlnode containing the created presence stanza
 */
xmlnode jutil_presnew(int type, char *to, char *status)
{
    xmlnode pres;

    pres = xmlnode_new_tag("presence");
    switch(type)
    {
    case JPACKET__SUBSCRIBE:
        xmlnode_put_attrib(pres,"type","subscribe");
        break;
    case JPACKET__UNSUBSCRIBE:
        xmlnode_put_attrib(pres,"type","unsubscribe");
        break;
    case JPACKET__SUBSCRIBED:
        xmlnode_put_attrib(pres,"type","subscribed");
        break;
    case JPACKET__UNSUBSCRIBED:
        xmlnode_put_attrib(pres,"type","unsubscribed");
        break;
    case JPACKET__PROBE:
        xmlnode_put_attrib(pres,"type","probe");
        break;
    case JPACKET__UNAVAILABLE:
        xmlnode_put_attrib(pres,"type","unavailable");
        break;
    case JPACKET__INVISIBLE:
        xmlnode_put_attrib(pres,"type","invisible");
        break;
    }
    if(to != NULL)
        xmlnode_put_attrib(pres,"to",to);
    if(status != NULL)
        xmlnode_insert_cdata(xmlnode_insert_tag(pres,"status"),status,strlen(status));

    return pres;
}

/**
 * utility for making IQ stanzas, that contain a <query/> element in a different namespace
 *
 * @note In traditional Jabber protocols the element inside an iq element has the name "query".
 * This util is not able to create IQ stanzas that contain a query which a element that does
 * not have the name "query"
 *
 * @param type the type of the iq stanza (one of JPACKET__GET, JPACKET__SET, JPACKET__RESULT, JPACKET__ERROR)
 * @param ns the namespace of the <query/> element
 * @return the created xmlnode
 */
xmlnode jutil_iqnew(int type, char *ns)
{
    xmlnode iq;

    iq = xmlnode_new_tag("iq");
    switch(type)
    {
    case JPACKET__GET:
        xmlnode_put_attrib(iq,"type","get");
        break;
    case JPACKET__SET:
        xmlnode_put_attrib(iq,"type","set");
        break;
    case JPACKET__RESULT:
        xmlnode_put_attrib(iq,"type","result");
        break;
    case JPACKET__ERROR:
        xmlnode_put_attrib(iq,"type","error");
        break;
    }
    xmlnode_put_attrib(xmlnode_insert_tag(iq,"query"),"xmlns",ns);

    return iq;
}

/**
 * utility for making message stanzas
 *
 * @param type the type of the message (as a string!)
 * @param to the recipient of the message
 * @param subj the subject of the message (NULL for no subject element)
 * @param body the body of the message
 * @return the xmlnode containing the new message stanza
 */
xmlnode jutil_msgnew(char *type, char *to, char *subj, char *body)
{
    xmlnode msg;

    msg = xmlnode_new_tag("message");

    if (type != NULL) {
	xmlnode_put_attrib (msg, "type", type);
    }

    if (to != NULL) {
	xmlnode_put_attrib (msg, "to", to);
    }

    if (subj != NULL) {
	xmlnode_insert_cdata(xmlnode_insert_tag(msg, "subject"), subj, strlen(subj));
    }

    if (body != NULL) {
	xmlnode_insert_cdata(xmlnode_insert_tag(msg, "body"), body, strlen(body));
    }

    return msg;
}

/**
 * utility for making stream packets (containing the stream header element)
 *
 * @param xmlns the default namespace of the stream (e.g. jabber:client or jabber:server)
 * @param server the domain of the server
 * @return the xmlnode containing the root element of the stream
 */
xmlnode jutil_header(char* xmlns, char* server)
{
     xmlnode result;
     if ((xmlns == NULL)||(server == NULL))
	  return NULL;
     result = xmlnode_new_tag("stream:stream");
     xmlnode_put_attrib(result, "xmlns:stream", "http://etherx.jabber.org/streams");
     xmlnode_put_attrib(result, "xmlns", xmlns);
     xmlnode_put_attrib(result, "to", server);

     return result;
}

/**
 * returns the priority on an available presence packet
 *
 * @param xmlnode the xmlnode containing the presence packet
 * @return the presence priority, -129 for unavailable presences and errors
 */
int jutil_priority(xmlnode x)
{
    char *str;
    int p;

    if(x == NULL)
        return -129;

    if(xmlnode_get_attrib(x,"type") != NULL)
        return -129;

    x = xmlnode_get_tag(x,"priority");
    if(x == NULL)
        return 0;

    str = xmlnode_get_data((x));
    if(str == NULL)
        return 0;

    p = atoi(str);
    /* xmpp-im section 2.2.2.3 */
    return p<-128 ? -128 : p>127 ? 127 : p;
}

/**
 * reverse sender and destination of a packet
 *
 * @param x the xmlnode where sender and receiver should be exchanged
 */
void jutil_tofrom(xmlnode x)
{
    char *to, *from;

    to = xmlnode_get_attrib(x,"to");
    from = xmlnode_get_attrib(x,"from");
    xmlnode_put_attrib(x,"from",to);
    xmlnode_put_attrib(x,"to",from);
}

/**
 * change and xmlnode to be the result xmlnode for the original iq query
 *
 * @param x the xmlnode that should become the result for itself
 * @return the result xmlnode (same as given as parameter x)
 */
xmlnode jutil_iqresult(xmlnode x)
{
    xmlnode cur;

    jutil_tofrom(x);

    xmlnode_put_attrib(x,"type","result");

    /* hide all children of the iq, they go back empty */
    for(cur = xmlnode_get_firstchild(x); cur != NULL; cur = xmlnode_get_nextsibling(cur))
        xmlnode_hide(cur);

    return x;
}

/**
 * get the present time as a textual timestamp in the format YYYYMMDDTHH:MM:SS
 *
 * @note this function is not thread safe
 *
 * @return pointer to a static (!) buffer containing the timestamp (or NULL on failure)
 */
char *jutil_timestamp(void)
{
    time_t t;
    struct tm *new_time;
    static char timestamp[18];
    int ret;

    t = time(NULL);

    if(t == (time_t)-1)
        return NULL;
    new_time = gmtime(&t);

    ret = snprintf(timestamp, 18, "%d%02d%02dT%02d:%02d:%02d", 1900+new_time->tm_year,
                   new_time->tm_mon+1, new_time->tm_mday, new_time->tm_hour,
                   new_time->tm_min, new_time->tm_sec);

    if(ret == -1)
        return NULL;

    return timestamp;
}

/**
 * map a terror structure to a xterror structure
 *
 * terror structures have been used in jabberd14 up to version 1.4.3 but
 * are not able to hold XMPP compliant stanza errors. The xterror
 * structure has been introduced to be XMPP compliant. This function
 * is to ease writting wrappers that accept terror structures and call
 * the real functions that require now xterror structures
 *
 * @param old the terror struct that should be converted
 * @param mapped pointer to the xterror struct that should be filled with the converted error
 */
void jutil_error_map(terror old, xterror *mapped)
{
    mapped->code = old.code;
    if (old.msg == NULL)
	mapped->msg[0] = 0;
    else
	strncpy(mapped->msg, old.msg, sizeof(mapped->msg));

    switch (old.code)
    {
	case 302:
	    strcpy(mapped->type, "modify");
	    strcpy(mapped->condition, "redirect");
	    break;
	case 400:
	    strcpy(mapped->type, "modify");
	    strcpy(mapped->condition, "bad-request");
	    break;
	case 401:
	    strcpy(mapped->type, "auth");
	    strcpy(mapped->condition, "not-authorized");
	    break;
	case 402:
	    strcpy(mapped->type, "auth");
	    strcpy(mapped->condition, "payment-required");
	    break;
	case 403:
	    strcpy(mapped->type, "auth");
	    strcpy(mapped->condition, "forbidden");
	    break;
	case 404:
	    strcpy(mapped->type, "cancel");
	    strcpy(mapped->condition, "item-not-found");
	    break;
	case 405:
	    strcpy(mapped->type, "cancel");
	    strcpy(mapped->condition, "not-allowed");
	    break;
	case 406:
	    strcpy(mapped->type, "modify");
	    strcpy(mapped->condition, "not-acceptable");
	    break;
	case 407:
	    strcpy(mapped->type, "auth");
	    strcpy(mapped->condition, "registration-requited");
	    break;
	case 408:
	    strcpy(mapped->type, "wait");
	    strcpy(mapped->condition, "remote-server-timeout");
	    break;
	case 409:
	    strcpy(mapped->type, "cancel");
	    strcpy(mapped->condition, "conflict");
	    break;
	case 500:
	    strcpy(mapped->type, "wait");
	    strcpy(mapped->condition, "internal-server-error");
	    break;
	case 501:
	    strcpy(mapped->type, "cancel");
	    strcpy(mapped->condition, "feature-not-implemented");
	    break;
	case 502:
	    strcpy(mapped->type, "wait");
	    strcpy(mapped->condition, "service-unavailable");
	    break;
	case 503:
	    strcpy(mapped->type, "cancel");
	    strcpy(mapped->condition, "service-unavailable");
	    break;
	case 504:
	    strcpy(mapped->type, "wait");
	    strcpy(mapped->condition, "remote-server-timeout");
	    break;
	case 510:
	    strcpy(mapped->type, "cancel");
	    strcpy(mapped->condition, "service-unavailable");
	    break;
	default:
	    strcpy(mapped->type, "wait");
	    strcpy(mapped->condition, "undefined-condition");
    }
}

/**
 * update an xmlnode to be the error stanza for itself
 *
 * @param x the xmlnode that should become an stanza error message
 * @param E the structure that holds the error information
 */
void jutil_error_xmpp(xmlnode x, xterror E)
{
    xmlnode err;
    char code[4];

    xmlnode_put_attrib(x, "type", "error");
    err = xmlnode_insert_tag(x, "error");

    snprintf(code, sizeof(code), "%d", E.code);
    xmlnode_put_attrib(err, "code", code);
    if (E.type != NULL)
	xmlnode_put_attrib(err, "type", E.type);
    if (E.condition != NULL)
	xmlnode_put_attrib(xmlnode_insert_tag(err, E.condition), "xmlns", NS_XMPP_STANZAS);
    if (E.msg != NULL)
    {
	xmlnode text;
	text = xmlnode_insert_tag(err, "text");
	xmlnode_put_attrib(text, "xmlns", NS_XMPP_STANZAS);
	xmlnode_insert_cdata(text, E.msg, strlen(E.msg));
    }

    jutil_tofrom(x);
}

/**
 * add a delayed delivery (JEP-0091) element to a message using the
 * present timestamp.
 * If a reason is given, this reason will be added as CDATA to the
 * inserted element
 *
 * @param msg the message where the element should be added
 * @param reason plain text information why the delayed delivery information has been added
 */
void jutil_delay(xmlnode msg, char *reason)
{
    xmlnode delay;

    delay = xmlnode_insert_tag(msg,"x");
    xmlnode_put_attrib(delay,"xmlns",NS_DELAY);
    xmlnode_put_attrib(delay,"from",xmlnode_get_attrib(msg,"to"));
    xmlnode_put_attrib(delay,"stamp",jutil_timestamp());
    if(reason != NULL)
        xmlnode_insert_cdata(delay,reason,strlen(reason));
}
