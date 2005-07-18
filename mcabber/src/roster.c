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

#define _GNU_SOURCE     /* for strcasestr() */
#include <string.h>

#include "roster.h"


/* This is a private structure type for the roster */

typedef struct {
  const gchar *name;
  const gchar *jid;
  const gchar *status_msg;
  guint type;
  enum imstatus status;
  guint flags;
  // list: user -> points to his group; group -> points to its users list
  GSList *list;
} roster;


/* ### Variables ### */

static int hide_offline_buddies;
static GSList *groups;
static GSList *unread_list;
GList *buddylist;
GList *current_buddy;
GList *alternate_buddy;


/* ### Roster functions ### */

// Comparison function used to search in the roster (compares jids and types)
static gint roster_compare_jid_type(roster *a, roster *b) {
  if (! (a->type & b->type))
    return -1; // arbitrary (but should be != , of course)
  return strcasecmp(a->jid, b->jid);
}

// Comparison function used to sort the roster (by name)
static gint roster_compare_name(roster *a, roster *b) {
  return strcasecmp(a->name, b->name);
}

// Finds a roster element (user, group, agent...), by jid or name
// If roster_type is 0, returns match of any type.
// Returns the roster GSList element, or NULL if jid/name not found
GSList *roster_find(const char *jidname, enum findwhat type, guint roster_type)
{
  GSList *sl_roster_elt = groups;
  GSList *res;
  roster sample;
  GCompareFunc comp;

  if (!jidname) return NULL;

  if (!roster_type)
    roster_type = ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_GROUP;

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
GSList *roster_add_group(const char *name)
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
GSList *roster_add_user(const char *jid, const char *name, const char *group,
        guint type)
{
  roster *roster_usr;
  roster *my_group;
  GSList *slist;

  if ((type != ROSTER_TYPE_USER) && (type != ROSTER_TYPE_AGENT)) {
    // XXX Error message?
    return NULL;
  }

  // Let's be arbitrary: default group has an empty name ("").
  if (!group)  group = "";

  // #1 Check this user doesn't already exist
  slist = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (slist) return slist;
  // #2 add group if necessary
  slist = roster_add_group(group);
  if (!slist) return NULL;
  my_group = (roster*)slist->data;
  // #3 Create user node
  roster_usr = g_new0(roster, 1);
  roster_usr->jid   = g_strdup(jid);
  if (name) {
    roster_usr->name  = g_strdup(name);
  } else {
    gchar *p, *str = g_strdup(jid);
    p = strstr(str, "/");
    if (p)  *p = '\0';
    roster_usr->name = g_strdup(str);
    g_free(str);
  }
  roster_usr->type  = type; //ROSTER_TYPE_USER;
  roster_usr->list  = slist;    // (my_group SList element)
  // #4 Insert node (sorted)
  my_group->list = g_slist_insert_sorted(my_group->list, roster_usr,
          (GCompareFunc)&roster_compare_name);
  return roster_find(jid, jidsearch, type);
}

// Removes user (jid) from roster, frees allocated memory
void roster_del_user(const char *jid)
{
  GSList *sl_user, *sl_group;
  GSList **sl_group_listptr;
  roster *roster_usr;
  GSList *node;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return;
  roster_usr = (roster*)sl_user->data;

  // Remove (if present) from unread messages list
  node = g_slist_find(unread_list, roster_usr);
  if (node) unread_list = g_slist_delete_link(unread_list, node);

  // Let's free memory (jid, name, status message)
  if (roster_usr->jid)        g_free((gchar*)roster_usr->jid);
  if (roster_usr->name)       g_free((gchar*)roster_usr->name);
  if (roster_usr->status_msg) g_free((gchar*)roster_usr->status_msg);
  g_free(roster_usr);

  // That's a little complex, we need to dereference twice
  sl_group = ((roster*)sl_user->data)->list;
  sl_group_listptr = &((roster*)(sl_group->data))->list;
  *sl_group_listptr = g_slist_delete_link(*sl_group_listptr, sl_user);

  // We need to rebuild the list
  if (current_buddy)
    buddylist_build();
  // TODO What we could do, too, is to check if the deleted node is
  // current_buddy, in which case we could move current_buddy to the
  // previous (or next) node.
}

// Free all roster data and call buddylist_build() to free the buddylist.
void roster_free(void)
{
  GSList *sl_grp = groups;

  // Free unread_list
  if (unread_list) {
    g_slist_free(unread_list);
    unread_list = NULL;
  }

  // Walk through groups
  while (sl_grp) {
    roster *roster_grp = (roster*)sl_grp->data;
    GSList *sl_usr = roster_grp->list;
    // Walk through this group users
    while (sl_usr) {
      roster *roster_usr = (roster*)sl_usr->data;
      // Free name and jid
      if (roster_usr->jid)        g_free((gchar*)roster_usr->jid);
      if (roster_usr->name)       g_free((gchar*)roster_usr->name);
      if (roster_usr->status_msg) g_free((gchar*)roster_usr->status_msg);
      g_free(roster_usr);
      sl_usr = g_slist_next(sl_usr);
    }
    // Free group's users list
    if (roster_grp->list)
      g_slist_free(roster_grp->list);
    // Free group's name and jid
    if (roster_grp->jid)  g_free((gchar*)roster_grp->jid);
    if (roster_grp->name) g_free((gchar*)roster_grp->name);
    g_free(roster_grp);
    sl_grp = g_slist_next(sl_grp);
  }
  // Free groups list
  if (groups) {
    g_slist_free(groups);
    groups = NULL;
    // Update (i.e. free) buddylist
    if (buddylist)
      buddylist_build();
  }
}

void roster_setstatus(const char *jid, enum imstatus bstat,
        const char *status_msg)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  // If we can't find it, we add it
  if (sl_user == NULL)
    sl_user = roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER);

  roster_usr = (roster*)sl_user->data;
  roster_usr->status = bstat;
  if (roster_usr->status_msg) {
    g_free((gchar*)roster_usr->status_msg);
    roster_usr->status_msg = NULL;
  }
  if (status_msg)
    roster_usr->status_msg = g_strdup(status_msg);
}

