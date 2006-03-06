/*
 * hbuf.c       -- History buffer implementation
 *
 * Copyright (C) 2005, 2006 Mikael Berthe <bmikael@lists.lilotux.net>
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

#define _GNU_SOURCE  /* We need glibc for strptime */
#include <string.h>

#include "hbuf.h"


/* This is a private structure type */

typedef struct {
  char *ptr;
  char *ptr_end;        // beginning of the block
  char *ptr_end_alloc;  // end of the current persistent block
  guchar flags;

  // XXX This should certainly be a pointer, and be allocated only when needed
  // (for ex. when HBB_FLAG_PERSISTENT is set).
  struct { // hbuf_line_info
    time_t timestamp;
    guchar flags;
  } prefix;
} hbuf_block;


//  hbuf_add_line(p_hbuf, text, prefix_flags, width)
// Add a line to the given buffer.  If width is not null, then lines are
// wrapped at this length.
//
// Note 1: Splitting according to width won't work if there are tabs; they
//         should be expanded before.
// Note 2: width does not include the ending \0.
void hbuf_add_line(GList **p_hbuf, const char *text, time_t timestamp,
        guint prefix_flags, guint width)
{
  GList *hbuf = *p_hbuf;
  char *line, *cr, *end;
  hbuf_block *hbuf_block_elt;

  if (!text) return;

  hbuf_block_elt = g_new0(hbuf_block, 1);
  hbuf_block_elt->prefix.timestamp  = timestamp;
  hbuf_block_elt->prefix.flags      = prefix_flags;
  if (!hbuf) {
    hbuf_block_elt->ptr  = g_new(char, HBB_BLOCKSIZE);
    hbuf_block_elt->flags  = HBB_FLAG_ALLOC | HBB_FLAG_PERSISTENT;
    hbuf_block_elt->ptr_end_alloc = hbuf_block_elt->ptr + HBB_BLOCKSIZE;
    *p_hbuf = g_list_append(*p_hbuf, hbuf_block_elt);
  } else {
    hbuf_block *hbuf_b_prev;
    // Set p_hbuf to the end of the list, to speed up history loading
    // (or CPU time will be used by g_list_last() for each line)
    hbuf = *p_hbuf = g_list_last(*p_hbuf);
    hbuf_b_prev = hbuf->data;
    hbuf_block_elt->ptr    = hbuf_b_prev->ptr_end;
    hbuf_block_elt->flags  = HBB_FLAG_PERSISTENT;
    hbuf_block_elt->ptr_end_alloc = hbuf_b_prev->ptr_end_alloc;
    g_list_append(*p_hbuf, hbuf_block_elt);
  }

  if (strlen(text) >= HBB_BLOCKSIZE) {
    // Too long
    text = "[ERR:LINE_TOO_LONG]";
    hbuf_block_elt->prefix.flags |= HBB_PREFIX_INFO;
  }
  if (hbuf_block_elt->ptr + strlen(text) >= hbuf_block_elt->ptr_end_alloc) {
    // Too long for the current allocated bloc, we need another one
    hbuf_block_elt->ptr  = g_new0(char, HBB_BLOCKSIZE);
    hbuf_block_elt->flags  = HBB_FLAG_ALLOC | HBB_FLAG_PERSISTENT;
    hbuf_block_elt->ptr_end_alloc = hbuf_block_elt->ptr + HBB_BLOCKSIZE;
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
      hbuf_block_elt->ptr_end_alloc = hbuf_b_prev->ptr_end_alloc;
      g_list_append(*p_hbuf, hbuf_block_elt);
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
      hbuf_block_elt->ptr_end_alloc = hbuf_b_prev->ptr_end_alloc;
      g_list_append(*p_hbuf, hbuf_block_elt);
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
    g_free(hbuf_b_elt);
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

  // *p_hbuf needs to be the head of the list
  first_elt = *p_hbuf = g_list_first(*p_hbuf);

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
      g_free(hbuf_b_next);
      g_list_delete_link(curr_elt, next_elt);
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
        hbuf_b_curr->ptr_end_alloc = hbuf_b_prev->ptr_end_alloc;
        // This is OK because insert_before(NULL) == append():
        *p_hbuf = g_list_insert_before(*p_hbuf, curr_elt->next, hbuf_b_curr);
      }
      curr_elt = g_list_next(curr_elt);
    }
  }
}

