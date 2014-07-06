/*
 * compl.c      -- Completion system
 *
 * Copyright (C) 2005-2014 Mikael Berthe <mikael@lilotux.net>
 * Copyright (C) 2009-2014 Myhailo Danylenko <isbear@ukrpost.net>
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
#include "settings.h"
#include "logprint.h"

// Completion structure
typedef struct {
  GSList *list;         // list of matches
  guint len_prefix;     // length of text already typed by the user
  guint len_compl;      // length of the last completion
  GSList *next;         // pointer to next completion to try
} compl;

typedef GSList *(*compl_handler_t) (void); // XXX userdata? *dynlist?

// Category structure
typedef struct {
  guint flags;
  GSList *words;
  compl_handler_t dynamic;
} category;

#define COMPL_CAT_BUILTIN   0x01
#define COMPL_CAT_ACTIVE    0x02
#define COMPL_CAT_DYNAMIC   0x04
#define COMPL_CAT_REVERSE   0x10
#define COMPL_CAT_NOSORT    0x20

#define COMPL_CAT_USERFLAGS 0x30

static compl *InputCompl;
static category *Categories;
static guint num_categories;

// Dynamic completions callbacks
static GSList *compl_dyn_group (void)
{
  return compl_list(ROSTER_TYPE_GROUP);
}

static GSList *compl_dyn_user (void)
{
  return compl_list(ROSTER_TYPE_USER);
}

static GSList *compl_dyn_resource (void)
{
  return buddy_getresources_locale(NULL);
}

static GSList *compl_dyn_events (void)
{
  GSList *compl = evs_geteventslist();
  GSList *cel;
  for (cel = compl; cel; cel = cel->next)
    cel->data = g_strdup(cel->data);
  compl = g_slist_append(compl, g_strdup("list"));
  return compl;
}

static inline void register_builtin_cat(guint c, compl_handler_t dynamic) {
  Categories[c-1].flags   = COMPL_CAT_BUILTIN | COMPL_CAT_ACTIVE;
  Categories[c-1].words   = NULL;
  Categories[c-1].dynamic = dynamic;
  if (dynamic != NULL) {
    Categories[c-1].flags |= COMPL_CAT_DYNAMIC;
  }
}

void compl_init_system(void)
{
  num_categories = COMPL_MAX_ID;
#ifdef MODULES_ENABLE
  num_categories = ((num_categories / 16) + 1) * 16;
#endif
  Categories = g_new0(category, num_categories);

  // Builtin completion categories:
  register_builtin_cat(COMPL_CMD, NULL);
  register_builtin_cat(COMPL_JID, compl_dyn_user);
  register_builtin_cat(COMPL_URLJID, NULL);
  register_builtin_cat(COMPL_NAME, NULL);
  register_builtin_cat(COMPL_STATUS, NULL);
  register_builtin_cat(COMPL_FILENAME, NULL);
  register_builtin_cat(COMPL_ROSTER, NULL);
  register_builtin_cat(COMPL_BUFFER, NULL);
  register_builtin_cat(COMPL_GROUP, NULL);
  register_builtin_cat(COMPL_GROUPNAME, compl_dyn_group);
  register_builtin_cat(COMPL_MULTILINE, NULL);
  register_builtin_cat(COMPL_ROOM, NULL);
  register_builtin_cat(COMPL_RESOURCE, compl_dyn_resource);
  register_builtin_cat(COMPL_AUTH, NULL);
  register_builtin_cat(COMPL_REQUEST, NULL);
  register_builtin_cat(COMPL_EVENTS, NULL);
  register_builtin_cat(COMPL_EVENTSID, compl_dyn_events);
  register_builtin_cat(COMPL_PGP, NULL);
  register_builtin_cat(COMPL_COLOR, NULL);
  register_builtin_cat(COMPL_OTR, NULL);
  register_builtin_cat(COMPL_OTRPOLICY, NULL);
  register_builtin_cat(COMPL_MODULE, NULL);
  register_builtin_cat(COMPL_CARBONS, NULL);
}

#ifdef MODULES_ENABLE
//  compl_new_category(flags)
// Reserves id for new completion category.
// Flags determine word sorting order.
// Returns 0, if no more categories can be allocated.
guint compl_new_category(guint flags)
{
  guint i;
  for (i = 0; i < num_categories; i++)
    if (!(Categories[i].flags & COMPL_CAT_ACTIVE))
      break;
  if (i >= num_categories ) {
    guint j;
    if (num_categories > G_MAXUINT - 16) {
      scr_log_print(LPRINT_LOGNORM, "Warning: Too many "
                    "completion categories!");
      return 0;
    }
    num_categories += 16;
    Categories = g_renew(category, Categories, num_categories);
    for (j = i+1; j < num_categories; j++)
      Categories[j].flags = 0;
  }
  Categories[i].flags = COMPL_CAT_ACTIVE | (flags & COMPL_CAT_USERFLAGS);
  Categories[i].words = NULL;
  return i+1;
}

//  compl_del_category(id)
// Frees reserved id for category.
// Note, that for now it not validates its input, so, be careful
// and specify exactly what you get from compl_new_category.
void compl_del_category(guint compl)
{
  GSList *wel;

  if (!compl) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_del_category() - "
                                "Invalid category (0).");
    return;
  }

  compl--;

  if ((compl >= num_categories) ||
      (Categories[compl].flags & COMPL_CAT_BUILTIN)) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_del_category() "
                                "Invalid category.");
    return;
  }

  Categories[compl].flags = 0;
  for (wel = Categories[compl].words; wel; wel = g_slist_next (wel))
    g_free (wel -> data);
  g_slist_free (Categories[compl].words);
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
  guint  ret_len = 0;
  GSList *sl_cat;
  gint (*cmp)(const char *s1, const char *s2, size_t n);
  size_t len = strlen(prefix);

  if (InputCompl) { // This should not happen, but hey...
    scr_log_print(LPRINT_DEBUG, "Warning: new_completion() - "
                                "Previous completion exists!");
    done_completion();
  }

  if (settings_opt_get_int("completion_ignore_case"))
    cmp = &strncasecmp;
  else
    cmp = &strncmp;

  c = g_new0(compl, 1);
  // Build the list of matches
  for (sl_cat = compl_cat; sl_cat; sl_cat = g_slist_next(sl_cat)) {
    char *word = sl_cat->data;
    if (!cmp(prefix, word, len)) {
      if (strlen(word) != len) {
        gchar *compval;
        if (suffix)
          compval = g_strdup_printf("%s%s", word+len, suffix);
        else
          compval = g_strdup(word+len);
        // for a bit of efficiency, will reverse order afterwards
        c->list = g_slist_prepend(c->list, compval);
        ret_len ++;
      }
    }
  }
  c->next = c->list = g_slist_reverse (c->list);
  InputCompl = c;
  return ret_len;
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

static gint compl_sort_forward(gconstpointer a, gconstpointer b)
{
  return g_ascii_strcasecmp((const gchar *)a, (const gchar *)b);
}

static gint compl_sort_reverse(gconstpointer a, gconstpointer b)
{
  return -g_ascii_strcasecmp((const gchar *)a, (const gchar *)b);
}

static gint compl_sort_append(gconstpointer a, gconstpointer b)
{
  return 1;
}

static gint compl_sort_prepend(gconstpointer a, gconstpointer b)
{
  return -1;
}

//  compl_add_category_word(categ, command)
// Adds a keyword as a possible completion in category categ.
void compl_add_category_word(guint categ, const gchar *word)
{
  char *nword;

  if (!categ) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_add_category_word() - "
                  "Invalid category (0).");
    return;
  }

  categ--;

  if ((categ >= num_categories) ||
      !(Categories[categ].flags & COMPL_CAT_ACTIVE)) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_add_category_word() - "
                  "Category does not exist.");
    return;
  }

  // If word is not space-terminated, we add one trailing space
  for (nword = (char*)word; *nword; nword++)
    ;
  if (nword > word) nword--;
  if (*nword != ' ') {  // Add a space
    nword = g_strdup_printf("%s ", word);
  } else {              // word is fine
    nword = g_strdup(word);
  }

  if (g_slist_find_custom(Categories[categ].words, nword,
                          (GCompareFunc)g_strcmp0) == NULL) {
    guint flags = Categories[categ].flags;
    GCompareFunc comparator = compl_sort_forward;
    if (flags & COMPL_CAT_NOSORT) {
      if (flags & COMPL_CAT_REVERSE)
        comparator = compl_sort_prepend;
      else
        comparator = compl_sort_append;
    } else if (flags & COMPL_CAT_REVERSE)
      comparator = compl_sort_reverse;

    Categories[categ].words = g_slist_insert_sorted
                                  (Categories[categ].words, nword, comparator);
  }
}

//  compl_del_category_word(categ, command)
// Removes a keyword from category categ in completion list.
void compl_del_category_word(guint categ, const gchar *word)
{
  GSList *wel;
  char *nword;

  if (!categ) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_del_category_word() - "
                  "Invalid category (0).");
    return;
  }

  categ--;

  if ((categ >= num_categories) ||
      !(Categories[categ].flags & COMPL_CAT_ACTIVE)) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_del_category_word() - "
                  "Category does not exist.");
    return;
  }

  // If word is not space-terminated, we add one trailing space
  for (nword = (char*)word; *nword; nword++)
    ;
  if (nword > word) nword--;
  if (*nword != ' ')  // Add a space
    word = nword = g_strdup_printf("%s ", word);
  else
    nword = NULL;

  for (wel = Categories[categ].words; wel; wel = g_slist_next (wel)) {
    if (!strcasecmp((char*)wel->data, word)) {
      g_free(wel->data);
      Categories[categ].words = g_slist_delete_link
                                (Categories[categ].words, wel);
      break; // Only remove first occurence
    }
  }

  g_free (nword);
}

//  compl_get_category_list()
// Returns a slist of all words in the specified categorie.
// Iff this function sets *dynlist to TRUE, then the caller must free the
// whole list after use.
GSList *compl_get_category_list(guint categ, guint *dynlist)
{
  if (!categ) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_get_category_list() - "
                  "Invalid category (0).");
    return NULL;
  }

  categ --;

  if ((categ > num_categories) ||
      !(Categories[categ].flags & COMPL_CAT_ACTIVE)) {
    scr_log_print(LPRINT_DEBUG, "Error: compl_get_category_list() - "
                  "Category does not exist.");
    return NULL;
  }

  if (Categories[categ].flags & COMPL_CAT_DYNAMIC) {
    *dynlist = TRUE;
    return (*Categories[categ].dynamic) ();
  } else {
    *dynlist = FALSE;
    return Categories[categ].words;
  }
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