//  roster_setflags()
// Set one or several flags to value (TRUE/FALSE)
void roster_setflags(const char *jid, guint flags, guint value)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return;

  roster_usr = (roster*)sl_user->data;
  if (value)
    roster_usr->flags |= flags;
  else
    roster_usr->flags &= ~flags;
}

//  roster_msg_setflag()
// Set the ROSTER_FLAG_MSG to the given value for the given jid.
// It will update the buddy's group message flag.
// Update the unread messages list too.
void roster_msg_setflag(const char *jid, guint value)
{
  GSList *sl_user;
  roster *roster_usr, *roster_grp;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return;

  roster_usr = (roster*)sl_user->data;
  roster_grp = (roster*)roster_usr->list->data;
  if (value) {
    // Message flag is TRUE.  This is easy, we just have to set both flags
    // to TRUE...
    roster_usr->flags |= ROSTER_FLAG_MSG;
    roster_grp->flags |= ROSTER_FLAG_MSG; // group
    // Append the roster_usr to unread_list, but avoid duplicates
    if (!g_slist_find(unread_list, roster_usr))
      unread_list = g_slist_append(unread_list, roster_usr);
  } else {
    // Message flag is FALSE.
    guint msg = FALSE;
    roster_usr->flags &= ~ROSTER_FLAG_MSG;
    if (unread_list) {
      GSList *node = g_slist_find(unread_list, roster_usr);
      if (node) unread_list = g_slist_delete_link(unread_list, node);
    }
    // For the group value we need to watch all buddies in this group;
    // if one is flagged, then the group will be flagged.
    // I will re-use sl_user and roster_usr here, as they aren't used
    // anymore.
    sl_user = roster_grp->list;
    while (sl_user) {
      roster_usr = (roster*)sl_user->data;
      if (roster_usr->flags & ROSTER_FLAG_MSG) {
        msg = TRUE;
        break;
      }
      sl_user = g_slist_next(sl_user);
    }
    if (!msg)
      roster_grp->flags &= ~ROSTER_FLAG_MSG;
    else
      roster_grp->flags |= ROSTER_FLAG_MSG;
      // Actually the "else" part is useless, because the group
      // ROSTER_FLAG_MSG should already be set...
  }
}

