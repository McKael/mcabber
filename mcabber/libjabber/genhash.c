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

/*****************************************************************************
 * Internal type definitions
 */

typedef struct tagHNODE
{
    struct tagHNODE *next;             /* next node in list */
    const void *key;                   /* key pointer */
    void *value;                       /* value pointer */
} HNODE;

#define SLAB_NUM_NODES     64        /* allocate this many nodes per slab */

typedef struct tagHSLAB
{
    struct tagHSLAB *next;             /* next slab pointer */
    HNODE nodes[SLAB_NUM_NODES];       /* the actual nodes */
} HSLAB;

#define HASH_NUM_BUCKETS   509       /* should be a prime number; see Knuth */

typedef struct tagHASHTABLE_INTERNAL
{
    unsigned long sig1;                /* first signature word */
    KEYHASHFUNC hash;                  /* hash function */
    KEYCOMPAREFUNC cmp;                /* comparison function */
    int count;                         /* table entry count */
    int bcount;                        /* bucket count */
    HNODE **buckets;                   /* the hash buckets */
    unsigned long sig2;                /* second signature word */

} HASHTABLE_INTERNAL;

#define HASH_SIG1      0x68736148UL  /* "Hash" */
#define HASH_SIG2      0x6F627245UL  /* "Erbo" */

#define do_hash(tb,key)     ((*((tb)->hash))(key) % ((tb)->bcount))

static HNODE *s_free_nodes = NULL;   /* free nodes list */
static HSLAB *s_slabs = NULL;        /* node slabs list */

/*****************************************************************************
 * Internal functions
 */

static HNODE *allocate_node(
    const void *key,   /* key pointer for this node */
    void *value)       /* value pointer for this node */
/*
    allocate_node allocates a new hash node and fills it.  Returns NULL if the
    node could not be allocated.
*/
{
    HNODE *rc;   /* return from this function */

    if (!s_free_nodes)
    { /* allocate a new slabful of nodes and chain them to make a new free list */
        register int i;  /* loop counter */
        HSLAB *slab = (HSLAB *)malloc(sizeof(HSLAB));
        if (!slab)
            return NULL;
        memset(slab,0,sizeof(HSLAB));
        slab->next = s_slabs;
        for (i=0; i<(SLAB_NUM_NODES-1); i++)
            slab->nodes[i].next = &(slab->nodes[i+1]);
        s_free_nodes = &(slab->nodes[0]);
        s_slabs = slab;

    } /* end if */

    /* grab a node off the fron of the free list and fill it */
    rc = s_free_nodes;
    s_free_nodes = rc->next;
    rc->next = NULL;
    rc->key = key;
    rc->value = value;
    return rc;

} /* end allocate_node */

static void free_node(
    HNODE *node)   /* node to be freed */
/*
    free_node returns a hash node to the list.
*/
{
    /* zap the node contents to avoid problems later */
    memset(node,0,sizeof(HNODE));

    /* chain it onto the free list */
    node->next = s_free_nodes;
    s_free_nodes = node;

} /* end free_node */

static HNODE *find_node(
    HASHTABLE_INTERNAL *tab,  /* pointer to hash table */
    const void *key,          /* key value to look up */
    int bucket)               /* bucket number (-1 to have function compute it) */
/*
    find_node walks a hash bucket to find a node whose key matches the named key value.
    Returns the node pointer, or NULL if it's not found.
*/
{
    register HNODE *p;  /* search pointer/return from this function */

    if (bucket<0)  /* compute hash value if we don't know it already */
        bucket = do_hash(tab,key);

    /* search through the bucket contents */
    for (p=tab->buckets[bucket]; p; p=p->next)
        if ((*(tab->cmp))(key,p->key)==0)
            return p;  /* found! */

    return NULL;   /* not found */

} /* end find_node */

static HASHTABLE_INTERNAL *handle2ptr(
    HASHTABLE tbl)  /* hash table handle */
/*
    handle2ptr converts a hash table handle into a pointer and checks its signatures
    to make sure someone's not trying to pull a whizzer on this module.
*/
{
    register HASHTABLE_INTERNAL *rc = (HASHTABLE_INTERNAL *)tbl;
    if ((rc->sig1==HASH_SIG1) && (rc->sig2==HASH_SIG2))
        return rc;     /* signatures match */
    else
        return NULL;   /* yIkes! */
}

/*****************************************************************************
 * External functions
 */

