/*
 * roster.c     -- Local roster implementation
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

#include "roster.h"


/* This is a private structure type for the roster */

typedef struct {
  char *name;
  char *jid;
  guint type;
  enum imstatus status;
  guint flags;
  // list: user -> points to his group; group -> points to its users list
  GSList *list;
} roster;


/* ### Variables ### */

static int hide_offline_buddies;
static GSList *groups;
GList *buddylist;

#ifdef MCABBER_TESTUNIT
// Export groups for testing routines
GSList **pgroups = &groups;
#endif


/* ### Roster functions ### */

// Comparison function used to search in the roster (compares jids and types)
gint roster_compare_jid_type(roster *a, roster *b) {
  if (a->type != b->type)
    return -1; // arbitrary (but should be != , of course)
  return strcasecmp(a->jid, b->jid);
}

// Comparison function used to sort the roster (by name)
gint roster_compare_name(roster *a, roster *b) {
  return strcasecmp(a->name, b->name);
}

// Finds a roster element (user, group, agent...), by jid or name
// Returns the roster GSList element, or NULL if jid/name not found
GSList *roster_find(char *jidname, enum findwhat type, guint roster_type)
{
  GSList *sl_roster_elt = groups;
  GSList *res;
  roster sample;
  GCompareFunc comp;

  if (!jidname)
    return NULL;    // should not happen

  sample.type = roster_type;
  if (type == jidsearch) {
    sample.jid = jidname;
    comp = (GCompareFunc)&roster_compare_jid_type;
  } else if (type == namesearch) {
    sample.name = jidname;
    comp = (GCompareFunc)&roster_compare_name;
  } else
    return NULL;    // should not happen

  while (sl_roster_elt) {
    roster *roster_elt = (roster*)sl_roster_elt->data;
    if (roster_type & ROSTER_TYPE_GROUP) {
      if ((type == namesearch) && !strcasecmp(jidname, roster_elt->name))
        return sl_roster_elt;
    } else {
      res = g_slist_find_custom(roster_elt->list, &sample, comp);
      if (res)
        return res;
    }
    sl_roster_elt = g_slist_next(sl_roster_elt);
  }
  return NULL;
}

// Returns pointer to new group, or existing group with that name
GSList *roster_add_group(char *name)
{
  roster *roster_grp;
  // #1 Check name doesn't already exist
  if (!roster_find(name, namesearch, ROSTER_TYPE_GROUP)) {
    // #2 Create the group node
    roster_grp = g_new0(roster, 1);
    roster_grp->name = g_strdup(name);
    roster_grp->type = ROSTER_TYPE_GROUP;
    // #3 Insert (sorted)
    groups = g_slist_insert_sorted(groups, roster_grp,
            (GCompareFunc)&roster_compare_name);
  }
  return roster_find(name, namesearch, ROSTER_TYPE_GROUP);
}

// Returns a pointer to the new user, or existing user with that name
GSList *roster_add_user(char *jid, char *name, char *group, guint type)
{
  roster *roster_usr;
  roster *my_group;
  GSList *slist;

  if ((type != ROSTER_TYPE_USER) && (type != ROSTER_TYPE_AGENT)) {
    // XXX Error message?
    return NULL;
  }

  // #1 Check this user doesn't already exist
  if ((slist = roster_find(jid, jidsearch, type)) != NULL)
    return slist;
  // #2 add group if necessary
  slist = roster_add_group(group);
  if (!slist) return NULL;
  my_group = (roster*)slist->data;
  // #3 Create user node
  roster_usr = g_new0(roster, 1);
  roster_usr->jid   = g_strdup(jid);
  roster_usr->name  = g_strdup(name);
  roster_usr->type  = type; //ROSTER_TYPE_USER;
  roster_usr->list  = slist;    // (my_group SList element)
  // #4 Insert node (sorted)
  my_group->list = g_slist_insert_sorted(my_group->list, roster_usr,
          (GCompareFunc)&roster_compare_name);
  return roster_find(jid, jidsearch, type);
}

// Removes user (jid) from roster, frees allocated memory
void roster_del_user(char *jid)
{
  GSList *sl_user, *sl_group;
  GSList **sl_group_listptr;
  roster *roster_usr;

  if ((sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER)) == NULL)
    return;
  // Let's free memory (jid, name)
  roster_usr = (roster*)sl_user->data;
  if (roster_usr->jid)
    g_free(roster_usr->jid);
  if (roster_usr->name)
    g_free(roster_usr->name);

  // That's a little complex, we need to dereference twice
  sl_group = ((roster*)sl_user->data)->list;
  sl_group_listptr = &((roster*)(sl_group->data))->list;
  *sl_group_listptr = g_slist_delete_link(*sl_group_listptr, sl_user);
}