void roster_settype(const char *jid, guint type)
{
  GSList *sl_user;
  roster *roster_usr;

  if ((sl_user = roster_find(jid, jidsearch, 0)) == NULL)
    return;

  roster_usr = (roster*)sl_user->data;
  roster_usr->type = type;
}

enum imstatus roster_getstatus(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return offline; // Not in the roster, anyway...

  roster_usr = (roster*)sl_user->data;
  return roster_usr->status;
}

const char *roster_getstatusmsg(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return offline; // Not in the roster, anyway...

  roster_usr = (roster*)sl_user->data;
  return roster_usr->status_msg;
}

guint roster_gettype(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  if ((sl_user = roster_find(jid, jidsearch, 0)) == NULL)
    return 0;

  roster_usr = (roster*)sl_user->data;
  return roster_usr->type;
}

inline guint roster_exists(const char *jidname, enum findwhat type,
        guint roster_type)
{
  if (roster_find(jidname, type, roster_type))
    return TRUE;
  return FALSE;
}


/* ### BuddyList functions ### */

//  buddylist_set_hide_offline_buddies(hide)
// "hide" values: 1=hide 0=show_all -1=invert
void buddylist_set_hide_offline_buddies(int hide)
{
  if (hide < 0)                     // NEG   (invert)
    hide_offline_buddies = !hide_offline_buddies;
  else if (hide == 0)               // FALSE (don't hide)
    hide_offline_buddies = 0;
  else                              // TRUE  (hide)
    hide_offline_buddies = 1;
}

inline int buddylist_get_hide_offline_buddies(void)
{
  return hide_offline_buddies;
}

//  buddylist_build()
// Creates the buddylist from the roster entries.
void buddylist_build(void)
{
  GSList *sl_roster_elt = groups;
  roster *roster_elt;
  roster *roster_current_buddy = NULL;
  roster *roster_alternate_buddy = NULL;
  int shrunk_group;

  // We need to remember which buddy is selected.
  if (current_buddy)
    roster_current_buddy = BUDDATA(current_buddy);
  current_buddy = NULL;
  if (alternate_buddy)
    roster_alternate_buddy = BUDDATA(alternate_buddy);
  alternate_buddy = NULL;

  // Destroy old buddylist
  if (buddylist) {
    g_list_free(buddylist);
    buddylist = NULL;
  }

  // Create the new list
  while (sl_roster_elt) {
    GSList *sl_roster_usrelt;
    roster *roster_usrelt;
    guint pending_group = FALSE;
    roster_elt = (roster*) sl_roster_elt->data;

    // Add the group now unless hide_offline_buddies is set,
    // in which case we'll add it only if an online buddy belongs to it.
    // We take care to keep the current_buddy in the list, too.
    if (!hide_offline_buddies || roster_elt == roster_current_buddy)
      buddylist = g_list_append(buddylist, roster_elt);
    else
      pending_group = TRUE;

    shrunk_group = roster_elt->flags & ROSTER_FLAG_HIDE;

    sl_roster_usrelt = roster_elt->list;
    while (sl_roster_usrelt) {
      roster_usrelt = (roster*) sl_roster_usrelt->data;

      // Buddy will be added if either:
      // - hide_offline_buddies is FALSE
      // - buddy is not offline
      // - buddy has a lock (for example the buddy window is currently open)
      // - buddy has a pending (non-read) message
      // - group isn't hidden (shrunk)
      // - this is the current_buddy
      if (!hide_offline_buddies || roster_usrelt == roster_current_buddy ||
          (buddy_getstatus((gpointer)roster_usrelt) != offline) ||
          (buddy_getflags((gpointer)roster_usrelt) &
               (ROSTER_FLAG_LOCK | ROSTER_FLAG_MSG))) {
        // This user should be added.  Maybe the group hasn't been added yet?
        if (pending_group &&
            (hide_offline_buddies || roster_usrelt == roster_current_buddy)) {
          // It hasn't been done yet
          buddylist = g_list_append(buddylist, roster_elt);
          pending_group = FALSE;
        }
        // Add user
        // XXX Should we add the user if there is a message and
        //     the group is shrunk? If so, we'd need to check LOCK flag too,
        //     perhaps...
        if (!shrunk_group)
          buddylist = g_list_append(buddylist, roster_usrelt);
      }

      sl_roster_usrelt = g_slist_next(sl_roster_usrelt);
    }
    sl_roster_elt = g_slist_next(sl_roster_elt);
  }

  // Check if we can find our saved current_buddy...
  if (roster_current_buddy)
    current_buddy = g_list_find(buddylist, roster_current_buddy);
  if (roster_alternate_buddy)
    alternate_buddy = g_list_find(buddylist, roster_alternate_buddy);
  // current_buddy initialization
  if (!current_buddy || (g_list_position(buddylist, current_buddy) == -1))
    current_buddy = g_list_first(buddylist);
  // XXX Maybe we should set update_roster to TRUE there?
}

