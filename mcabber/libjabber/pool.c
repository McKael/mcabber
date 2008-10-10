/*
 *  pool.c
 * This code comes from jabberd - Jabber Open Source Server
 *  Copyright (c) 2002 Jeremie Miller, Thomas Muldowney,
 *                    Ryan Eatmon, Robert Norris
 *  Copyright (C) 1998-1999 The Jabber Team http://jabber.org/
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
 * @file pool.c
 * @brief Handling of memory pools
 *
 * Jabberd handles its memory allocations in pools. You create a pool, can
 * allocate memory from it and all allocations will be freed if you free
 * the pool. Therefore you don't have to care that each malloc is freed,
 * you only have to take care that the pool is freed.
 *
 * The normal call-flow for pools is:
 *
 * pool p = pool_new();
 * struct mystruct *allocation1 = pmalloc(sizeof(struct mystruct));
 * struct myotherstruct *allocation2 = pmalloc(sizeof(struct myotherstruct));
 * ...
 * pool_free(p);
 */

#include "libxode.h"

#define MAX_MALLOC_TRIES 10 /**< how many seconds we try to allocate memory */

#ifdef POOL_DEBUG
int pool__total = 0;		/**< how many memory blocks are allocated */
int pool__ltotal = 0;
xht pool__disturbed = NULL;

inline void *_retried__malloc(size_t size);

/**
 * create a new memory allocation and increment the pool__total counter
 *
 * only used if POOL_DEBUG is defined, else it is an alias for malloc
 *
 * @param size size of the memory to allocate
 * @return pointer to the allocated memory
 */
void *_pool__malloc(size_t size)
{
    pool__total++;
    return malloc(size);
}

/**
 * free memory and decrement the pool__total counter
 *
 * only used if POOL_DEBUG is defined, else it is an alias for free
 *
 * @param block pointer to the memory allocation that should be freed
 */
void _pool__free(void *block)
{
    pool__total--;
    free(block);
}
#else
#define _pool__malloc malloc	/**< _pool__malloc updates pool__total counter if POOL_DEBUG is defined */
#define _pool__free free	/**< _pool__free updates pool__total counter if POOL_DEBUG is defined */
#endif

/**
 * try to allocate memory
 *
 * If allocation fails, it will be retries for MAX_MALLOC_TRIES seconds.
 * If it still fails, we exit the process
 *
 * @param size how many bytes of memory we allocate
 * @return pointer to the allocated memory
 */
void *_retried__malloc(size_t size) {
    void *allocated_memory;
    int malloc_tries = 0;

    while ((allocated_memory=_pool__malloc(size)) == NULL) {
	if (malloc_tries++ > MAX_MALLOC_TRIES) {
	    exit(999);
	}

	sleep(1); //pth_sleep(1);
    }

    return allocated_memory;
}

/**
 * make an empty pool
 *
 * Use the macro pool_new() instead of a direct call to this function. The
 * macro will create the parameters for you.
 *
 * @param zone the file in which the pool_new macro is called
 * @param line the line in the file in which the pool_new macro is called
 * @return the new allocated memory pool
 */
pool _pool_new(char *zone, int line)
{
    // int malloc_tries = 0;
#ifdef POOL_DEBUG
    int old__pool__total;
#endif

    pool p = _retried__malloc(sizeof(_pool));

    p->cleanup = NULL;
    p->heap = NULL;
    p->size = 0;

#ifdef POOL_DEBUG
    p->lsize = -1;
    p->zone[0] = '\0';
    strcat(p->zone,zone);
    snprintf(p->zone, sizeof(p->zone), "%s:%i", zone, line);
    snprintf(p->name, sizeof(p->name), "%X", p);

    if(pool__disturbed == NULL)
    {
        pool__disturbed = (xht)1; /* reentrancy flag! */
        pool__disturbed = ghash_create(POOL_DEBUG,(KEYHASHFUNC)str_hash_code,(KEYCOMPAREFUNC)j_strcmp);
    }
    if(pool__disturbed != (xht)1)
        ghash_put(pool__disturbed,p->name,p);
#endif

    return p;
}

