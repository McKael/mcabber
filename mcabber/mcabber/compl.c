/*
 * compl.c      -- Completion system
 *
 * Copyright (C) 2005-2010 Mikael Berthe <mikael@lilotux.net>
 * Copyright (C) 2009,2010 Myhailo Danylenko <isbear@ukrpost.net>
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

/*  Usage, basically:
 * - new_completion();      // 1.   Initialization
 * - complete();            // 2.   1st completion
 * - cancel_completion();   // 3a.  2nd completion / cancel previous
 * - complete();            // 3b.  2nd completion / complete
 *   ...
 * - done_completion();     // n.   finished -- free allocated areas
 *
 */

#include <string.h>

#include "compl.h"
#include "utf8.h"
#include "roster.h"
#include "events.h"

// Completion structure
typedef struct {
  GSList *list;         // list of matches
  guint len_prefix;     // length of text already typed by the user
  guint len_compl;      // length of the last completion
  GSList *next;         // pointer to next completion to try
} compl;

// Category structure
typedef struct {
  guint flag;
  GSList *words;
} category;

static GSList *Categories;
static compl *InputCompl;

#ifdef MODULES_ENABLE
guint registered_cats = COMPL_CMD|COMPL_JID|COMPL_URLJID|COMPL_NAME| \
                        COMPL_STATUS|COMPL_FILENAME|COMPL_ROSTER|COMPL_BUFFER| \
                        COMPL_GROUP|COMPL_GROUPNAME|COMPL_MULTILINE|COMPL_ROOM| \
                        COMPL_RESOURCE|COMPL_AUTH|COMPL_REQUEST|COMPL_EVENTS| \
                        COMPL_EVENTSID|COMPL_PGP|COMPL_COLOR| \
                        COMPL_OTR|COMPL_OTRPOLICY| \
                        0;

//  compl_new_category()
// Reserves id for new completion category.
// Returns 0, if no more categories can be allocated.
// Note, that user should not make any assumptions about id nature,
// as it is likely to change in future.
guint compl_new_category(void)
{
  guint i = 0;
  while ((registered_cats >> i) & 1)
    i++;
  if (i >= 8 * sizeof (guint))
    return 0;
  else {
    guint id = 1 << i;
    registered_cats |= id;
    return id;
  }
}

//  compl_del_category(id)
// Frees reserved id for category.
// Note, that for now it not validates its input, so, be careful
// and specify exactly what you get from compl_new_category.
void compl_del_category(guint id)
{
  registered_cats &= ~id;
}
#endif

//  new_completion(prefix, compl_cat, suffix)
// . prefix    = beginning of the word, typed by the user
// . compl_cat = pointer to a completion category list (list of *char)
// . suffix    = string to append to all completion possibilities (i.e. ":")
// Set the InputCompl pointer to an allocated compl structure.
// done_completion() must be called when finished.
// Returns the number of possible completions.
guint new_completion(const char *prefix, GSList *compl_cat, const gchar *suffix)
{
  compl *c;
  GSList *sl_cat;
  size_t len = strlen(prefix);

  if (InputCompl) { // This should not happen, but hey...
    cancel_completion();
  }

  c = g_new0(compl, 1);
  // Build the list of matches
  for (sl_cat = compl_cat; sl_cat; sl_cat = g_slist_next(sl_cat)) {
    char *word = sl_cat->data;
    if (!strncasecmp(prefix, word, len)) {
      if (strlen(word) != len) {
        gchar *compval;
        if (suffix)
          compval = g_strdup_printf("%s%s", word+len, suffix);
        else
          compval = g_strdup(word+len);
        c->list = g_slist_insert_sorted(c->list, compval,
                                        (GCompareFunc)g_ascii_strcasecmp);
      }
    }
  }
  c->next = c->list;
  InputCompl = c;
  return g_slist_length(c->list);
}

//  done_completion();
void done_completion(void)
{
  GSList *clp;

  if (!InputCompl)  return;

  // Free the current completion list
  for (clp = InputCompl->list; clp; clp = g_slist_next(clp))
    g_free(clp->data);
  g_slist_free(InputCompl->list);
  g_free(InputCompl);
  InputCompl = NULL;
}

//  cancel_completion()
// Returns the number of chars to delete to cancel the completion
guint cancel_completion(void)
{
  if (!InputCompl)  return 0;
  return InputCompl->len_compl;
}