//  buddy_hide_group(roster, hide)
// "hide" values: 1=hide 0=show_all -1=invert
void buddy_hide_group(gpointer rosterdata, int hide)
{
  roster *roster_usr = rosterdata;
  if (hide > 0)                     // TRUE   (hide)
    roster_usr->flags |= ROSTER_FLAG_HIDE;
  else if (hide < 0)                // NEG    (invert)
    roster_usr->flags ^= ROSTER_FLAG_HIDE;
  else                              // FALSE  (don't hide)
    roster_usr->flags &= ~ROSTER_FLAG_HIDE;
}

const char *buddy_getjid(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->jid;
}

//  buddy_setgroup()
// Change the group of current buddy
//
// Warning!  This function changes current_buddy!
// Warning!  Old buddy is deleted, so you can't acces to its jid/name after
//           calling this function.
void buddy_setgroup(gpointer rosterdata, char *newgroupname)
{
  roster *roster_usr = rosterdata;
  GSList **sl_group;
  GSList *sl_clone;
  roster *roster_clone;
  int is_alternate;

  // A group has no group :)
  if (roster_usr->type & ROSTER_TYPE_GROUP) return;

  // Remove the buddy from current group
  sl_group = &((roster*)((GSList*)roster_usr->list)->data)->list;
  *sl_group = g_slist_remove(*sl_group, rosterdata);
  
  // Add the buddy to its new group; actually we "clone" this buddy...
  sl_clone = roster_add_user(roster_usr->jid, roster_usr->name,
          newgroupname, roster_usr->type);
  roster_clone = (roster*)sl_clone->data;
  roster_clone->status = roster_usr->status;
  roster_clone->flags  = roster_usr->flags;

  // Free old buddy
  if (roster_usr->jid)        g_free((gchar*)roster_usr->jid);
  if (roster_usr->name)       g_free((gchar*)roster_usr->name);
  if (roster_usr->status_msg) g_free((gchar*)roster_usr->status_msg);
  g_free(roster_usr);

  // If new new group is folded, the curren_buddy will be lost, and the
  // chat window won't be correctly refreshed.  So we make sure it isn't...
  ((roster*)((GSList*)roster_clone->list)->data)->flags &= ~ROSTER_FLAG_HIDE;

  // Little trick to have current_body pointing to the cloned buddy
  is_alternate = (alternate_buddy == current_buddy);
  buddylist = g_list_append(buddylist, roster_clone);
  current_buddy = g_list_find(buddylist, roster_clone);
  if (is_alternate)
    alternate_buddy = current_buddy;

  buddylist_build();
}