/**
 * free a memory heap (struct pheap)
 *
 * @param arg which heep should be freed
 */
void _pool_heap_free(void *arg)
{
    struct pheap *h = (struct pheap *)arg;

    _pool__free(h->block);
    _pool__free(h);
}

/**
 * append a pool_cleaner function (callback) to a pool
 *
 * mem should always be freed last
 *
 * All appended pool_cleaner functions will be called if a pool is freed.
 * This might be used to clean logically subpools.
 *
 * @param p to which pool the pool_cleaner should be added
 * @param pf structure containing the reference to the pool_cleaner and links for the list
 */
void _pool_cleanup_append(pool p, struct pfree *pf)
{
    struct pfree *cur;

    if(p->cleanup == NULL)
    {
        p->cleanup = pf;
        return;
    }

    /* fast forward to end of list */
    for(cur = p->cleanup; cur->next != NULL; cur = cur->next);

    cur->next = pf;
}

/**
 * create a cleanup tracker
 *
 * this function is used to create a pfree structure that can be passed to _pool_cleanup_append()
 *
 * @param p the pool to which the pool_cleaner should be added
 * @param f the function that should be called if the pool is freed
 * @param arg the parameter that should be passed to the pool_cleaner function
 * @return pointer to the new pfree structure
 */
struct pfree *_pool_free(pool p, pool_cleaner f, void *arg)
{
    struct pfree *ret;

    /* make the storage for the tracker */
    ret = _retried__malloc(sizeof(struct pfree));
    ret->f = f;
    ret->arg = arg;
    ret->next = NULL;

    return ret;
}

/**
 * create a heap and make sure it get's cleaned up
 *
 * pheaps are used by memory pools internally to handle the memory allocations
 *
 * @note the macro pool_heap calls _pool_new_heap and NOT _pool_heap
 *
 * @param p for which pool the heap should be created
 * @param size how big the pool should be
 * @return pointer to the new pheap
 */
struct pheap *_pool_heap(pool p, int size)
{
    struct pheap *ret;
    struct pfree *clean;

    /* make the return heap */
    ret = _retried__malloc(sizeof(struct pheap));
    ret->block = _retried__malloc(size);
    ret->size = size;
    p->size += size;
    ret->used = 0;

    /* append to the cleanup list */
    clean = _pool_free(p, _pool_heap_free, (void *)ret);
    clean->heap = ret; /* for future use in finding used mem for pstrdup */
    _pool_cleanup_append(p, clean);

    return ret;
}

/**
 * create a new memory pool and set the initial heap size
 *
 * @note you should not call this function but use the macro pool_heap instead which fills zone and line automatically
 *
 * @param size the initial size of the memory pool
 * @param zone the file where this function is called (for debugging)
 * @param line the line in the file where this function is called
 * @return the new memory pool
 */
pool _pool_new_heap(int size, char *zone, int line)
{
    pool p;
    p = _pool_new(zone, line);
    p->heap = _pool_heap(p,size);
    return p;
}

/**
 * allocate memory from a memory pool
 *
 * @param p the pool to use
 * @param size how much memory to allocate
 * @return pointer to the allocated memory
 */
void *pmalloc(pool p, int size)
{
    void *block;

    if(p == NULL)
    {
        fprintf(stderr,"Memory Leak! [pmalloc received NULL pool, unable to track allocation, exiting]\n");
        abort();
    }

    /* if there is no heap for this pool or it's a big request, just raw, I like how we clean this :) */
    if(p->heap == NULL || size > (p->heap->size / 2))
    {
	block = _retried__malloc(size);
        p->size += size;
        _pool_cleanup_append(p, _pool_free(p, _pool__free, block));
        return block;
    }

    /* we have to preserve boundaries, long story :) */
    if(size >= 4)
        while(p->heap->used&7) p->heap->used++;

    /* if we don't fit in the old heap, replace it */
    if(size > (p->heap->size - p->heap->used))
        p->heap = _pool_heap(p, p->heap->size);

    /* the current heap has room */
    block = (char *)p->heap->block + p->heap->used;
    p->heap->used += size;
    return block;
}

