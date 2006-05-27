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
 * @file str.c
 * @brief utilities for string handling
 *
 * This file contains utility functions for string handling:
 * - NULL pointer save versions of many functions in string.c
 * - string spools
 * - functions to (un)escape strings for XML usage
 *
 * String spools allow to create a string by concatenating several smaller strings
 * and the spool implementation is allocating the neccessary memory using memory pools.
 */

#include "libxode.h"

/**
 * NULL pointer save version of strdup()
 *
 * @param str the string the should be duplicated
 * @return the duplicated string
 */
char *j_strdup(const char *str)
{
    if(str == NULL)
        return NULL;
    else
        return strdup(str);
}

/**
 * NULL pointer save version of strcat()
 *
 * @note the return value of j_strcat() is not compatible with the return value of strcat()
 *
 * @todo check if the behaviour of the return value is intended
 *
 * @param dest where to append the string
 * @param txt what to append
 * @return dest if txt contains a NULL pointer, pointer to the terminating zero byte of the result else
 */
char *j_strcat(char *dest, char *txt)
{
    if(!txt) return(dest);

    while(*txt)
        *dest++ = *txt++;
    *dest = '\0';

    return(dest);
}

/**
 * NULL pointer save version of strcmp
 *
 * If one of the parameters contains a NULL pointer, the string is considered to be unequal.
 *
 * @note the return value is not compatible with strcmp()
 *
 * @param a the one string
 * @param b the other string
 * @return 0 if the strings are equal, -1 if the strings are not equal
 */
int j_strcmp(const char *a, const char *b)
{
    if(a == NULL || b == NULL)
        return -1;

    while(*a == *b && *a != '\0' && *b != '\0'){ a++; b++; }

    if(*a == *b) return 0;

    return -1;
}

/**
 * NULL pointer save version of strcasecmp()
 *
 * If one of the parameters contains a NULL pointer, the string is considered to be unequal
 *
 * @param a the one string
 * @param b the other string
 * @return 0 if the strings are equal, non zero else
 */
int j_strcasecmp(const char *a, const char *b)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strcasecmp(a, b);
}

/**
 * NULL pointer save version of strncmp()
 *
 * If one of the parameters contains a NULL pointer, the string is considered to be unequal
 *
 * @param a the first string
 * @param b the second string
 * @param i how many characters to compare at most
 * @return 0 if the strings are equal (within the given length limitation), non zero else
 */
int j_strncmp(const char *a, const char *b, int i)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strncmp(a, b, i);
}

/**
 * NULL pointer save version of strncasecmp()
 *
 * If one of the parameters contains a NULL pointer, the string is considered to be unequal
 *
 * @param a the first string
 * @param b the second string
 * @param i how many characters to compare at most
 * @return 0 if the strings are equal (within the given length limitation), non zero else
 */
int j_strncasecmp(const char *a, const char *b, int i)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strncasecmp(a, b, i);
}

/**
 * NULL pointer save version of strlen
 *
 * If the parameter contains a NULL pointer, 0 is returned
 *
 * @param a the string for which the length should be calculated
 * @return 0 if a==NULL, length of the string else
 */
int j_strlen(const char *a)
{
    if(a == NULL)
        return 0;
    else
        return strlen(a);
}

int j_atoi(const char *a, int def)
{
    if(a == NULL)
        return def;
    else
        return atoi(a);
}

spool spool_new(pool p)
{
    spool s;

    s = pmalloc(p, sizeof(struct spool_struct));
    s->p = p;
    s->len = 0;
    s->last = NULL;
    s->first = NULL;
    return s;
}

void spool_add(spool s, char *str)
{
    struct spool_node *sn;
    int len;

    if(str == NULL)
        return;

    len = strlen(str);
    if(len == 0)
        return;

    sn = pmalloc(s->p, sizeof(struct spool_node));
    sn->c = pstrdup(s->p, str);
    sn->next = NULL;

    s->len += len;
    if(s->last != NULL)
        s->last->next = sn;
    s->last = sn;
    if(s->first == NULL)
        s->first = sn;
}

