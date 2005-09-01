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

/**
 * callback function used for start elements
 *
 * This function is used internally by expat.c as a callback function
 * given to expat. It will create a new xmlnode and add it to the
 * already created xmlnode tree.
 *
 * @param userdata pointer to the parent xmlnode instance (NULL if this function is called for the root note)
 * @param name name of the starting element
 * @param atts attributes that are contained in the start element
 */
void expat_startElement(void* userdata, const char* name, const char** atts)
{
    /* get the xmlnode pointed to by the userdata */
    xmlnode *x = userdata;
    xmlnode current = *x;

    if (current == NULL)
    {
        /* allocate a base node */
        current = xmlnode_new_tag(name);
        xmlnode_put_expat_attribs(current, atts);
        *x = current;
    }
    else
    {
        *x = xmlnode_insert_tag(current, name);
        xmlnode_put_expat_attribs(*x, atts);
    }
}

/**
 * callback function used for end elements
 *
 * This function is used internally by expat.c as a callback function
 * given to expat. It will complete an xmlnode and update the userdata pointer
 * to point to the node that is parent of the next starting element.
 *
 * @param userdata pointer to the current xmlnode
 * @param name name of the ending element (ignored by this function)
 */
void expat_endElement(void* userdata, const char* name)
{
    xmlnode *x = userdata;
    xmlnode current = *x;

    current->complete = 1;
    current = xmlnode_get_parent(current);

    /* if it's NULL we've hit the top folks, otherwise back up a level */
    if(current != NULL)
        *x = current;
}

/**
 * callback function for CDATA nodes
 *
 * This function will insert CDATA in an xmlnode
 *
 * @param userdata pointer to the current xmlnode
 * @param s pointer to the CDATA string (not zero terminated!)
 * @param len length of the CDATA string
 */
void expat_charData(void* userdata, const char* s, int len)
{
    xmlnode *x = userdata;
    xmlnode current = *x;

    xmlnode_insert_cdata(current, s, len);
}

/**
 * create an xmlnode instance (possibly including other xmlnode instances) by parsing a string
 *
 * This function will parse a string containing an XML document and create an xmlnode graph
 *
 * @param str the string containing the XML document (not necessarily zero terminated)
 * @param len the length of the string (without the zero byte, if present)
 * @return the graph of xmlnodes that represent the parsed document, NULL on failure
 */
xmlnode xmlnode_str(char *str, int len)
{
    XML_Parser p;
    xmlnode *x, node; /* pointer to an xmlnode */

    if(NULL == str)
        return NULL;

    x = malloc(sizeof(void *));

    *x = NULL; /* pointer to NULL */
    p = XML_ParserCreate(NULL);
    XML_SetUserData(p, x);
    XML_SetElementHandler(p, expat_startElement, expat_endElement);
    XML_SetCharacterDataHandler(p, expat_charData);
    if(!XML_Parse(p, str, len, 1))
    {
        /*        jdebug(ZONE,"xmlnode_str_error: %s",(char *)XML_ErrorString(XML_GetErrorCode(p)));*/
        xmlnode_free(*x);
        *x = NULL;
    }
    node = *x;
    free(x);
    XML_ParserFree(p);
    return node; /* return the xmlnode x points to */
}

/**
 * create an xmlnode instance (possibly including other xmlnode instances) by parsing a file
 *
 * This function will parse a file containing an XML document and create an xmlnode graph
 *
 * @param file the filename
 * @return the graph of xmlnodes that represent the parsed document, NULL on failure
 */
xmlnode xmlnode_file(char *file)
{
    XML_Parser p;
    xmlnode *x, node; /* pointer to an xmlnode */
    char buf[BUFSIZ];
    int done, fd, len;

    if(NULL == file)
        return NULL;

    fd = open(file,O_RDONLY);
    if(fd < 0)
        return NULL;

    x = malloc(sizeof(void *));

    *x = NULL; /* pointer to NULL */
    p = XML_ParserCreate(NULL);
    XML_SetUserData(p, x);
    XML_SetElementHandler(p, expat_startElement, expat_endElement);
    XML_SetCharacterDataHandler(p, expat_charData);
    do{
        len = read(fd, buf, BUFSIZ);
        done = len < BUFSIZ;
        if(!XML_Parse(p, buf, len, done))
        {
            /*            jdebug(ZONE,"xmlnode_file_parseerror: %s",(char *)XML_ErrorString(XML_GetErrorCode(p)));*/
            xmlnode_free(*x);
            *x = NULL;
            done = 1;
        }
    }while(!done);

    node = *x;
    XML_ParserFree(p);
    free(x);
    close(fd);
    return node; /* return the xmlnode x points to */
}

/**
 * write an xmlnode to a file (without a size limit)
 *
 * @param file the target file
 * @param node the xmlnode that should be written
 * @return 1 on success, -1 on failure
 */
int xmlnode2file(char *file, xmlnode node)
{
    return xmlnode2file_limited(file, node, 0);
}

/**
 * write an xmlnode to a file, limited by size
 *
 * @param file the target file
 * @param node the xmlnode that should be written
 * @param sizelimit the maximum length of the file to be written
 * @return 1 on success, 0 if failed due to size limit, -1 on failure
 */
int xmlnode2file_limited(char *file, xmlnode node, size_t sizelimit)
{
    char *doc, *ftmp;
    int fd, i;
    size_t doclen;

    if(file == NULL || node == NULL)
        return -1;

    ftmp = spools(xmlnode_pool(node),file,".t.m.p",xmlnode_pool(node));
    fd = open(ftmp, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if(fd < 0)
        return -1;

    doc = xmlnode2str(node);
    doclen = strlen(doc);

    if (sizelimit > 0 && doclen > sizelimit)
    {
	close(fd);
	return 0;
    }

    i = write(fd,doc,doclen);
    if(i < 0)
        return -1;

    close(fd);

    if(rename(ftmp,file) < 0)
    {
        unlink(ftmp);
        return -1;
    }
    return 1;
}

/**
 * append attributes in the expat format to an existing xmlnode
 *
 * @param owner where to add the attributes
 * @param atts the attributes in expat format (even indexes are the attribute names, odd indexes the values)
 */
void xmlnode_put_expat_attribs(xmlnode owner, const char** atts)
{
    int i = 0;
    if (atts == NULL) return;
    while (atts[i] != '\0')
    {
        xmlnode_put_attrib(owner, atts[i], atts[i+1]);
        i += 2;
    }
}
