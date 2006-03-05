/*
 * jabberd - Jabber Open Source Server
 * Copyright (c) 2002 Jeremie Miller, Thomas Muldowney,
 *                    Ryan Eatmon, Robert Norris
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 */

#ifdef HAVE_CONFIG_H
#   include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <syslog.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "xmlparse.h"

/*
**  Arrange to use either varargs or stdargs
*/

#define MAXSHORTSTR    203        /* max short string length */
#define QUAD_T    unsigned long long

#ifdef __STDC__

#include <stdarg.h>

# define VA_LOCAL_DECL  va_list ap;
# define VA_START(f)    va_start(ap, f)
# define VA_END         va_end(ap)

#else /* __STDC__ */

# include <varargs.h>

# define VA_LOCAL_DECL  va_list ap;
# define VA_START(f)    va_start(ap)
# define VA_END         va_end(ap)

#endif /* __STDC__ */


#ifndef INCL_LIBXODE_H
#define INCL_LIBXODE_H

#ifdef __cplusplus
extern "C" {
#endif


#ifndef HAVE_SNPRINTF
extern int ap_snprintf(char *, size_t, const char *, ...);
#define snprintf ap_snprintf
#endif

#ifndef HAVE_VSNPRINTF
extern int ap_vsnprintf(char *, size_t, const char *, va_list ap);
#define vsnprintf ap_vsnprintf
#endif

#define ZONE zonestr(__FILE__,__LINE__)
char *zonestr(char *file, int line);

/* --------------------------------------------------------- */
/*                                                           */
/* Pool-based memory management routines                     */
/*                                                           */
/* --------------------------------------------------------- */

#undef POOL_DEBUG
/*
 flip these, this should be a prime number for top # of pools debugging
#define POOL_DEBUG 40009
*/

/* pheap - singular allocation of memory */
struct pheap
{
    void *block;
    int size, used;
};

/* pool_cleaner - callback type which is associated
   with a pool entry; invoked when the pool entry is
   free'd */
typedef void (*pool_cleaner)(void *arg);

/* pfree - a linked list node which stores an
   allocation chunk, plus a callback */
struct pfree
{
    pool_cleaner f;
    void *arg;
    struct pheap *heap;
    struct pfree *next;
};

/* pool - base node for a pool. Maintains a linked list
   of pool entries (pfree) */
typedef struct pool_struct
{
    int size;
    struct pfree *cleanup;
    struct pheap *heap;
#ifdef POOL_DEBUG
    char name[8], zone[32];
    int lsize;
} _pool, *pool;
#define pool_new() _pool_new(__FILE__,__LINE__)
#define pool_heap(i) _pool_new_heap(i,__FILE__,__LINE__)
#else
} _pool, *pool;
#define pool_heap(i) _pool_new_heap(i, NULL, 0)
#define pool_new() _pool_new(NULL, 0)
#endif

pool _pool_new(char *zone, int line); /* new pool :) */
pool _pool_new_heap(int size, char *zone, int line); /* creates a new memory pool with an initial heap size */
void *pmalloc(pool p, int size); /* wrapper around malloc, takes from the pool, cleaned up automatically */
void *pmalloc_x(pool p, int size, char c); /* Wrapper around pmalloc which prefils buffer with c */
void *pmalloco(pool p, int size); /* YAPW for zeroing the block */
char *pstrdup(pool p, const char *src); /* wrapper around strdup, gains mem from pool */
void pool_stat(int full); /* print to stderr the changed pools and reset */
char *pstrdupx(pool p, const char *src); /* temp stub */
void pool_cleanup(pool p, pool_cleaner f, void *arg); /* calls f(arg) before the pool is freed during cleanup */
void pool_free(pool p); /* calls the cleanup functions, frees all the data on the pool, and deletes the pool itself */
int pool_size(pool p); /* returns total bytes allocated in this pool */




/* --------------------------------------------------------- */
/*                                                           */
/* Socket helper stuff                                       */
/*                                                           */
/* --------------------------------------------------------- */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

#define NETSOCKET_SERVER 0
#define NETSOCKET_CLIENT 1
#define NETSOCKET_UDP 2

#ifndef WIN32
int make_netsocket(u_short port, char *host, int type, int ssl);
int make_nb_netsocket(u_short port, char *host, int type, int ssl, int * state);
void change_socket_to_blocking(int socket);
struct in_addr *make_addr(char *host);
int set_fd_close_on_exec(int fd, int flag);
#endif


