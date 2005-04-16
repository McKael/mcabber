/*
 * hbuf.c       -- History buffer implementation
 * 
 * Copyright (C) 2005 Mikael Berthe <bmikael@lists.lilotux.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <string.h>

#include "hbuf.h"


/* This is a private structure type */

typedef struct {
  char *ptr;
  char *ptr_end;
  guchar flags;

  // XXX This should certainly be a pointer, and be allocated only when needed
  // (for ex. when HBB_FLAG_PERSISTENT is set).
  struct { // hbuf_line_info
    char *ptr_end_alloc;
    char prefix[32];
  } persist;
} hbuf_block;


//  hbuf_add_line(p_hbuf, text, width)
// Add a line to the given buffer.  If width is not null, then lines are
// wrapped at this length.
//
// Note 1: Splitting according to width won't work if there are tabs; they
//         should be expanded before.
// Note 2: width does not include the ending \0.
void hbuf_add_line(GList **p_hbuf, char *text, unsigned int width)
{
  GList *hbuf = *p_hbuf;
  char *line, *cr, *end;

  if (!text) return;

  hbuf_block *hbuf_block_elt = g_new0(hbuf_block, 1);
  if (!hbuf) {
    hbuf_block_elt->ptr    = g_new(char, HBB_BLOCKSIZE);
    hbuf_block_elt->flags  = HBB_FLAG_ALLOC | HBB_FLAG_PERSISTENT;
    hbuf_block_elt->persist.ptr_end_alloc = hbuf_block_elt->ptr + HBB_BLOCKSIZE;
    *p_hbuf = g_list_append(*p_hbuf, hbuf_block_elt);
  } else {
    hbuf_block *hbuf_b_prev = g_list_last(hbuf)->data;
    hbuf_block_elt->ptr    = hbuf_b_prev->ptr_end;
    hbuf_block_elt->flags  = HBB_FLAG_PERSISTENT;
    hbuf_block_elt->persist.ptr_end_alloc = hbuf_b_prev->persist.ptr_end_alloc;
    *p_hbuf = g_list_append(*p_hbuf, hbuf_block_elt);
  }

  if (strlen(text) >= HBB_BLOCKSIZE) {
    // Too long
    text = "[ERR:LINE_TOO_LONG]";
  }
  if (hbuf_block_elt->ptr + strlen(text) >= hbuf_block_elt->persist.ptr_end_alloc) {
    // Too long for the current allocated bloc, we need another one
    hbuf_block_elt->ptr    = g_new0(char, HBB_BLOCKSIZE);
    hbuf_block_elt->flags  = HBB_FLAG_ALLOC | HBB_FLAG_PERSISTENT;
    hbuf_block_elt->persist.ptr_end_alloc = hbuf_block_elt->ptr + HBB_BLOCKSIZE;
  }

  line = hbuf_block_elt->ptr;
  // Ok, now we can copy the text..
  strcpy(line, text);
  hbuf_block_elt->ptr_end = line + strlen(line) + 1;
  end = hbuf_block_elt->ptr_end;

  // Let's add non-persistent blocs if necessary
  // - If there are '\n' in the string
  // - If length > width (and width != 0)
  cr = strchr(line, '\n');
  while (cr || (width && strlen(line) > width)) {
    hbuf_block *hbuf_b_prev = hbuf_block_elt;

    if (!width || (cr && (cr - line <= (int)width))) {
      // Carriage return
      *cr = 0;
      hbuf_block_elt->ptr_end = cr;
      // Create another persistent block
      hbuf_block_elt = g_new0(hbuf_block, 1);
      hbuf_block_elt->ptr      = hbuf_b_prev->ptr_end + 1; // == cr+1
      hbuf_block_elt->ptr_end  = end;
      hbuf_block_elt->flags    = HBB_FLAG_PERSISTENT;
      hbuf_block_elt->persist.ptr_end_alloc = hbuf_b_prev->persist.ptr_end_alloc;
      *p_hbuf = g_list_append(*p_hbuf, hbuf_block_elt);
      line = hbuf_block_elt->ptr;
    } else {
      // We need to break where we can find a space char
      char *br; // break pointer
      for (br = line + width; br > line && *br != 32 && *br != 9; br--)
        ;
      if (br <= line)
        br = line + width;
      else
        br++;
      hbuf_block_elt->ptr_end = br;
      // Create another block, non-persistent
      hbuf_block_elt = g_new0(hbuf_block, 1);
      hbuf_block_elt->ptr      = hbuf_b_prev->ptr_end; // == br
      hbuf_block_elt->ptr_end  = end;
      hbuf_block_elt->flags    = 0;
      hbuf_block_elt->persist.ptr_end_alloc = hbuf_b_prev->persist.ptr_end_alloc;
      *p_hbuf = g_list_append(*p_hbuf, hbuf_block_elt);
      line = hbuf_block_elt->ptr;
    }
    cr = strchr(line, '\n');
  }
}