// Returns pointer to text to insert, NULL if no completion.
const char *complete()
{
  compl* c = InputCompl;
  char *r;

  if (!InputCompl)  return NULL;

  if (!c->next) {
    c->next = c->list;  // back to the beginning
    c->len_compl = 0;
    return NULL;
  }
  r = (char*)c->next->data;
  c->next = g_slist_next(c->next);
  if (!utf8_mode) {
    c->len_compl = strlen(r);
  } else {
    char *wc;
    c->len_compl = 0;
    for (wc = r; *wc; wc = next_char(wc))
      c->len_compl++;
  }
  return r;
}


/* Categories functions */

//  compl_add_category_word(categ, command)
// Adds a keyword as a possible completion in category categ.
void compl_add_category_word(guint categ, const gchar *word)
{
  GSList *sl_cat;
  category *cat;
  char *nword;
  // Look for category
  for (sl_cat=Categories; sl_cat; sl_cat = g_slist_next(sl_cat)) {
    if (categ == ((category*)sl_cat->data)->flag)
      break;
  }
  if (!sl_cat) {   // Category not found, let's create it
    cat = g_new0(category, 1);
    cat->flag = categ;
    Categories = g_slist_append(Categories, cat);
  } else
    cat = (category*)sl_cat->data;

  // If word is not space-terminated, we add one trailing space
  for (nword = (char*)word; *nword; nword++)
    ;
  if (nword > word) nword--;
  if (*nword != ' ') {  // Add a space
    nword = g_strdup_printf("%s ", word);
  } else {              // word is fine
    nword = g_strdup(word);
  }

  if (g_slist_find_custom(cat->words, nword, (GCompareFunc)g_strcmp0) != NULL)
    return;

  cat->words = g_slist_insert_sorted(cat->words, nword,
                                     (GCompareFunc)g_ascii_strcasecmp);
}

//  compl_del_category_word(categ, command)
// Removes a keyword from category categ in completion list.
void compl_del_category_word(guint categ, const gchar *word)
{
  GSList *sl_cat, *sl_elt;
  category *cat;
  char *nword;
  // Look for category
  for (sl_cat=Categories; sl_cat; sl_cat = g_slist_next(sl_cat)) {
    if (categ == ((category*)sl_cat->data)->flag)
      break;
  }
  if (!sl_cat) return;   // Category not found, finished!

  cat = (category*)sl_cat->data;

  // If word is not space-terminated, we add one trailing space
  for (nword = (char*)word; *nword; nword++)
    ;
  if (nword > word) nword--;
  if (*nword != ' ') {  // Add a space
    nword = g_strdup_printf("%s ", word);
  } else {              // word is fine
    nword = g_strdup(word);
  }

  sl_elt = cat->words;
  while (sl_elt) {
    if (!strcasecmp((char*)sl_elt->data, nword)) {
      g_free(sl_elt->data);
      cat->words = g_slist_delete_link(cat->words, sl_elt);
      break; // Only remove first occurence
    }
    sl_elt = g_slist_next(sl_elt);
  }
}

//  compl_get_category_list()
// Returns a slist of all words in the categories specified by the given flags
// Iff this function sets *dynlist to TRUE, then the caller must free the
// whole list after use.
GSList *compl_get_category_list(guint cat_flags, guint *dynlist)
{
  GSList *sl_cat;

  *dynlist = FALSE;

  // Look for category
  // XXX Actually that's not that simple... cat_flags can be a combination
  // of several flags!
  for (sl_cat=Categories; sl_cat; sl_cat = g_slist_next(sl_cat)) {
    if (cat_flags == ((category*)sl_cat->data)->flag)
      break;
  }
  if (sl_cat)       // Category was found, easy...
    return ((category*)sl_cat->data)->words;

  // Handle dynamic SLists
  *dynlist = TRUE;
  if (cat_flags == COMPL_GROUPNAME) {
    return compl_list(ROSTER_TYPE_GROUP);
  }
  if (cat_flags == COMPL_JID) {
    return compl_list(ROSTER_TYPE_USER);
  }
  if (cat_flags == COMPL_RESOURCE) {
    return buddy_getresources_locale(NULL);
  }
  if (cat_flags == COMPL_EVENTSID) {
    GSList *compl = evs_geteventslist();
    GSList *cel;
    for (cel = compl; cel; cel = cel->next)
      cel->data = g_strdup(cel->data);
    compl = g_slist_append(compl, g_strdup("list"));
    return compl;
  }

  *dynlist = FALSE;
  return NULL;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