void roster_setstatus(char *jid, enum imstatus bstat)
{
  GSList *sl_user;
  roster *roster_usr;

  if ((sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER)) == NULL)
    return;

  roster_usr = (roster*)sl_user->data;
  roster_usr->status = bstat;
}

// char *roster_getgroup(...)   / Or *GSList?  Which use??
// ... setgroup(char*) ??
// guint  roster_gettype(...)   / settype
// guchar roster_getflags(...)  / setflags
// guchar roster_getname(...)   / setname ??
// roster_del_group?


/* ### BuddyList functions ### */

//  buddylist_hide_offline_buddies(hide)
// "hide" values: 1=hide 0=show_all -1=invert
void buddylist_hide_offline_buddies(int hide)
{
  if (hide < 0)                     // NEG   (invert)
    hide_offline_buddies = !hide_offline_buddies;
  else if (hide == 0)               // FALSE (don't hide)
    hide_offline_buddies = 0;
  else                              // TRUE  (hide)
    hide_offline_buddies = 1;
}

//  buddylist_build()
// Creates the buddylist from the roster entries.
void buddylist_build(void)
{
  GSList *sl_roster_elt = groups;
  roster *roster_elt;
  int pending_group;

  // Destroy old buddylist
  if (buddylist) {
    g_list_free(buddylist);
    buddylist = NULL;
  }

  // Create the new list
  while (sl_roster_elt) {
    GSList *sl_roster_usrelt;
    roster *roster_usrelt;
    roster_elt = (roster*) sl_roster_elt->data;

    // Add the group now unless hide_offline_buddies is set,
    // in which case we'll add it only if an online buddy belongs to it.
    if (!hide_offline_buddies)
      buddylist = g_list_append(buddylist, roster_elt);
    else
       pending_group = TRUE;

    sl_roster_usrelt = roster_elt->list;
    while (sl_roster_usrelt) {
      roster_usrelt = (roster*) sl_roster_usrelt->data;

      // Buddy will be added if either:
      // - hide_offline_buddies is FALSE
      // - buddy is not offline
      // - buddy has a lock (for example the buddy window is currently open)
      // - buddy has a pending (non-read) message
      if (!hide_offline_buddies ||
          (buddy_getstatus((gpointer)roster_usrelt) != offline) ||
          (buddy_getflags((gpointer)roster_usrelt) &
               (ROSTER_FLAG_LOCK | ROSTER_FLAG_MSG))) {
        // This user should be added.  Maybe the group hasn't been added yet?
        if (hide_offline_buddies && pending_group) {
          // It hasn't been done yet
          buddylist = g_list_append(buddylist, roster_elt);
          pending_group = FALSE;
        }
        // Add user
        buddylist = g_list_append(buddylist, roster_usrelt);
      }

      sl_roster_usrelt = g_slist_next(sl_roster_usrelt);
    }
    sl_roster_elt = g_slist_next(sl_roster_elt);
  }
}

//  buddy_hide_group(roster, hide)
// "hide" values: 1=hide 0=show_all -1=invert
void buddy_hide_group(gpointer rosterdata, int hide)
{
  roster *roster = rosterdata;
  if (hide > 0)                     // TRUE   (hide)
    roster->flags |= ROSTER_FLAG_HIDE;
  else if (hide < 0)                // NEG    (invert)
    roster->flags ^= ROSTER_FLAG_HIDE;
  else                              // FALSE  (don't hide)
    roster->flags &= ~ROSTER_FLAG_HIDE;
}

const char *buddy_getjid(gpointer rosterdata)
{
  roster *roster = rosterdata;
  return roster->jid;
}

const char *buddy_getname(gpointer rosterdata)
{
  roster *roster = rosterdata;
  return roster->name;
}

guint buddy_gettype(gpointer rosterdata)
{
  roster *roster = rosterdata;
  return roster->type;
}

enum imstatus buddy_getstatus(gpointer rosterdata)
{
  roster *roster = rosterdata;
  return roster->status;
}

guint buddy_getflags(gpointer rosterdata)
{
  roster *roster = rosterdata;
  return roster->flags;
}