void spooler(spool s, ...)
{
    va_list ap;
    char *arg = NULL;

    if(s == NULL)
        return;

    va_start(ap, s);

    /* loop till we hit our end flag, the first arg */
    while(1)
    {
        arg = va_arg(ap,char *);
        if((spool)arg == s)
            break;
        else
            spool_add(s, arg);
    }

    va_end(ap);
}

char *spool_print(spool s)
{
    char *ret,*tmp;
    struct spool_node *next;

    if(s == NULL || s->len == 0 || s->first == NULL)
        return NULL;

    ret = pmalloc(s->p, s->len + 1);
    *ret = '\0';

    next = s->first;
    tmp = ret;
    while(next != NULL)
    {
        tmp = j_strcat(tmp,next->c);
        next = next->next;
    }

    return ret;
}

/* convenience :) */
char *spools(pool p, ...)
{
    va_list ap;
    spool s;
    char *arg = NULL;

    if(p == NULL)
        return NULL;

    s = spool_new(p);

    va_start(ap, p);

    /* loop till we hit our end flag, the first arg */
    while(1)
    {
        arg = va_arg(ap,char *);
        if((pool)arg == p)
            break;
        else
            spool_add(s, arg);
    }

    va_end(ap);

    return spool_print(s);
}


char *strunescape(pool p, char *buf)
{
    int i,j=0;
    char *temp;

    if (p == NULL || buf == NULL) return(NULL);

    if (strchr(buf,'&') == NULL) return(buf);

    temp = pmalloc(p,strlen(buf)+1);

    if (temp == NULL) return(NULL);

    for(i=0;i<(int)strlen(buf);i++)
    {
        if (buf[i]=='&')
        {
            if (strncmp(&buf[i],"&amp;",5)==0)
            {
                temp[j] = '&';
                i += 4;
            } else if (strncmp(&buf[i],"&quot;",6)==0) {
                temp[j] = '\"';
                i += 5;
            } else if (strncmp(&buf[i],"&apos;",6)==0) {
                temp[j] = '\'';
                i += 5;
            } else if (strncmp(&buf[i],"&lt;",4)==0) {
                temp[j] = '<';
                i += 3;
            } else if (strncmp(&buf[i],"&gt;",4)==0) {
                temp[j] = '>';
                i += 3;
            }
        } else {
            temp[j]=buf[i];
        }
        j++;
    }
    temp[j]='\0';
    return(temp);
}


char *strescape(pool p, char *buf)
{
    int i,j,oldlen,newlen;
    char *temp;

    if (p == NULL || buf == NULL) return(NULL);

    oldlen = newlen = strlen(buf);
    for(i=0;i<oldlen;i++)
    {
        switch(buf[i])
        {
        case '&':
            newlen+=5;
            break;
        case '\'':
            newlen+=6;
            break;
        case '\"':
            newlen+=6;
            break;
        case '<':
            newlen+=4;
            break;
        case '>':
            newlen+=4;
            break;
        }
    }

    if(oldlen == newlen) return buf;

    temp = pmalloc(p,newlen+1);

    if (temp==NULL) return(NULL);

    for(i=j=0;i<oldlen;i++)
    {
        switch(buf[i])
        {
        case '&':
            memcpy(&temp[j],"&amp;",5);
            j += 5;
            break;
        case '\'':
            memcpy(&temp[j],"&apos;",6);
            j += 6;
            break;
        case '\"':
            memcpy(&temp[j],"&quot;",6);
            j += 6;
            break;
        case '<':
            memcpy(&temp[j],"&lt;",4);
            j += 4;
            break;
        case '>':
            memcpy(&temp[j],"&gt;",4);
            j += 4;
            break;
        default:
            temp[j++] = buf[i];
        }
    }
    temp[j] = '\0';
    return temp;
}

char *zonestr(char *file, int line)
{
    static char buff[64];
    int i;

    i = snprintf(buff,63,"%s:%d",file,line);
    buff[i] = '\0';

    return buff;
}