//  hbuf_previous_persistent()
// Returns the previous persistent block (line).  If the given line is
// persistent, then it is returned.
// This function is used for example when resizing a buffer.  If the top of the
// screen is on a non-persistent block, then a screen resize could destroy this
// line...
GList *hbuf_previous_persistent(GList *l_line)
{
  hbuf_block *hbuf_b_elt;

  while (l_line) {
    hbuf_b_elt = (hbuf_block*)l_line->data;
    if (hbuf_b_elt->flags & HBB_FLAG_PERSISTENT)
      return l_line;
    l_line = g_list_previous(l_line);
  }

  return NULL;
}

//  hbuf_get_lines(hbuf, n)
// Returns an array of n hbb_line pointers
// (The first line will be the line currently pointed by hbuf)
// Note: The caller should free the array and the text pointers after use.
hbb_line **hbuf_get_lines(GList *hbuf, unsigned int n)
{
  unsigned int i;
  hbuf_block *blk;
  guchar last_persist_prefixflags = 0;
  GList *last_persist;

  last_persist = hbuf_previous_persistent(hbuf);
  if (last_persist && last_persist != hbuf) {
    blk = (hbuf_block*)(last_persist->data);
    last_persist_prefixflags = blk->prefix.flags;
  }

  hbb_line **array = g_new0(hbb_line*, n);
  hbb_line **array_elt = array;

  for (i=0 ; i < n ; i++) {
    if (hbuf) {
      int maxlen;
      blk = (hbuf_block*)(hbuf->data);
      maxlen = blk->ptr_end - blk->ptr;
      *array_elt = (hbb_line*)g_new(hbb_line, 1);
      (*array_elt)->timestamp = blk->prefix.timestamp;
      (*array_elt)->flags     = blk->prefix.flags;
      (*array_elt)->text      = g_strndup(blk->ptr, maxlen);

      if (blk->flags & HBB_FLAG_PERSISTENT) {
        last_persist_prefixflags = blk->prefix.flags;
      } else {
        // Propagate hilighting flag
        (*array_elt)->flags |= last_persist_prefixflags & HBB_PREFIX_HLIGHT;
      }

      hbuf = g_list_next(hbuf);
    } else
      break;

    array_elt++;
  }

  return array;
}

//  hbuf_search(hbuf, direction, string)
// Look backward/forward for a line containing string in the history buffer
// Search starts at hbuf, and goes forward if direction == 1, backward if -1
GList *hbuf_search(GList *hbuf, int direction, const char *string)
{
  hbuf_block *blk;

  for (;;) {
    if (direction > 0)
      hbuf = g_list_next(hbuf);
    else
      hbuf = g_list_previous(hbuf);

    if (!hbuf) break;

    blk = (hbuf_block*)(hbuf->data);
    // XXX blk->ptr is (maybe) not really correct, because the match should
    // not be after ptr_end.  We should check that...
    if (strcasestr(blk->ptr, string))
      break;
  }

  return hbuf;
}

//  hbuf_jump_date(hbuf, t)
// Return a pointer to the first line after date t in the history buffer
GList *hbuf_jump_date(GList *hbuf, time_t t)
{
  hbuf_block *blk;

  hbuf = g_list_first(hbuf);

  for ( ; hbuf && g_list_next(hbuf); hbuf = g_list_next(hbuf)) {
    blk = (hbuf_block*)(hbuf->data);
    if (blk->prefix.timestamp >= t) break;
  }

  return hbuf;
}

//  hbuf_jump_percent(hbuf, pc)
// Return a pointer to the line at % pc of the history buffer
GList *hbuf_jump_percent(GList *hbuf, int pc)
{
  guint hlen;

  hbuf = g_list_first(hbuf);
  hlen = g_list_length(hbuf);

  return g_list_nth(hbuf, pc*hlen/100);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