HASHTABLE ghash_create(int buckets, KEYHASHFUNC hash, KEYCOMPAREFUNC cmp)
/*
    Description:
        Creates a new hash table.

    Input:
        Parameters:
        buckets - Number of buckets to allocate for the hash table; this value
                  should be a prime number for maximum efficiency.
        hash - Key hash code function to use.
        cmp - Key comparison function to use.

    Output:
        Returns:
        NULL - Table could not be allocated.
        Other - Handle to the new hashtable.
*/
{
    HASHTABLE_INTERNAL *tab;  /* new table structure */
    char *allocated;

    if (!hash || !cmp)
        return NULL;  /* bogus! */

    if (buckets<=0)
        buckets = HASH_NUM_BUCKETS;

    /* allocate a hash table structure */
    allocated = malloc(sizeof(HASHTABLE_INTERNAL) + (buckets * sizeof(HNODE *)));
    if (!allocated)
        return NULL;  /* memory error */

    /* fill the fields of the hash table */
    tab = (HASHTABLE_INTERNAL *)allocated;
    allocated += sizeof(HASHTABLE_INTERNAL);
    memset(tab,0,sizeof(HASHTABLE_INTERNAL));
    memset(allocated,0,buckets * sizeof(HNODE *));
    tab->sig1 = HASH_SIG1;
    tab->hash = hash;
    tab->cmp = cmp;
    tab->bcount = buckets;
    tab->buckets = (HNODE **)allocated;
    tab->sig2 = HASH_SIG2;

    return (HASHTABLE)tab;  /* Qa'pla! */

} /* end ghash_create */

void ghash_destroy(HASHTABLE tbl)
/*
    Description:
        Destroys a hash table.

    Input:
        Parameters:
        tbl - Table to be destroyed.

    Output:
        Returns:
        Nothing.
*/
{
    HASHTABLE_INTERNAL *tab;  /* new table structure */
    int i;                    /* loop counter */
    HNODE *p, *p2;            /* temporary pointers */

    if (!tbl)
        return;  /* bogus! */

    /* Convert the handle to a table pointer. */
    tab = handle2ptr(tbl);
    if (!tab)
        return;

    /* Nuke the nodes it contains. */
    for (i=0; i<tab->bcount; i++)
    { /* free the contents of each bucket */
        p = tab->buckets[i];
        while (p)
        { /* free each node in turn */
            p2 = p->next;
            free_node(p);
            p = p2;

        } /* end while */

    } /* end for */

    free(tab);  /* bye bye now! */

} /* end ghash_destroy */

void *ghash_get(HASHTABLE tbl, const void *key)
/*
    Description:
        Retrieves a value stored in the hash table.

    Input:
        Parameters:
        tbl - The hash table to look in.
        key - The key value to search on.

    Output:
        Returns:
        NULL - Value not found.
        Other - Value corresponding to the specified key.
*/
{
    HASHTABLE_INTERNAL *tab;  /* internal table pointer */
    HNODE *node;              /* hash node */
    void *rc = NULL;          /* return from this function */

    if (!tbl || !key)
        return NULL;  /* bogus! */

    /* Convert the handle to a table pointer. */
    tab = handle2ptr(tbl);
    if (!tab)
        return NULL;  /* error */

    /* Attempt to find the node. */
    node = find_node(tab,key,-1);
    if (node)
        rc = node->value;  /* found it! */

    return rc;

} /* end ghash_get */

int ghash_put(HASHTABLE tbl, const void *key, void *value)
/*
    Description:
        Associates a key with a value in this hash table.

    Input:
        Parameters:
        tbl - Hash table to add.
        key - Key to use for the value in the table.
        value - Value to add for this key.

    Output:
        Returns:
        1 - Success.
        0 - Failure.

    Notes:
        If the specified key is already in the hashtable, its value will be replaced.
*/
{
    HASHTABLE_INTERNAL *tab;  /* internal table pointer */
    int bucket;               /* bucket value goes into */
    HNODE *node;              /* hash node */
    int rc = 1;               /* return from this function */

    if (!tbl || !key || !value)
        return 0;  /* bogus! */

    /* Convert the handle to a table pointer. */
    tab = handle2ptr(tbl);
    if (!tab)
        return 0;  /* error */


    /* Compute the hash bucket and try to find an existing node. */
    bucket = do_hash(tab,key);
    node = find_node(tab,key,bucket);
    if (!node)
    { /* OK, try to allocate a new node. */
        node = allocate_node(key,value);
        if (node)
        { /* Chain the new node into the hash table. */
            node->next = tab->buckets[bucket];
            tab->buckets[bucket] = node;
            tab->count++;

        } /* end if */
        else  /* allocation error */
            rc = 0;

    } /* end if */
    else  /* already in table - just reassign value */
        node->value = value;

    return rc;

} /* end ghash_put */