void buddy_setname(gpointer rosterdata, char *newname)
{
  roster *roster_usr = rosterdata;
  GSList **sl_group;

  // TODO For groups, we need to check for unicity
  // However, renaming a group boils down to moving all its buddies to
  // another group, so calling this function is not really necessary...
  if (roster_usr->type & ROSTER_TYPE_GROUP) return;

  if (roster_usr->name) {
    g_free((gchar*)roster_usr->name);
    roster_usr->name = NULL;
  }
  if (newname)
    roster_usr->name = g_strdup(newname);

  // We need to resort the group list
  sl_group = &((roster*)((GSList*)roster_usr->list)->data)->list;
  *sl_group = g_slist_sort(*sl_group, (GCompareFunc)&roster_compare_name);

  buddylist_build();
}

const char *buddy_getname(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->name;
}

//  buddy_getgroupname()
// Returns a pointer on buddy's group name.
const char *buddy_getgroupname(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;

  if (roster_usr->type & ROSTER_TYPE_GROUP)
    return roster_usr->name;

  // This is a user
  return ((roster*)((GSList*)roster_usr->list)->data)->name;
}

//  buddy_getgroup()
// Returns a pointer on buddy's group.
gpointer buddy_getgroup(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;

  if (roster_usr->type & ROSTER_TYPE_GROUP)
    return rosterdata;

  // This is a user
  return (gpointer)((GSList*)roster_usr->list)->data;
}

guint buddy_gettype(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->type;
}

enum imstatus buddy_getstatus(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->status;
}

const char *buddy_getstatusmsg(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->status_msg;
}

//  buddy_setflags()
// Set one or several flags to value (TRUE/FALSE)
void buddy_setflags(gpointer rosterdata, guint flags, guint value)
{
  roster *roster_usr = rosterdata;
  if (value)
    roster_usr->flags |= flags;
  else
    roster_usr->flags &= ~flags;
}

guint buddy_getflags(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->flags;
}

//  buddy_search(string)
// Look for a buddy whose name or jid contains string.
// Search begins at current_buddy; if no match is found in the the buddylist,
// return NULL;
GList *buddy_search(char *string)
{
  GList *buddy = current_buddy;
  roster *roster_usr;
  if (!buddylist || !current_buddy) return NULL;
  for (;;) {
    buddy = g_list_next(buddy);
    if (!buddy)
      buddy = buddylist;

    roster_usr = (roster*)buddy->data;
    if (roster_usr->jid && strcasestr(roster_usr->jid, string))
      return buddy;
    if (roster_usr->name && strcasestr(roster_usr->name, string))
      return buddy;

    if (buddy == current_buddy)
      return NULL; // Back to the beginning, and no match found
  }
}

//  compl_list(type)
// Returns a list of jid's or groups.  (For commands completion)
// type: ROSTER_TYPE_USER (jid's) or ROSTER_TYPE_GROUP (group names)
// The list should be freed by the caller after use.
GSList *compl_list(guint type)
{
  GSList *list = NULL;
  GList  *buddy = buddylist;

  for ( ; buddy ; buddy = g_list_next(buddy)) {
    guint btype = buddy_gettype(BUDDATA(buddy));

    if (type == ROSTER_TYPE_GROUP) { // (group names)
      if (btype == ROSTER_TYPE_GROUP) {
        const char *bname = buddy_getname(BUDDATA(buddy));
        if ((bname) && (*bname))
          list = g_slist_append(list, g_strdup(bname));
      }
    } else { // ROSTER_TYPE_USER (jid)
        const char *bjid = buddy_getjid(BUDDATA(buddy));
        if (bjid)
          list = g_slist_append(list, g_strdup(bjid));
    }
  }

  return list;
}

//  unread_msg(rosterdata)
// Return the next buddy with an unread message.  If the parameter is NULL,
// return the first buddy with an unread message.
gpointer unread_msg(gpointer rosterdata)
{
  GSList *unread, *next_unread;

  if (!unread_list) return NULL;
  // First unread message
  if (!rosterdata) return unread_list->data;

  unread = g_slist_find(unread_list, rosterdata);
  if (!unread) return unread_list->data;

  next_unread = g_slist_next(unread);
  if (next_unread) return next_unread->data;

  return unread_list->data;
}

