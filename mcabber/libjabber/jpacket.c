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
 * @file jpacket.c
 * @brief a jpacket is a wrapper around an xmlnode that contains an XMPP stanza
 *
 * A jpacket adds some variables to an xmlnode that contains a stanza, so that
 * jabberd is able to cache information on the stanza type (message, presence, iq)
 * that is contained in this jpacket and to further classify the presence stanzas.
 * It also adds some pointers to important data inside the xmlnode, that has
 * to be accessed often (e.g. sender and receiver of a stanza).
 */

#include "jabber.h"

/**
 * create a new jpacket by wrapping an xmlnode
 *
 * @param x the xmlnode that should be wrapped
 * @return the newly created jpacket (NULL on failure)
 */
jpacket jpacket_new(xmlnode x)
{
    jpacket p;

    if(x == NULL)
        return NULL;

    p = pmalloc(xmlnode_pool(x),sizeof(_jpacket));
    p->x = x;

    return jpacket_reset(p);
}

/**
 * recalculate the information the jpacket holds about the stanza
 *
 * @param p the packet that should get its information recalculated
 * @return the jpacket (as given as the p parameter)
 */
jpacket jpacket_reset(jpacket p)
{
    char *val;
    xmlnode x;

    x = p->x;
    memset(p,0,sizeof(_jpacket));
    p->x = x;
    p->p = xmlnode_pool(x);

    if(strncmp(xmlnode_get_name(x),"message",7) == 0) {
        p->type = JPACKET_MESSAGE;
    } else if(strncmp(xmlnode_get_name(x),"presence",8) == 0) {
        p->type = JPACKET_PRESENCE;
        val = xmlnode_get_attrib(x, "type");
        if(val == NULL)
            p->subtype = JPACKET__AVAILABLE;
        else if(strcmp(val,"unavailable") == 0)
            p->subtype = JPACKET__UNAVAILABLE;
        else if(strcmp(val,"probe") == 0)
            p->subtype = JPACKET__PROBE;
        else if(strcmp(val,"error") == 0)
            p->subtype = JPACKET__ERROR;
        else if(strcmp(val,"invisible") == 0)
            p->subtype = JPACKET__INVISIBLE;
        else if(*val == 's' || *val == 'u')
            p->type = JPACKET_S10N;
        else if(strcmp(val,"available") == 0) {
	    /* someone is using type='available' which is frowned upon */
	    /* XXX better reject this presence? */
            xmlnode_hide_attrib(x,"type");
            p->subtype = JPACKET__AVAILABLE;
        } else
            p->type = JPACKET_UNKNOWN;
    } else if(strncmp(xmlnode_get_name(x),"iq",2) == 0) {
        p->type = JPACKET_IQ;
        p->iq = xmlnode_get_tag(x,"?xmlns");
        p->iqns = xmlnode_get_attrib(p->iq,"xmlns");
    }

    /* set up the jids if any, flag packet as unknown if they are unparseable */
    val = xmlnode_get_attrib(x,"to");
    if(val != NULL)
        if((p->to = jid_new(p->p, val)) == NULL)
            p->type = JPACKET_UNKNOWN;
    val = xmlnode_get_attrib(x,"from");
    if(val != NULL)
        if((p->from = jid_new(p->p, val)) == NULL)
            p->type = JPACKET_UNKNOWN;

    return p;
}

/**
 * get the subtype of a jpacket
 *
 * @param p the jpacket for which the caller wants to know the subtype
 * @return the subtype of the jpacket (one of the JPACKET__* constants)
 */
int jpacket_subtype(jpacket p)
{
    char *type;
    int ret = p->subtype;

    if(ret != JPACKET__UNKNOWN)
        return ret;

    ret = JPACKET__NONE; /* default, when no type attrib is specified */
    type = xmlnode_get_attrib(p->x, "type");
    if(j_strcmp(type,"error") == 0)
        ret = JPACKET__ERROR;
    else
        switch(p->type)
        {
        case JPACKET_MESSAGE:
            if(j_strcmp(type,"chat") == 0)
                ret = JPACKET__CHAT;
            else if(j_strcmp(type,"groupchat") == 0)
                ret = JPACKET__GROUPCHAT;
            else if(j_strcmp(type,"headline") == 0)
                ret = JPACKET__HEADLINE;
            break;
        case JPACKET_S10N:
            if(j_strcmp(type,"subscribe") == 0)
                ret = JPACKET__SUBSCRIBE;
            else if(j_strcmp(type,"subscribed") == 0)
                ret = JPACKET__SUBSCRIBED;
            else if(j_strcmp(type,"unsubscribe") == 0)
                ret = JPACKET__UNSUBSCRIBE;
            else if(j_strcmp(type,"unsubscribed") == 0)
                ret = JPACKET__UNSUBSCRIBED;
            break;
        case JPACKET_IQ:
            if(j_strcmp(type,"get") == 0)
                ret = JPACKET__GET;
            else if(j_strcmp(type,"set") == 0)
                ret = JPACKET__SET;
            else if(j_strcmp(type,"result") == 0)
                ret = JPACKET__RESULT;
            break;
        }

    p->subtype = ret;
    return ret;
}