int ghash_remove(HASHTABLE tbl, const void *key)
/*
    Description:
        Removes an entry from a hash table, given its key.
    
    Input:
        Parameters:
        tbl - Hash table to remove from.
        key - Key of value to remove.

    Output:
        Returns:
        1 - Success.
        0 - Failure; key not present in hash table.
*/
{
    HASHTABLE_INTERNAL *tab;  /* internal table pointer */
    int bucket;               /* bucket value goes into */
    HNODE *node;              /* hash node */
    register HNODE *p;        /* removal pointer */
    int rc = 1;               /* return from this function */

    if (!tbl || !key)
        return 0;  /* bogus! */

    /* Convert the handle to a table pointer. */
    tab = handle2ptr(tbl);
    if (!tab)
        return 0;  /* error */


    /* Compute the hash bucket and try to find an existing node. */
    bucket = do_hash(tab,key);
    node = find_node(tab,key,bucket);
    if (node)
    { /* look to unchain it from the bucket it's in */
        if (node==tab->buckets[bucket])
            tab->buckets[bucket] = node->next;  /* unchain at head */
        else
        { /* unchain in middle of list */
            for (p=tab->buckets[bucket]; p->next!=node; p=p->next) ;
            p->next = node->next;

        } /* end else */

        free_node(node);  /* bye bye now! */
        tab->count--;

    } /* end if */
    else  /* node not found */
        rc = 0;

    return rc;

} /* end ghash_remove */

int ghash_walk(HASHTABLE tbl, TABLEWALKFUNC func, void *user_data)
/*
    Description:
        "Walks" through a hash table, calling a callback function for each element
    stored in it.

    Input:
        Parameters:
        tbl - Hash table to walk.
        func - Function to be called for each node.  It takes three parameters,
                   a user data pointer, a key value pointer, and a data value pointer.
           It returns 0 to stop the enumeration or 1 to keep it going.
        user_data - Value to use as the first parameter for the callback
                    function.

    Output:
        Returns:
        0 - Error occurred.
        Other - Number of nodes visited up to and including the one for which
                the callback function returned 0, if it did; ranges from 1
            to the number of nodes in the hashtable.
*/
{
    HASHTABLE_INTERNAL *tab;  /* internal table pointer */
    int i;                    /* loop counter */
    int running = 1;          /* we're still running */
    int count = 0;            /* number of nodes visited before stop node */
    register HNODE *p, *p2;   /* loop pointer */

    if (!tbl || !func)
        return -1;  /* bogus values! */

    /* Convert the handle to a table pointer. */
    tab = handle2ptr(tbl);
    if (!tab)
        return -1;  /* error */


    for (i=0; running && (i<tab->bcount); i++)
    { /* visit the contents of each bucket */
        p = tab->buckets[i];
        while (running && p)
        { /* visit each node in turn */
            p2 = p->next;
            count++;
            running = (*func)(user_data,p->key,p->value);
            p = p2;

        } /* end while */

    } /* end for */

    return count;

} /* end ghash_walk */

int str_hash_code(const char *s)
/*
    Description:
        Generates a hash code for a string.  This function uses the ELF hashing
        algorithm as reprinted in Andrew Binstock, "Hashing Rehashed," _Dr.
        Dobb's Journal_, April 1996.
 
    Input:
        Parameters:
            s - The string to be hashed.
 
    Output:
        Returns:
            A hash code for the string.
*/
{
    /* ELF hash uses unsigned chars and unsigned arithmetic for portability */
    const unsigned char *name = (const unsigned char *)s;
    unsigned long h = 0, g;

    if (!name)
        return 0;  /* anti-NULL guard not in the original */

    while (*name)
    { /* do some fancy bitwanking on the string */
        h = (h << 4) + (unsigned long)(*name++);
        if ((g = (h & 0xF0000000UL))!=0)
            h ^= (g >> 24);
        h &= ~g;

    } /* end while */

    return (int)h;

}