/* --------------------------------------------------------- */
/*                                                           */
/* String management routines                                */
/*                                                           */
/* --------------------------------------------------------- */
char *j_strdup(const char *str); /* provides NULL safe strdup wrapper */
char *j_strcat(char *dest, char *txt); /* strcpy() clone */
int j_strcmp(const char *a, const char *b); /* provides NULL safe strcmp wrapper */
int j_strcasecmp(const char *a, const char *b); /* provides NULL safe strcasecmp wrapper */
int j_strncmp(const char *a, const char *b, int i); /* provides NULL safe strncmp wrapper */
int j_strncasecmp(const char *a, const char *b, int i); /* provides NULL safe strncasecmp wrapper */
int j_strlen(const char *a); /* provides NULL safe strlen wrapper */
int j_atoi(const char *a, int def); /* checks for NULL and uses default instead, convienence */
void str_b64decode(char *str); /* what it says */


/* --------------------------------------------------------- */
/*                                                           */
/* SHA calculations                                          */
/*                                                           */
/* --------------------------------------------------------- */
#if (SIZEOF_INT == 4)
typedef unsigned int uint32;
#elif (SIZEOF_SHORT == 4)
typedef unsigned short uint32;
#else
typedef unsigned int uint32;
#endif /* HAVEUINT32 */

int sha_hash(int *data, int *hash);
int sha_init(int *hash);
char *shahash(char *str);       /* NOT THREAD SAFE */
void shahash_r(const char* str, char hashbuf[40]); /* USE ME */

int strprintsha(char *dest, int *hashval);


/* --------------------------------------------------------- */
/*                                                           */
/* Hashtable functions                                       */
/*                                                           */
/* --------------------------------------------------------- */
typedef int (*KEYHASHFUNC)(const void *key);
typedef int (*KEYCOMPAREFUNC)(const void *key1, const void *key2);
typedef int (*TABLEWALKFUNC)(void *user_data, const void *key, void *data);

typedef void *HASHTABLE;

HASHTABLE ghash_create(int buckets, KEYHASHFUNC hash, KEYCOMPAREFUNC cmp);
void ghash_destroy(HASHTABLE tbl);
void *ghash_get(HASHTABLE tbl, const void *key);
int ghash_put(HASHTABLE tbl, const void *key, void *value);
int ghash_remove(HASHTABLE tbl, const void *key);
int ghash_walk(HASHTABLE tbl, TABLEWALKFUNC func, void *user_data);
int str_hash_code(const char *s);


/* --------------------------------------------------------- */
/*                                                           */
/* XML escaping utils                                        */
/*                                                           */
/* --------------------------------------------------------- */
char *strescape(pool p, char *buf); /* Escape <>&'" chars */
char *strunescape(pool p, char *buf);


/* --------------------------------------------------------- */
/*                                                           */
/* String pools (spool) functions                            */
/*                                                           */
/* --------------------------------------------------------- */
struct spool_node
{
    char *c;
    struct spool_node *next;
};

typedef struct spool_struct
{
    pool p;
    int len;
    struct spool_node *last;
    struct spool_node *first;
} *spool;

spool spool_new(pool p); /* create a string pool */
void spooler(spool s, ...); /* append all the char * args to the pool, terminate args with s again */
char *spool_print(spool s); /* return a big string */
void spool_add(spool s, char *str); /* add a single string to the pool */
char *spools(pool p, ...); /* wrap all the spooler stuff in one function, the happy fun ball! */


/* --------------------------------------------------------- */
/*                                                           */
/* xmlnodes - Document Object Model                          */
/*                                                           */
/* --------------------------------------------------------- */
#define NTYPE_TAG    0
#define NTYPE_ATTRIB 1
#define NTYPE_CDATA  2

#define NTYPE_LAST   2
#define NTYPE_UNDEF  -1

/* --------------------------------------------------------------------------
   Node structure. Do not use directly! Always use accessor macros
   and methods!
   -------------------------------------------------------------------------- */
typedef struct xmlnode_t
{
     char*               name;
     unsigned short      type;
     char*               data;
     int                 data_sz;
     int                 complete;
     pool               p;
     struct xmlnode_t*  parent;
     struct xmlnode_t*  firstchild;
     struct xmlnode_t*  lastchild;
     struct xmlnode_t*  prev;
     struct xmlnode_t*  next;
     struct xmlnode_t*  firstattrib;
     struct xmlnode_t*  lastattrib;
} _xmlnode, *xmlnode;

/* Node creation routines */
xmlnode  xmlnode_wrap(xmlnode x,const char* wrapper);
xmlnode  xmlnode_new_tag(const char* name);
xmlnode  xmlnode_new_tag_pool(pool p, const char* name);
xmlnode  xmlnode_insert_tag(xmlnode parent, const char* name);
xmlnode  xmlnode_insert_cdata(xmlnode parent, const char* CDATA, unsigned int size);
xmlnode  xmlnode_insert_tag_node(xmlnode parent, xmlnode node);
void     xmlnode_insert_node(xmlnode parent, xmlnode node);
xmlnode  xmlnode_str(char *str, int len);
xmlnode  xmlnode_file(char *file);
xmlnode  xmlnode_dup(xmlnode x); /* duplicate x */
xmlnode  xmlnode_dup_pool(pool p, xmlnode x);

