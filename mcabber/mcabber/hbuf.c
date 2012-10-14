/*
 * hbuf.c       -- History buffer implementation
 *
 * Copyright (C) 2005-2010 Mikael Berthe <mikael@lilotux.net>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hbuf.h"
#include "utils.h"
#include "utf8.h"
#include "screen.h"


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
    unsigned mucnicklen;
    guint  flags;
    gpointer xep184;
  } prefix;
} hbuf_block;


//  do_wrap(p_hbuf, first_hbuf_elt, width)
// Wrap hbuf lines with the specified width.
// '\n' are handled by this routine (they are removed and persistent lines
// are created).
// All hbuf elements are processed, starting from first_hbuf_elt.
static inline void do_wrap(GList **p_hbuf, GList *first_hbuf_elt,
                           unsigned int width)
{
  GList *curr_elt = first_hbuf_elt;

  // Let's add non-persistent blocs if necessary
  // - If there are '\n' in the string
  // - If length > width (and width != 0)
  while (curr_elt) {
    hbuf_block *hbuf_b_curr, *hbuf_b_prev;
    char *c, *end;
    char *br = NULL; // break pointer
    char *cr = NULL; // CR pointer
    unsigned int cur_w = 0;

    // We want to break where we can find a space char or a CR

    hbuf_b_curr = (hbuf_block*)(curr_elt->data);
    hbuf_b_prev = hbuf_b_curr;
    c = hbuf_b_curr->ptr;

    while (*c && (!width || cur_w <= width)) {
      if (*c == '\n') {
        br = cr = c;
        *c = 0;
        break;
      }
      if (iswblank(get_char(c)))
        br = c;
      cur_w += get_char_width(c);
      c = next_char(c);
    }

    if (cr || (*c && cur_w > width)) {
      if (!br || br == hbuf_b_curr->ptr)
        br = c;
      else
        br = next_char(br);
      end = hbuf_b_curr->ptr_end;
      hbuf_b_curr->ptr_end = br;
      // Create another block
      hbuf_b_curr = g_new0(hbuf_block, 1);
      // The block must be persistent after a CR
      if (cr) {
        hbuf_b_curr->ptr    = hbuf_b_prev->ptr_end + 1; // == cr+1
        hbuf_b_curr->flags  = HBB_FLAG_PERSISTENT;
      } else {
        hbuf_b_curr->ptr    = hbuf_b_prev->ptr_end; // == br
        hbuf_b_curr->flags    = 0;
      }
      hbuf_b_curr->ptr_end  = end;
      hbuf_b_curr->ptr_end_alloc = hbuf_b_prev->ptr_end_alloc;
      // This is OK because insert_before(NULL) == append():
      *p_hbuf = g_list_insert_before(*p_hbuf, curr_elt->next, hbuf_b_curr);
    }
    curr_elt = g_list_next(curr_elt);
  }
}

//  hbuf_add_line(p_hbuf, text, prefix_flags, width, maxhbufblocks)
// Add a line to the given buffer.  If width is not null, then lines are
// wrapped at this length.
// maxhbufblocks is the maximum number of hbuf blocks we can allocate.  If
// null, there is no limit.  If non-null, it should be >= 2.
//
// Note 1: Splitting according to width won't work if there are tabs; they
//         should be expanded before.
// Note 2: width does not include the ending \0.
void hbuf_add_line(GList **p_hbuf, const char *text, time_t timestamp,
        guint prefix_flags, guint width, guint maxhbufblocks,
        unsigned mucnicklen, gpointer xep184)
{
  GList *curr_elt;
  char *line;
  guint hbb_blocksize, textlen;
  hbuf_block *hbuf_block_elt;

  if (!text) return;

  prefix_flags |= (xep184 ? HBB_PREFIX_RECEIPT : 0);

  textlen = strlen(text);
  hbb_blocksize = MAX(textlen+1, HBB_BLOCKSIZE);

  hbuf_block_elt = g_new0(hbuf_block, 1);
  hbuf_block_elt->prefix.timestamp  = timestamp;
  hbuf_block_elt->prefix.flags      = prefix_flags;
  hbuf_block_elt->prefix.mucnicklen = mucnicklen;
  hbuf_block_elt->prefix.xep184     = xep184;
  if (!*p_hbuf) {
    hbuf_block_elt->ptr  = g_new(char, hbb_blocksize);
    if (!hbuf_block_elt->ptr) {
      g_free(hbuf_block_elt);
      return;
    }
    hbuf_block_elt->flags  = HBB_FLAG_ALLOC | HBB_FLAG_PERSISTENT;
    hbuf_block_elt->ptr_end_alloc = hbuf_block_elt->ptr + hbb_blocksize;
  } else {
    hbuf_block *hbuf_b_prev;
    // Set p_hbuf to the end of the list, to speed up history loading
    // (or CPU time will be used by g_list_last() for each line)
    *p_hbuf = g_list_last(*p_hbuf);
    hbuf_b_prev = (*p_hbuf)->data;
    hbuf_block_elt->ptr    = hbuf_b_prev->ptr_end;
    hbuf_block_elt->flags  = HBB_FLAG_PERSISTENT;
    hbuf_block_elt->ptr_end_alloc = hbuf_b_prev->ptr_end_alloc;
  }
  *p_hbuf = g_list_append(*p_hbuf, hbuf_block_elt);

  if (hbuf_block_elt->ptr + textlen >= hbuf_block_elt->ptr_end_alloc) {
    // Too long for the current allocated bloc, we need another one
    if (!maxhbufblocks || textlen >= HBB_BLOCKSIZE) {
      // No limit, let's allocate a new block
      // If the message text is big, we won't bother to reuse an old block
      // as well (it could be too small and cause a segfault).
      hbuf_block_elt->ptr  = g_new0(char, hbb_blocksize);
      hbuf_block_elt->ptr_end_alloc = hbuf_block_elt->ptr + hbb_blocksize;
      // XXX We should check the return value.
    } else {
      GList *hbuf_head, *hbuf_elt;
      hbuf_block *hbuf_b_elt;
      guint n = 0;
      hbuf_head = g_list_first(*p_hbuf);
      // We need at least 2 allocated blocks
      if (maxhbufblocks == 1)
        maxhbufblocks = 2;
      // Let's count the number of allocated areas
      for (hbuf_elt = hbuf_head; hbuf_elt; hbuf_elt = g_list_next(hbuf_elt)) {
        hbuf_b_elt = (hbuf_block*)(hbuf_elt->data);
        if (hbuf_b_elt->flags & HBB_FLAG_ALLOC)
          n++;
      }
      // If we can't allocate a new area, reuse the previous block(s)
      if (n < maxhbufblocks) {
        hbuf_block_elt->ptr  = g_new0(char, hbb_blocksize);
        hbuf_block_elt->ptr_end_alloc = hbuf_block_elt->ptr + hbb_blocksize;
      } else {
        // Let's use an old block, and free the extra blocks if needed
        char *allocated_block = NULL;
        char *end_of_allocated_block = NULL;
        while (n >= maxhbufblocks) {
          int start_of_block = 1;
          for (hbuf_elt = hbuf_head; hbuf_elt; hbuf_elt = hbuf_head) {
            hbuf_b_elt = (hbuf_block*)(hbuf_elt->data);
            if (hbuf_b_elt->flags & HBB_FLAG_ALLOC) {
              if (start_of_block-- == 0)
                break;
              if (n == maxhbufblocks) {
                allocated_block = hbuf_b_elt->ptr;
                end_of_allocated_block = hbuf_b_elt->ptr_end_alloc;
              } else {
                g_free(hbuf_b_elt->ptr);
              }
            }
            g_free(hbuf_b_elt);
            hbuf_head = *p_hbuf = g_list_delete_link(hbuf_head, hbuf_elt);
          }
          n--;
        }
        memset(allocated_block, 0, end_of_allocated_block-allocated_block);
        hbuf_block_elt->ptr = allocated_block;
        hbuf_block_elt->ptr_end_alloc = end_of_allocated_block;
      }
    }
    hbuf_block_elt->flags  = HBB_FLAG_ALLOC | HBB_FLAG_PERSISTENT;
  }

  line = hbuf_block_elt->ptr;
  // Ok, now we can copy the text..
  strcpy(line, text);
  hbuf_block_elt->ptr_end = line + textlen + 1;

  curr_elt = g_list_last(*p_hbuf);

  // Wrap lines and handle CRs ('\n')
  do_wrap(p_hbuf, curr_elt, width);
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

  g_list_free(first_elt);
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
      curr_elt = g_list_delete_link(curr_elt, next_elt);
    } else
      curr_elt = next_elt;
  }
  // #2 Go back to head and create non-persistent blocks when needed
  if (width)
    do_wrap(p_hbuf, first_elt, width);
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
    if (hbuf_b_elt->flags & HBB_FLAG_PERSISTENT &&
        (hbuf_b_elt->flags & ~HBB_PREFIX_READMARK))
      return l_line;
    l_line = g_list_previous(l_line);
  }

  return NULL;
}

//  hbuf_get_lines(hbuf, n)
// Returns an array of n hbb_line pointers
// (The first line will be the line currently pointed by hbuf)
// Note: The caller should free the array, the hbb_line pointers and the
// text pointers after use.
hbb_line **hbuf_get_lines(GList *hbuf, unsigned int n)
{
  unsigned int i;
  hbuf_block *blk;
  guint last_persist_prefixflags = 0;
  GList *last_persist;  // last persistent flags
  hbb_line **array, **array_elt;
  hbb_line *prev_array_elt = NULL;

  // To be able to correctly highlight multi-line messages,
  // we need to look at the last non-null prefix, which should be the first
  // line of the message.  We also need to check if there's a readmark flag
  // somewhere in the message.
  last_persist = hbuf_previous_persistent(hbuf);
  while (last_persist) {
    blk = (hbuf_block*)last_persist->data;
    if ((blk->flags & HBB_FLAG_PERSISTENT) && blk->prefix.flags) {
      // This can be either the beginning of the message,
      // or a persistent line with a readmark flag (or both).
      if (blk->prefix.flags & ~HBB_PREFIX_READMARK) { // First message line
        last_persist_prefixflags |= blk->prefix.flags;
        break;
      } else { // Not the first line, but we need to keep the readmark flag
        last_persist_prefixflags = blk->prefix.flags;
      }
    }
    last_persist = g_list_previous(last_persist);
  }

  array = g_new0(hbb_line*, n);
  array_elt = array;

  for (i = 0 ; i < n ; i++) {
    if (hbuf) {
      int maxlen;

      blk = (hbuf_block*)(hbuf->data);
      maxlen = blk->ptr_end - blk->ptr;
      *array_elt = (hbb_line*)g_new(hbb_line, 1);
      (*array_elt)->timestamp  = blk->prefix.timestamp;
      (*array_elt)->flags      = blk->prefix.flags;
      (*array_elt)->mucnicklen = blk->prefix.mucnicklen;
      (*array_elt)->text       = g_strndup(blk->ptr, maxlen);

      if ((blk->flags & HBB_FLAG_PERSISTENT) &&
          (blk->prefix.flags & ~HBB_PREFIX_READMARK)) {
        // This is a new message: persistent block flag and no prefix flag
        // (except a possible readmark flag)
        last_persist_prefixflags = blk->prefix.flags;
      } else {
        // Propagate highlighting flags
        (*array_elt)->flags |= last_persist_prefixflags &
                               (HBB_PREFIX_HLIGHT_OUT | HBB_PREFIX_HLIGHT |
                                HBB_PREFIX_INFO | HBB_PREFIX_IN |
                                HBB_PREFIX_READMARK);
        // Continuation of a message - omit the prefix
        (*array_elt)->flags |= HBB_PREFIX_CONT;
        (*array_elt)->mucnicklen = 0; // The nick is in the first one

        // If there is a readmark on this line, update last_persist_prefixflags
        if (blk->flags & HBB_FLAG_PERSISTENT)
          last_persist_prefixflags |= blk->prefix.flags & HBB_PREFIX_READMARK;
        // Remove readmark flag from the previous line
        if (prev_array_elt && last_persist_prefixflags & HBB_PREFIX_READMARK)
          prev_array_elt->flags &= ~HBB_PREFIX_READMARK;
      }

      prev_array_elt = *array_elt;

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

//  hbuf_jump_readmark(hbuf)
// Return a pointer to the line following the readmark
// or NULL if no mark was found.
GList *hbuf_jump_readmark(GList *hbuf)
{
  hbuf_block *blk;
  GList *r = NULL;

  hbuf = g_list_last(hbuf);
  for ( ; hbuf; hbuf = g_list_previous(hbuf)) {
    blk = (hbuf_block*)(hbuf->data);
    if (blk->prefix.flags & HBB_PREFIX_READMARK)
      return r;
    if ((blk->flags & HBB_FLAG_PERSISTENT) &&
        (blk->prefix.flags & ~HBB_PREFIX_READMARK))
      r = hbuf;
  }

  return NULL;
}

//  hbuf_dump_to_file(hbuf, filename)
// Save the buffer to a file.
void hbuf_dump_to_file(GList *hbuf, const char *filename)
{
  hbuf_block *blk;
  hbb_line line;
  guint last_persist_prefixflags = 0;
  guint prefixwidth;
  char pref[96];
  FILE *fp;
  struct stat statbuf;

  if (!stat(filename, &statbuf)) {
    scr_LogPrint(LPRINT_NORMAL, "The file already exists.");
    return;
  }
  fp = fopen(filename, "w");
  if (!fp) {
    scr_LogPrint(LPRINT_NORMAL, "Unable to open the file.");
    return;
  }

  prefixwidth = scr_getprefixwidth();
  prefixwidth = MIN(prefixwidth, sizeof pref);

  for (hbuf = g_list_first(hbuf); hbuf; hbuf = g_list_next(hbuf)) {
    int maxlen;

    blk = (hbuf_block*)(hbuf->data);
    maxlen = blk->ptr_end - blk->ptr;

    memset(&line, 0, sizeof(line));
    line.timestamp  = blk->prefix.timestamp;
    line.flags      = blk->prefix.flags;
    line.mucnicklen = blk->prefix.mucnicklen;
    line.text       = g_strndup(blk->ptr, maxlen);

    if ((blk->flags & HBB_FLAG_PERSISTENT) &&
        (blk->prefix.flags & ~HBB_PREFIX_READMARK)) {
      last_persist_prefixflags = blk->prefix.flags;
    } else {
      // Propagate necessary highlighting flags
      line.flags |= last_persist_prefixflags &
                    (HBB_PREFIX_HLIGHT_OUT | HBB_PREFIX_HLIGHT |
                     HBB_PREFIX_INFO | HBB_PREFIX_IN);
      // Continuation of a message - omit the prefix
      line.flags |= HBB_PREFIX_CONT;
      line.mucnicklen = 0; // The nick is in the first one
    }

    scr_line_prefix(&line, pref, prefixwidth);
    fprintf(fp, "%s%s\n", pref, line.text);
  }

  fclose(fp);
  return;
}

//  hbuf_remove_receipt(hbuf, xep184)
// Remove the Receipt Flag for the message with the given xep184 id
// Returns TRUE if it was found and removed, otherwise FALSE
gboolean hbuf_remove_receipt(GList *hbuf, gconstpointer xep184)
{
  hbuf_block *blk;

  hbuf = g_list_last(hbuf);

  for ( ; hbuf; hbuf = g_list_previous(hbuf)) {
    blk = (hbuf_block*)(hbuf->data);
    if (!g_strcmp0(blk->prefix.xep184, xep184)) {
      g_free(blk->prefix.xep184);
      blk->prefix.xep184 = NULL;
      blk->prefix.flags ^= HBB_PREFIX_RECEIPT;
      return TRUE;
    }
  }
  return FALSE;
}

//  hbuf_set_readmark(hbuf, action)
// Set/Reset the readmark Flag
// If action is TRUE, set a mark to the latest line,
// if action is FALSE, remove a previous readmark flag.
void hbuf_set_readmark(GList *hbuf, gboolean action)
{
  hbuf_block *blk;

  if (!hbuf) return;

  hbuf = hbuf_previous_persistent(g_list_last(hbuf));

  if (action) {
    // Add a readmark flag
    blk = (hbuf_block*)(hbuf->data);
    blk->prefix.flags |= HBB_PREFIX_READMARK;

    // Shift hbuf in order to remove previous flags
    // (maybe it can be optimized out, if there's no risk
    //  we have several marks)
    hbuf = g_list_previous(hbuf);
  }

  // Remove old mark
  for ( ; hbuf; hbuf = g_list_previous(hbuf)) {
    blk = (hbuf_block*)(hbuf->data);
    if (blk->prefix.flags & HBB_PREFIX_READMARK) {
      blk->prefix.flags &= ~HBB_PREFIX_READMARK;
      break;
    }
  }
}

//  hbuf_remove_trailing_readmark(hbuf)
// Unset the buffer readmark if it is on the last line
void hbuf_remove_trailing_readmark(GList *hbuf)
{
  hbuf_block *blk;

  if (!hbuf) return;

  hbuf = g_list_last(hbuf);
  blk = (hbuf_block*)(hbuf->data);
  blk->prefix.flags &= ~HBB_PREFIX_READMARK;
}

//  hbuf_get_blocks_number()
// Returns the number of allocated hbuf_block's.
guint hbuf_get_blocks_number(GList *hbuf)
{
  hbuf_block *hbuf_b_elt;
  guint count = 0U;

  for (hbuf = g_list_first(hbuf); hbuf; hbuf = g_list_next(hbuf)) {
    hbuf_b_elt = (hbuf_block*)(hbuf->data);
    if (hbuf_b_elt->flags & HBB_FLAG_ALLOC)
      count++;
  }
  return count;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