/**
 * allocate memory and initialize the memory with the given char c
 *
 * @deprecated jabberd does use pmalloco instead, this function will be removed
 *
 * @param p which pool to use
 * @param size the size of the allocation
 * @param c the initialization character
 * @return pointer to the allocated memory
 */
void *pmalloc_x(pool p, int size, char c)
{
   void* result = pmalloc(p, size);
   if (result != NULL)
           memset(result, c, size);
   return result;
}

/**
 * allocate memory and initialize the memory with zero bytes
 *
 * easy safety utility (for creating blank mem for structs, etc)
 *
 * @param p which pool to use
 * @param size the size of the allocation
 * @return pointer to the allocated memory
 */
void *pmalloco(pool p, int size)
{
    void *block = pmalloc(p, size);
    memset(block, 0, size);
    return block;
}

/**
 * duplicate a string and allocate memory for it
 *
 * @todo efficient: move this to const char* and then loop through the existing heaps to see if src is within a block in this pool
 *
 * @param p the pool to use
 * @param src the string that should be duplicated
 * @return the duplicated string
 */
char *pstrdup(pool p, const char *src)
{
    char *ret;

    if(src == NULL)
        return NULL;

    ret = pmalloc(p,strlen(src) + 1);
    strcpy(ret,src);

    return ret;
}

/**
 * when pstrdup() is moved to "const char*", this one would actually return a new block
 */
char *pstrdupx(pool p, const char *src)
{
    return pstrdup(p, src);
}

/**
 * get the size of a memory pool
 *
 * @param p the pool
 * @return the size
 */
int pool_size(pool p)
{
    if(p == NULL) return 0;

    return p->size;
}

/**
 * free a pool (and all memory that is allocated in it)
 *
 * @param p which pool to free
 */
void pool_free(pool p)
{
    struct pfree *cur, *stub;

    if(p == NULL) return;

    cur = p->cleanup;
    while(cur != NULL)
    {
        (*cur->f)(cur->arg);
        stub = cur->next;
        _pool__free(cur);
        cur = stub;
    }

#ifdef POOL_DEBUG
    ghash_remove(pool__disturbed,p->name);
#endif

    _pool__free(p);

}

/**
 * public cleanup utils, insert in a way that they are run FIFO, before mem frees
 */
void pool_cleanup(pool p, pool_cleaner f, void *arg)
{
    struct pfree *clean;

    clean = _pool_free(p, f, arg);
    clean->next = p->cleanup;
    p->cleanup = clean;
}

#ifdef POOL_DEBUG
void debug_log(char *zone, const char *msgfmt, ...);
void _pool_stat(xht h, const char *key, void *data, void *arg)
{
    pool p = (pool)data;

    if(p->lsize == -1)
        debug_log("pool_debug","%s: %s is a new pool",p->zone, p->name);
    else if(p->size > p->lsize)
        debug_log("pool_debug","%s: %s grew %d",p->zone, p->name, p->size - p->lsize);
    else if((int)arg)
        debug_log("pool_debug","%s: %s exists %d",p->zone,p->name, p->size);
    p->lsize = p->size;
}

/**
 * print memory pool statistics (for debugging purposes)
 *
 * @param full make a full report? (0 = no, 1 = yes)
 */
void pool_stat(int full)
{
    if (pool__disturbed == NULL || pool__disturbed == (xht)1)
	return;

    ghash_walk(pool__disturbed,_pool_stat,(void *)full);
    if(pool__total != pool__ltotal)
        debug_log("pool_debug","%d\ttotal missed mallocs",pool__total);
    pool__ltotal = pool__total;

    return;
}
#else
/**
 * dummy implementation: print memory pool statistics (for debugging purposes, real implementation if POOL_DEBUG is defined)
 *
 * @param full make a full report? (0 = no, 1 = yes)
 */
void pool_stat(int full)
{
    return;
}
#endif
