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
 *  Jabber
 *  Copyright (C) 1998-1999 The Jabber Team http://jabber.org/
 */

#include "jabber.h"

jid jid_safe(jid id)
{
    char *str;

    if(strlen(id->server) == 0 || strlen(id->server) > 255)
	return NULL;

    /* lowercase the hostname, make sure it's valid characters */
    for(str = id->server; *str != '\0'; str++)
    {
	*str = tolower(*str);
	if(!(isalnum(*str) || *str == '.' || *str == '-' || *str == '_')) return NULL;
    }

    /* cut off the user */
    if(id->user != NULL && strlen(id->user) > 64)
	id->user[64] = '\0';

    /* check for low and invalid ascii characters in the username */
    if(id->user != NULL)
	for(str = id->user; *str != '\0'; str++)
	    if(*str <= 32 || *str == ':' || *str == '@' || *str == '<' || *str == '>' || *str == '\'' || *str == '"' || *str == '&') return NULL;

    return id;
}

jid jid_new(pool p, char *idstr)
{
    char *server, *resource, *type, *str;
    jid id;

    if(p == NULL || idstr == NULL || strlen(idstr) == 0)
	return NULL;

    /* user@server/resource */

    str = pstrdup(p, idstr);

    id = pmalloc(p,sizeof(struct jid_struct));
    id->full = id->server = id->user = id->resource = NULL;
    id->p = p;
    id->next = NULL;

    resource = strstr(str,"/");
    if(resource != NULL)
    {
	*resource = '\0';
	++resource;
	if(strlen(resource) > 0)
	    id->resource = resource;
    }else{
	resource = str + strlen(str); /* point to end */
    }

    type = strstr(str,":");
    if(type != NULL && type < resource)
    {
	*type = '\0';
	++type;
	str = type; /* ignore the type: prefix */
    }

    server = strstr(str,"@");
    if(server == NULL || server > resource)
    { /* if there's no @, it's just the server address */
	id->server = str;
    }else{
	*server = '\0';
	++server;
	id->server = server;
	if(strlen(str) > 0)
	    id->user = str;
    }

    return jid_safe(id);
}

void jid_set(jid id, char *str, int item)
{
    char *old;

    if(id == NULL)
	return;

    /* invalidate the cached copy */
    id->full = NULL;

    switch(item)
    {
    case JID_RESOURCE:
	if(str != NULL && strlen(str) != 0)
	    id->resource = pstrdup(id->p, str);
	else
	    id->resource = NULL;
	break;
    case JID_USER:
	old = id->user;
	if(str != NULL && strlen(str) != 0)
	    id->user = pstrdup(id->p, str);
	else
	    id->user = NULL;
	if(jid_safe(id) == NULL)
	    id->user = old; /* revert if invalid */
	break;
    case JID_SERVER:
	old = id->server;
	id->server = pstrdup(id->p, str);
	if(jid_safe(id) == NULL)
	    id->server = old; /* revert if invalid */
	break;
    }

}

char *jid_full(jid id)
{
    spool s;

    if(id == NULL)
	return NULL;

    /* use cached copy */
    if(id->full != NULL)
	return id->full;

    s = spool_new(id->p);

    if(id->user != NULL)
	spooler(s, id->user,"@",s);

    spool_add(s, id->server);

    if(id->resource != NULL)
	spooler(s, "/",id->resource,s);

    id->full = spool_print(s);
    return id->full;
}

/* parses a /resource?name=value&foo=bar into an xmlnode representing <resource name="value" foo="bar"/> */
xmlnode jid_xres(jid id)
{
    char *cur, *qmark, *amp, *eq;
    xmlnode x;

    if(id == NULL || id->resource == NULL) return NULL;

    cur = pstrdup(id->p, id->resource);
    qmark = strstr(cur, "?");
    if(qmark == NULL) return NULL;
    *qmark = '\0';
    qmark++;

    x = _xmlnode_new(id->p, cur, NTYPE_TAG);

    cur = qmark;
    while(cur != '\0')
    {
	eq = strstr(cur, "=");
	if(eq == NULL) break;
	*eq = '\0';
	eq++;

	amp = strstr(eq, "&");
	if(amp != NULL)
	{
	    *amp = '\0';
	    amp++;
	}

	xmlnode_put_attrib(x,cur,eq);

	if(amp != NULL)
	    cur = amp;
	else
	    break;
    }

    return x;
}

/* local utils */
int _jid_nullstrcmp(char *a, char *b)
{
    if(a == NULL && b == NULL) return 0;
    if(a == NULL || b == NULL) return -1;
    return strcmp(a,b);
}
int _jid_nullstrcasecmp(char *a, char *b)
{
    if(a == NULL && b == NULL) return 0;
    if(a == NULL || b == NULL) return -1;
    return strcasecmp(a,b);
}

int jid_cmp(jid a, jid b)
{
    if(a == NULL || b == NULL)
	return -1;

    if(_jid_nullstrcmp(a->resource, b->resource) != 0) return -1;
    if(_jid_nullstrcasecmp(a->user, b->user) != 0) return -1;
    if(_jid_nullstrcmp(a->server, b->server) != 0) return -1;

    return 0;
}

/* suggested by Anders Qvist <quest@valdez.netg.se> */
int jid_cmpx(jid a, jid b, int parts)
{
    if(a == NULL || b == NULL)
	return -1;

    if(parts & JID_RESOURCE && _jid_nullstrcmp(a->resource, b->resource) != 0) return -1;
    if(parts & JID_USER && _jid_nullstrcasecmp(a->user, b->user) != 0) return -1;
    if(parts & JID_SERVER && _jid_nullstrcmp(a->server, b->server) != 0) return -1;

    return 0;
}

/* makes a copy of b in a's pool, requires a valid a first! */
jid jid_append(jid a, jid b)
{
    jid next;

    if(a == NULL)
	return NULL;

    if(b == NULL)
	return a;

    next = a;
    while(next != NULL)
    {
	/* check for dups */
	if(jid_cmp(next,b) == 0)
	    break;
	if(next->next == NULL)
	    next->next = jid_new(a->p,jid_full(b));
	next = next->next;
    }
    return a;
}

xmlnode jid_nodescan(jid id, xmlnode x)
{
    xmlnode cur;
    pool p;
    jid tmp;

    if(id == NULL || xmlnode_get_firstchild(x) == NULL) return NULL;

    p = pool_new();
    for(cur = xmlnode_get_firstchild(x); cur != NULL; cur = xmlnode_get_nextsibling(cur))
    {
	if(xmlnode_get_type(cur) != NTYPE_TAG) continue;

	tmp = jid_new(p,xmlnode_get_attrib(cur,"jid"));
	if(tmp == NULL) continue;

	if(jid_cmp(tmp,id) == 0) break;
    }
    pool_free(p);

    return cur;
}
