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

#include <libxode.h>

/* xstream is a way to have a consistent method of handling incoming XML Stream based events... it doesn't handle the generation of an XML Stream, but provides some facilities to help do that */

/******* internal expat callbacks *********/
void _xstream_startElement(xstream xs, const char* name, const char** atts)
{
    pool p;

    /* if xstream is bad, get outa here */
    if(xs->status > XSTREAM_NODE) return;

    if(xs->node == NULL)
    {
        p = pool_heap(5*1024); /* 5k, typically 1-2k each plus copy of self and workspace */
        xs->node = xmlnode_new_tag_pool(p,name);
        xmlnode_put_expat_attribs(xs->node, atts);

        if(xs->status == XSTREAM_ROOT)
        {
            xs->status = XSTREAM_NODE; /* flag status that we're processing nodes now */
            (xs->f)(XSTREAM_ROOT, xs->node, xs->arg); /* send the root, f must free all nodes */
            xs->node = NULL;
        }
    }else{
        xs->node = xmlnode_insert_tag(xs->node, name);
        xmlnode_put_expat_attribs(xs->node, atts);
    }

    /* depth check */
    xs->depth++;
    if(xs->depth > XSTREAM_MAXDEPTH)
        xs->status = XSTREAM_ERR;
}


void _xstream_endElement(xstream xs, const char* name)
{
    xmlnode parent;

    /* if xstream is bad, get outa here */
    if(xs->status > XSTREAM_NODE) return;

    /* if it's already NULL we've received </stream>, tell the app and we're outta here */
    if(xs->node == NULL)
    {
        xs->status = XSTREAM_CLOSE;
        (xs->f)(XSTREAM_CLOSE, NULL, xs->arg);
    }else{
        parent = xmlnode_get_parent(xs->node);

        /* we are the top-most node, feed to the app who is responsible to delete it */
        if(parent == NULL)
            (xs->f)(XSTREAM_NODE, xs->node, xs->arg);

        xs->node = parent;
    }
    xs->depth--;
}


void _xstream_charData(xstream xs, const char *str, int len)
{
    /* if xstream is bad, get outa here */
    if(xs->status > XSTREAM_NODE) return;

    if(xs->node == NULL)
    {
        /* we must be in the root of the stream where CDATA is irrelevant */
        return;
    }

    xmlnode_insert_cdata(xs->node, str, len);
}


void _xstream_cleanup(void *arg)
{
    xstream xs = (xstream)arg;

    xmlnode_free(xs->node); /* cleanup anything left over */
    XML_ParserFree(xs->parser);
}


/* creates a new xstream with given pool, xstream will be cleaned up w/ pool */
xstream xstream_new(pool p, xstream_onNode f, void *arg)
{
    xstream newx;

    if(p == NULL || f == NULL)
    {
        fprintf(stderr,"Fatal Programming Error: xstream_new() was improperly called with NULL.\n");
        return NULL;
    }

    newx = pmalloco(p, sizeof(_xstream));
    newx->p = p;
    newx->f = f;
    newx->arg = arg;

    /* create expat parser and ensure cleanup */
    newx->parser = XML_ParserCreate(NULL);
    XML_SetUserData(newx->parser, (void *)newx);
    XML_SetElementHandler(newx->parser, (void *)_xstream_startElement, (void *)_xstream_endElement);
    XML_SetCharacterDataHandler(newx->parser, (void *)_xstream_charData);
    pool_cleanup(p, _xstream_cleanup, (void *)newx);

    return newx;
}

/* attempts to parse the buff onto this stream firing events to the handler, returns the last known status */
int xstream_eat(xstream xs, char *buff, int len)
{
    char *err;
    xmlnode xerr;
    static char maxerr[] = "maximum node size reached";
    static char deeperr[] = "maximum node depth reached";

    if(xs == NULL)
    {
        fprintf(stderr,"Fatal Programming Error: xstream_eat() was improperly called with NULL.\n");
        return XSTREAM_ERR;
    }

    if(len == 0 || buff == NULL)
        return xs->status;

    if(len == -1) /* easy for hand-fed eat calls */
        len = strlen(buff);

    if(!XML_Parse(xs->parser, buff, len, 0))
    {
        err = (char *)XML_ErrorString(XML_GetErrorCode(xs->parser));
        xs->status = XSTREAM_ERR;
    }else if(pool_size(xmlnode_pool(xs->node)) > XSTREAM_MAXNODE || xs->cdata_len > XSTREAM_MAXNODE){
        err = maxerr;
        xs->status = XSTREAM_ERR;
    }else if(xs->status == XSTREAM_ERR){ /* set within expat handlers */
        err = deeperr;
    }

    /* fire parsing error event, make a node containing the error string */
    if(xs->status == XSTREAM_ERR)
    {
        xerr = xmlnode_new_tag("error");
        xmlnode_insert_cdata(xerr,err,-1);
        (xs->f)(XSTREAM_ERR, xerr, xs->arg);
    }

    return xs->status;
}


/* STREAM CREATION UTILITIES */

/* give a standard template xmlnode to work from */
xmlnode xstream_header(char *namespace, char *to, char *from)
{
    xmlnode x;
    char id[10];

    sprintf(id,"%X",(int)time(NULL));

    x = xmlnode_new_tag("stream:stream");
    xmlnode_put_attrib(x, "xmlns:stream", "http://etherx.jabber.org/streams");
    xmlnode_put_attrib(x, "id", id);
    if(namespace != NULL)
        xmlnode_put_attrib(x, "xmlns", namespace);
    if(to != NULL)
        xmlnode_put_attrib(x, "to", to);
    if(from != NULL)
        xmlnode_put_attrib(x, "from", from);

    return x;
}

/* trim the xmlnode to only the opening header :) [NO CHILDREN ALLOWED] */
char *xstream_header_char(xmlnode x)
{
    spool s;
    char *fixr, *head;

    if(xmlnode_has_children(x))
    {
        fprintf(stderr,"Fatal Programming Error: xstream_header_char() was sent a header with children!\n");
        return NULL;
    }

    s = spool_new(xmlnode_pool(x));
    spooler(s,"<?xml version='1.0'?>",xmlnode2str(x),s);
    head = spool_print(s);
    fixr = strstr(head,"/>");
    *fixr = '>';
    ++fixr;
    *fixr = '\0';

    return head;
}