//  hbuf_free()
// Destroys all hbuf list.
void hbuf_free(GList **p_hbuf)
{
  hbuf_block *hbuf_b_elt;
  GList *hbuf_elt;
  GList *first_elt = g_list_first(*p_hbuf);

  for (hbuf_elt = first_elt; hbuf_elt; hbuf_elt = g_list_next(hbuf_elt)) {
    hbuf_b_elt = (hbuf_block*)(hbuf_elt->data);
    if (hbuf_b_elt->flags & HBB_FLAG_ALLOC) {
      g_free(hbuf_b_elt->ptr);
    }
  }

  g_list_free(*p_hbuf);
  *p_hbuf = NULL;
}

//  hbuf_rebuild()
// Rebuild all hbuf list, with the new width.
// If width == 0, lines are not wrapped.
void hbuf_rebuild(GList **p_hbuf, unsigned int width)
{
  GList *first_elt, *curr_elt, *next_elt;
  hbuf_block *hbuf_b_curr, *hbuf_b_next;

  first_elt = g_list_first(*p_hbuf);

  // #1 Remove non-persistent blocks (ptr_end should be updated!)
  curr_elt = first_elt;
  while (curr_elt) {
    next_elt = g_list_next(curr_elt);
    // Last element?
    if (!next_elt)
      break;
    hbuf_b_curr = (hbuf_block*)(curr_elt->data);
    hbuf_b_next = (hbuf_block*)(next_elt->data);
    // Is next line not-persistent?
    if (!(hbuf_b_next->flags & HBB_FLAG_PERSISTENT)) {
      hbuf_b_curr->ptr_end = hbuf_b_next->ptr_end;
      g_list_delete_link(curr_elt, next_elt);
      next_elt = g_list_next(curr_elt);
    } else 
      curr_elt = next_elt;
  }
  // #2 Go back to head and create non-persistent blocks when needed
  if (width) {
    char *line, *end;
    curr_elt = first_elt;

    while (curr_elt) {
      hbuf_b_curr = (hbuf_block*)(curr_elt->data);
      line = hbuf_b_curr->ptr;
      if (strlen(line) > width) {
        hbuf_block *hbuf_b_prev = hbuf_b_curr;

        // We need to break where we can find a space char
        char *br; // break pointer
        for (br = line + width; br > line && *br != 32 && *br != 9; br--)
          ;
        if (br <= line)
          br = line + width;
        else
          br++;
        end = hbuf_b_curr->ptr_end;
        hbuf_b_curr->ptr_end = br;
        // Create another block, non-persistent
        hbuf_b_curr = g_new0(hbuf_block, 1);
        hbuf_b_curr->ptr      = hbuf_b_prev->ptr_end; // == br
        hbuf_b_curr->ptr_end  = end;
        hbuf_b_curr->flags    = 0;
        hbuf_b_curr->persist.ptr_end_alloc = hbuf_b_prev->persist.ptr_end_alloc;
        /*
        // Is there a better way?
        if (g_list_next(curr_elt))
          g_list_insert_before(*p_hbuf, curr_elt->next, hbuf_b_curr);
        else
          *p_hbuf = g_list_append(*p_hbuf, hbuf_b_curr);
        */
        // This is OK because insert_before(NULL) <==> append()
        g_list_insert_before(*p_hbuf, curr_elt->next, hbuf_b_curr);
      }
      curr_elt = g_list_next(curr_elt);
    }
  }
}

//  hbuf_get_lines(hbuf, n, where)
// Returns an array of n pointers (for n lines from hbuf)
// (The first line will be the line currently pointed by hbuf)
// Note:The caller should free the array after use.
char **hbuf_get_lines(GList *hbuf, unsigned int n)
{
  unsigned int i;

  char **array = g_new0(char*, n);
  char **array_elt = array;

  for (i=0 ; i < n ; i++) {
    if (hbuf) {
      hbuf_block *blk = (hbuf_block*)(hbuf->data);
      int maxlen;
      maxlen = blk->ptr_end - blk->ptr;
      *array_elt++ = g_strndup(blk->ptr, maxlen);
      hbuf = g_list_next(hbuf);
    } else
      *array_elt++ = NULL;
  }

  return array;
}

