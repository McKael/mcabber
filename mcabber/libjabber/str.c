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

#include "libxode.h"

char *j_strdup(const char *str)
{
    if(str == NULL)
        return NULL;
    else
        return strdup(str);
}

char *j_strcat(char *dest, char *txt)
{
    if(!txt) return(dest);

    while(*txt)
        *dest++ = *txt++;
    *dest = '\0';

    return(dest);
}

int j_strcmp(const char *a, const char *b)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strcmp(a, b);
}

int j_strcasecmp(const char *a, const char *b)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strcasecmp(a, b);
}

int j_strncmp(const char *a, const char *b, int i)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strncmp(a, b, i);
}

int j_strncasecmp(const char *a, const char *b, int i)
{
    if(a == NULL || b == NULL)
        return -1;
    else
        return strncasecmp(a, b, i);
}

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
        if((int)arg == (int)s)
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
        if((int)arg == (int)p)
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

    for(i=0;i<strlen(buf);i++)
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

void str_b64decode(char* str)
{
    char *cur;
    int d, dlast, phase;
    unsigned char c;
    static int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
    };

    phase = 0;
    for (cur = str; *cur != '\0'; ++cur )
    {
        d = table[(int)*cur];
        if(d != -1)
        {
            switch(phase)
            {
            case 0:
                ++phase;
                break;
            case 1:
                c = ((dlast << 2) | ((d & 0x30) >> 4));
                *str++ = c;
                ++phase;
                break;
            case 2:
                c = (((dlast & 0xf) << 4) | ((d & 0x3c) >> 2));
                *str++ = c;
                ++phase;
                break;
            case 3:
                c = (((dlast & 0x03 ) << 6) | d);
                *str++ = c;
                phase = 0;
                break;
            }
            dlast = d;
        }
    }
    *str = '\0';
}