/* Node Memory Pool */
pool xmlnode_pool(xmlnode node);
xmlnode _xmlnode_new(pool p, const char *name, unsigned int type);

/* Node editing */
void xmlnode_hide(xmlnode child);
void xmlnode_hide_attrib(xmlnode parent, const char *name);

/* Node deletion routine, also frees the node pool! */
void xmlnode_free(xmlnode node);

/* Locates a child tag by name and returns it */
xmlnode  xmlnode_get_tag(xmlnode parent, const char* name);
char* xmlnode_get_tag_data(xmlnode parent, const char* name);

/* Attribute accessors */
void     xmlnode_put_attrib(xmlnode owner, const char* name, const char* value);
char*    xmlnode_get_attrib(xmlnode owner, const char* name);
void     xmlnode_put_expat_attribs(xmlnode owner, const char** atts);

/* Bastard am I, but these are fun for internal use ;-) */
void     xmlnode_put_vattrib(xmlnode owner, const char* name, void *value);
void*    xmlnode_get_vattrib(xmlnode owner, const char* name);

/* Node traversal routines */
xmlnode  xmlnode_get_firstattrib(xmlnode parent);
xmlnode  xmlnode_get_firstchild(xmlnode parent);
xmlnode  xmlnode_get_lastchild(xmlnode parent);
xmlnode  xmlnode_get_nextsibling(xmlnode sibling);
xmlnode  xmlnode_get_prevsibling(xmlnode sibling);
xmlnode  xmlnode_get_parent(xmlnode node);

/* Node information routines */
char*    xmlnode_get_name(xmlnode node);
char*    xmlnode_get_data(xmlnode node);
int      xmlnode_get_datasz(xmlnode node);
int      xmlnode_get_type(xmlnode node);

int      xmlnode_has_children(xmlnode node);
int      xmlnode_has_attribs(xmlnode node);

/* Node-to-string translation */
char*    xmlnode2str(xmlnode node);

/* Node-to-terminated-string translation
   -- useful for interfacing w/ scripting langs */
char*    xmlnode2tstr(xmlnode node);

int      xmlnode_cmp(xmlnode a, xmlnode b); /* compares a and b for equality */

int      xmlnode2file(char *file, xmlnode node); /* writes node to file */
int xmlnode2file_limited(char *file, xmlnode node, size_t sizelimit);

/* Expat callbacks */
void expat_startElement(void* userdata, const char* name, const char** atts);
void expat_endElement(void* userdata, const char* name);
void expat_charData(void* userdata, const char* s, int len);

/***********************
 * XSTREAM Section
 ***********************/

#define XSTREAM_MAXNODE 1000000
#define XSTREAM_MAXDEPTH 100

#define XSTREAM_ROOT        0 /* root element */
#define XSTREAM_NODE        1 /* normal node */
#define XSTREAM_CLOSE       2 /* closed </stream:stream> */
#define XSTREAM_ERR         4 /* parser error */

typedef void (*xstream_onNode)(int type, xmlnode x, void *arg); /* xstream event handler */

typedef struct xstream_struct
{
    XML_Parser parser;
    xmlnode node;
    char *cdata;
    int cdata_len;
    pool p;
    xstream_onNode f;
    void *arg;
    int status;
    int depth;
} *xstream, _xstream;

xstream xstream_new(pool p, xstream_onNode f, void *arg); /* create a new xstream */
int xstream_eat(xstream xs, char *buff, int len); /* parse new data for this xstream, returns last XSTREAM_* status */

/* convience functions */
xmlnode xstream_header(char *nspace, char *to, char *from);
char *xstream_header_char(xmlnode x);

/* SHA.H */
/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is SHA 180-1 Header File
 *
 * The Initial Developer of the Original Code is Paul Kocher of
 * Cryptography Research.  Portions created by Paul Kocher are
 * Copyright (C) 1995-9 by Cryptography Research, Inc.  All
 * Rights Reserved.
 *
 * Contributor(s):
 *
 *     Paul Kocher
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable
 * instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

typedef struct {
  uint32_t H[5];
  uint32_t W[80];
  int lenW;
  uint32_t sizeHi,sizeLo;
} SHA_CTX;


void shaInit(SHA_CTX *ctx);
void shaUpdate(SHA_CTX *ctx, unsigned char *dataIn, int len);
void shaFinal(SHA_CTX *ctx, unsigned char hashout[20]);
void shaBlock(unsigned char *dataIn, int len, unsigned char hashout[20]);


/* END SHA.H */

#ifdef __cplusplus
}
#endif

#endif /* INCL_LIBXODE_H */
