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

/* these aren't the most efficient things in the world, a hash optimized for tiny spaces would be far better */

ppdb _ppdb_new(pool p, jid id)
{
    ppdb ret;
    ret = pmalloc(p,sizeof(_ppdb));
    ret->p = p;
    ret->pri = -1;
    ret->next = NULL;
    ret->user = NULL;
    ret->x = NULL;
    ret->id = jid_new(p,jid_full(id));

    return ret;
}

ppdb _ppdb_get(ppdb db, jid id)
{
    ppdb cur;

    if(db == NULL || id == NULL) return NULL;

    for(cur = db->next; cur != NULL; cur = cur->next)
	if(jid_cmp(cur->id,id) == 0) return cur;

    return NULL;
}

ppdb ppdb_insert(ppdb db, jid id, xmlnode x)
{
    char *res;
    ppdb cur, curu;
    pool p;

    if(id == NULL || id->server == NULL || x == NULL)
	return db;

    /* new ppdb list dummy holder */
    if(db == NULL)
    {
	p = pool_heap(1024);
	db = _ppdb_new(p,id);
    }

    cur = _ppdb_get(db,id);

    /* just update it */
    if(cur != NULL)
    {
	xmlnode_free(cur->x);
	cur->x = xmlnode_dup(x);
	cur->pri = jutil_priority(x);
	return db;
    }

    /* make an entry for it */
    cur = _ppdb_new(db->p,id);
    cur->x = xmlnode_dup(x);
    cur->pri = jutil_priority(x);
    cur->next = db->next;
    db->next = cur;

    /* this is a presence from a resource, make an entry for just the user */
    if(id->user != NULL && id->resource != NULL)
    {
	/* modify the id to just user@host */
	res = id->resource;
	jid_set(id,NULL,JID_RESOURCE);
	curu = _ppdb_get(db,id);

	/* no user entry, make one */
	if(curu == NULL)
	{
	    curu = _ppdb_new(db->p,id);
	    curu->next = db->next;
	    db->next = curu;
	}

	/* restore the id */
	jid_set(id,res,JID_RESOURCE);

	/* insert this resource into the user list */
	cur->user = curu->user;
	curu->user = cur;
    }

    return db;
}

xmlnode ppdb_primary(ppdb db, jid id)
{
    ppdb cur, top;

    if(db == NULL || id == NULL) return NULL;

    cur = _ppdb_get(db,id);

    if(cur == NULL) return NULL;

    /* not user@host check, just return */
    if(id->user == NULL || id->resource != NULL) return cur->x;

    top = cur;
    for(cur = cur->user; cur != NULL; cur = cur->user)
	if(cur->pri >= top->pri) top = cur;

    if(top != NULL && top->pri >= 0) return top->x;

    return NULL;
}

/* return the presence for the id, successive calls return all of the known resources for a user@host address */
xmlnode ppdb_get(ppdb db, jid id)
{
    static ppdb last = NULL;
    ppdb cur;

    if(db == NULL || id == NULL) return NULL;

    /* MODE: if this is NOT just user@host addy, return just the single entry */
    if(id->user == NULL || id->resource != NULL)
    {
	/* we were just here, return now */
	if(last != NULL)
	{
	    last = NULL;
	    return NULL;
	}

	last = _ppdb_get(db,id);
	if(last != NULL)
	    return last->x;
	else
	    return NULL;
    }

    /* handle looping for user@host */

    /* we're already in the loop */
    if(last != NULL)
    {
	/* this is the last entry in the list */
	if(last->user == NULL)
	{
	    last = NULL;
	    return NULL;
	}

	last = last->user;
	return last->x;
    }

    /* start a new loop */
    cur = _ppdb_get(db,id);

    if(cur == NULL) return NULL;

    last = cur->user;
    if(last != NULL)
	return last->x;
    else
	return NULL;
}


void ppdb_free(ppdb db)
{
    ppdb cur;

    if(db == NULL) return;

    for(cur = db; cur != NULL; cur = cur->next)
	xmlnode_free(cur->x);

    pool_free(db->p);
}

