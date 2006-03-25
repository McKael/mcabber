/*
 * roster.c     -- Local roster implementation
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

#define _GNU_SOURCE     /* for strcasestr() */
#include <string.h>

#include "roster.h"
#include "utils.h"


char *strrole[] = { /* Should match enum in roster.h */
  "none",
  "moderator",
  "participant",
  "visitor"
};

char *straffil[] = { /* Should match enum roster.h */
  "none",
  "owner",
  "admin",
  "memeber",
  "outcast"
};

/* Resource structure */

typedef struct {
  gchar *name;
  gchar prio;
  enum imstatus status;
  gchar *status_msg;
  time_t status_timestamp;
  enum imrole role;
  enum imaffiliation affil;
  gchar *realjid;       /* for chatrooms, if buddy's real jid is known */
} res;

/* This is a private structure type for the roster */

typedef struct {
  gchar *name;
  gchar *jid;
  guint type;
  enum subscr subscription;
  GSList *resource;

  /* For groupchats */
  gchar *nickname;
  gchar *topic;
  guint8 inside_room;

  /* Flag used for the UI */
  guint flags;

  // list: user -> points to his group; group -> points to its users list
  GSList *list;
} roster;


/* ### Variables ### */

static int hide_offline_buddies;
static GSList *groups;
static GSList *unread_list;
static GHashTable *unread_jids;
GList *buddylist;
GList *current_buddy;
GList *alternate_buddy;

void unread_jid_add(const char *jid);
int  unread_jid_del(const char *jid);


/* ### Resources functions ### */

static void free_all_resources(GSList **reslist)
{
  GSList *lip;
  res *p_res;

  for ( lip = *reslist; lip ; lip = g_slist_next(lip)) {
    p_res = (res*)lip->data;
    if (p_res->status_msg) {
      g_free((gchar*)p_res->status_msg);
    }
    if (p_res->name) {
      g_free((gchar*)p_res->name);
    }
    if (p_res->realjid) {
      g_free((gchar*)p_res->realjid);
    }
  }
  // Free all nodes but the first (which is static)
  g_slist_free(*reslist);
  *reslist = NULL;
}

// Resources are sorted in ascending order
static gint resource_compare_prio(res *a, res *b) {
  //return (a->prio - b->prio);
  if (a->prio < b->prio) return -1;
  else                   return 1;
}

//  get_resource(rost, resname)
// Return a pointer to the resource with name resname, in rost's resources list
// - if rost has no resources, return NULL
// - if resname is defined, return the match or NULL
// - if resname is NULL, the last resource is returned, currently
//   This could change in the future, because we should return the best one
//   (priority? last used? and fall back to the first resource)
//
static res *get_resource(roster *rost, const char *resname)
{
  GSList *p;
  res *r = NULL;

  for (p = rost->resource; p; p = g_slist_next(p)) {
    r = p->data;
    if (resname && !strcmp(r->name, resname))
      return r;
  }

  // The last resource is one of the resources with the highest priority,
  // however, we don't know if it is the more-recently-used.
  if (!resname) return r;
  return NULL;
}

//  get_or_add_resource(rost, resname, priority)
// - if there is a "resname" resource in rost's resources, return a pointer
//   on this resource
// - if not, add the resource, set the name, and return a pointer on this
//   new resource
static res *get_or_add_resource(roster *rost, const char *resname, gchar prio)
{
  GSList *p;
  res *nres;

  if (!resname) return NULL;

  for (p = rost->resource; p; p = g_slist_next(p)) {
    res *r = p->data;
    if (!strcmp(r->name, resname))
      return r;
  }

  // Resource not found
  nres = g_new0(res, 1);
  nres->name = g_strdup(resname);
  nres->prio = prio;
  rost->resource = g_slist_insert_sorted(rost->resource, nres,
                                         (GCompareFunc)&resource_compare_prio);
  return nres;
}

static void del_resource(roster *rost, const char *resname)
{
  GSList *p;
  GSList *p_res_elt = NULL;
  res *p_res;

  if (!resname) return;

  for (p = rost->resource; p; p = g_slist_next(p)) {
    res *r = p->data;
    if (!strcmp(r->name, resname))
      p_res_elt = p;
  }

  if (!p_res_elt) return;   // Resource not found

  p_res = p_res_elt->data;
  // Free allocations and delete resource node
  if (p_res->name)        g_free(p_res->name);
  if (p_res->status_msg)  g_free(p_res->status_msg);
  if (p_res->realjid)     g_free(p_res->realjid);
  rost->resource = g_slist_delete_link(rost->resource, p_res_elt);
  return;
}


/* ### Roster functions ### */

// Comparison function used to search in the roster (compares jids and types)
static gint roster_compare_jid_type(roster *a, roster *b) {
  if (! (a->type & b->type))
    return -1; // arbitrary (but should be != 0, of course)
  return strcasecmp(a->jid, b->jid);
}

// Comparison function used to search in the roster (compares names and types)
static gint roster_compare_name_type(roster *a, roster *b) {
  if (! (a->type & b->type))
    return -1; // arbitrary (but should be != 0, of course)
  return strcasecmp(a->name, b->name);
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
    roster_type = ROSTER_TYPE_USER  | ROSTER_TYPE_ROOM |
                  ROSTER_TYPE_AGENT | ROSTER_TYPE_GROUP;

  sample.type = roster_type;
  if (type == jidsearch) {
    sample.jid = (gchar*)jidname;
    comp = (GCompareFunc)&roster_compare_jid_type;
  } else if (type == namesearch) {
    sample.name = (gchar*)jidname;
    comp = (GCompareFunc)&roster_compare_name_type;
  } else
    return NULL;    // should not happen

  while (sl_roster_elt) {
    roster *roster_elt = (roster*)sl_roster_elt->data;
    if (roster_type & ROSTER_TYPE_GROUP) {
      if ((type == namesearch) && !strcasecmp(jidname, roster_elt->name))
        return sl_roster_elt;
    }
    res = g_slist_find_custom(roster_elt->list, &sample, comp);
    if (res) return res;
    sl_roster_elt = g_slist_next(sl_roster_elt);
  }
  return NULL;
}

// Returns pointer to new group, or existing group with that name
GSList *roster_add_group(const char *name)
{
  roster *roster_grp;
  GSList *p_group;

  // #1 Check name doesn't already exist
  p_group = roster_find(name, namesearch, ROSTER_TYPE_GROUP);
  if (!p_group) {
    // #2 Create the group node
    roster_grp = g_new0(roster, 1);
    roster_grp->name = g_strdup(name);
    roster_grp->type = ROSTER_TYPE_GROUP;
    // #3 Insert (sorted)
    groups = g_slist_insert_sorted(groups, roster_grp,
            (GCompareFunc)&roster_compare_name);
    p_group = roster_find(name, namesearch, ROSTER_TYPE_GROUP);
  }
  return p_group;

}

// Returns a pointer to the new user, or existing user with that name
GSList *roster_add_user(const char *jid, const char *name, const char *group,
                        guint type, enum subscr esub)
{
  roster *roster_usr;
  roster *my_group;
  GSList *slist;

  if ((type != ROSTER_TYPE_USER) &&
      (type != ROSTER_TYPE_ROOM) &&
      (type != ROSTER_TYPE_AGENT)) {
    // XXX Error message?
    return NULL;
  }

  // Let's be arbitrary: default group has an empty name ("").
  if (!group)  group = "";

  // #1 Check this user doesn't already exist
  slist = roster_find(jid, jidsearch, 0);
  if (slist) {
    char *oldgroupname;
    // That's an update
    roster_usr = slist->data;
    roster_usr->subscription = esub;
    if (name)
      buddy_setname(slist->data, (char*)name);
    // Let's check if the group name has changed
    oldgroupname = ((roster*)((GSList*)roster_usr->list)->data)->name;
    if (group && strcmp(oldgroupname, group))
      buddy_setgroup(slist->data, (char*)group);
    return slist;
  }
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
  if (unread_jid_del(jid)) {
    roster_usr->flags |= ROSTER_FLAG_MSG;
    // Append the roster_usr to unread_list
    unread_list = g_slist_append(unread_list, roster_usr);
  }
  roster_usr->type = type;
  roster_usr->subscription = esub;
  roster_usr->list = slist;    // (my_group SList element)
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

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM);
  if (sl_user == NULL)
    return;
  roster_usr = (roster*)sl_user->data;

  // Remove (if present) from unread messages list
  node = g_slist_find(unread_list, roster_usr);
  if (node) unread_list = g_slist_delete_link(unread_list, node);
  // If there is a pending unread message, keep track of it
  if (roster_usr->flags & ROSTER_FLAG_MSG)
    unread_jid_add(roster_usr->jid);

  // Let's free memory (jid, name, status message)
  if (roster_usr->jid)        g_free((gchar*)roster_usr->jid);
  if (roster_usr->name)       g_free((gchar*)roster_usr->name);
  if (roster_usr->nickname)   g_free((gchar*)roster_usr->nickname);
  if (roster_usr->topic)      g_free((gchar*)roster_usr->topic);
  free_all_resources(&roster_usr->resource);
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
      // If there is a pending unread message, keep track of it
      if (roster_usr->flags & ROSTER_FLAG_MSG)
        unread_jid_add(roster_usr->jid);
      // Free name and jid
      if (roster_usr->jid)        g_free((gchar*)roster_usr->jid);
      if (roster_usr->name)       g_free((gchar*)roster_usr->name);
      if (roster_usr->nickname)   g_free((gchar*)roster_usr->nickname);
      if (roster_usr->topic)      g_free((gchar*)roster_usr->topic);
      free_all_resources(&roster_usr->resource);
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

//  roster_setstatus()
// Note: resname, role, affil and realjid are for room members only
void roster_setstatus(const char *jid, const char *resname, gchar prio,
                      enum imstatus bstat, const char *status_msg,
                      time_t status_time,
                      enum imrole role, enum imaffiliation affil,
                      const char *realjid)
{
  GSList *sl_user;
  roster *roster_usr;
  res *p_res;

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
  // If we can't find it, we add it
  if (sl_user == NULL)
    sl_user = roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER, sub_none);

  // If there is no resource name, we can leave now
  if (!resname) return;

  roster_usr = (roster*)sl_user->data;

  // If bstat is offline, we MUST delete the resource, actually
  if (bstat == offline) {
    del_resource(roster_usr, resname);
    return;
  }

  // New or updated resource
  p_res = get_or_add_resource(roster_usr, resname, prio);
  p_res->prio = prio;
  p_res->status = bstat;
  if (p_res->status_msg) {
    g_free((gchar*)p_res->status_msg);
    p_res->status_msg = NULL;
  }
  if (status_msg)
    p_res->status_msg = g_strdup(status_msg);
  if (!status_time)
    time(&status_time);
  p_res->status_timestamp = status_time;

  p_res->role = role;
  p_res->affil = affil;

  if (p_res->realjid) {
    g_free((gchar*)p_res->realjid);
    p_res->realjid = NULL;
  }
  if (realjid)
    p_res->realjid = g_strdup(realjid);
}

//  roster_setflags()
// Set one or several flags to value (TRUE/FALSE)
void roster_setflags(const char *jid, guint flags, guint value)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
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

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
  // If we can't find it, we add it
  if (sl_user == NULL) {
    sl_user = roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER, sub_none);
  }

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

const char *roster_getname(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return NULL; // Not in the roster...

  roster_usr = (roster*)sl_user->data;
  return roster_usr->name;
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

enum imstatus roster_getstatus(const char *jid, const char *resname)
{
  GSList *sl_user;
  roster *roster_usr;
  res *p_res;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return offline; // Not in the roster, anyway...

  roster_usr = (roster*)sl_user->data;
  p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->status;
  return offline;
}

const char *roster_getstatusmsg(const char *jid, const char *resname)
{
  GSList *sl_user;
  roster *roster_usr;
  res *p_res;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return NULL; // Not in the roster, anyway...

  roster_usr = (roster*)sl_user->data;
  p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->status_msg;
  return NULL;
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

//  roster_unsubscribed()
// We have lost buddy's presence updates; this function clears the status
// message, sets the buddy offline and frees the resources
void roster_unsubscribed(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return;

  roster_usr = (roster*)sl_user->data;
  free_all_resources(&roster_usr->resource);
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
          (buddy_getstatus((gpointer)roster_usrelt, NULL) != offline) ||
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
void buddy_setgroup(gpointer rosterdata, char *newgroupname)
{
  roster *roster_usr = rosterdata;
  GSList **sl_group;
  GSList *sl_newgroup;
  roster *my_newgroup;

  // A group has no group :)
  if (roster_usr->type & ROSTER_TYPE_GROUP) return;

  // Add newgroup if necessary
  if (!newgroupname)  newgroupname = "";
  sl_newgroup = roster_add_group(newgroupname);
  if (!sl_newgroup) return;
  my_newgroup = (roster*)sl_newgroup->data;

  // Remove the buddy from current group
  sl_group = &((roster*)((GSList*)roster_usr->list)->data)->list;
  *sl_group = g_slist_remove(*sl_group, rosterdata);

  // Remove old group if it is empty
  if (!*sl_group) {
    roster *roster_grp = (roster*)((GSList*)roster_usr->list)->data;
    if (roster_grp->jid)  g_free((gchar*)roster_grp->jid);
    if (roster_grp->name) g_free((gchar*)roster_grp->name);
    g_free(roster_grp);
    groups = g_slist_remove(groups, roster_grp);
  }

  // Add the buddy to its new group
  roster_usr->list = sl_newgroup;    // (my_newgroup SList element)
  my_newgroup->list = g_slist_insert_sorted(my_newgroup->list, roster_usr,
                                            (GCompareFunc)&roster_compare_name);

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

//  buddy_setnickname(buddy, newnickname)
// Only for chatrooms
void buddy_setnickname(gpointer rosterdata, const char *newname)
{
  roster *roster_usr = rosterdata;

  if (!(roster_usr->type & ROSTER_TYPE_ROOM)) return; // XXX Error message?

  if (roster_usr->nickname) {
    g_free((gchar*)roster_usr->nickname);
    roster_usr->nickname = NULL;
  }
  if (newname)
    roster_usr->nickname = g_strdup(newname);
}

const char *buddy_getnickname(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->nickname;
}

//  buddy_setinsideroom(buddy, inside)
// Only for chatrooms
void buddy_setinsideroom(gpointer rosterdata, guint8 inside)
{
  roster *roster_usr = rosterdata;

  if (!(roster_usr->type & ROSTER_TYPE_ROOM)) return;

  roster_usr->inside_room = inside;
}

guint8 buddy_getinsideroom(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->inside_room;
}

//  buddy_settopic(buddy, newtopic)
// Only for chatrooms
void buddy_settopic(gpointer rosterdata, const char *newtopic)
{
  roster *roster_usr = rosterdata;

  if (!(roster_usr->type & ROSTER_TYPE_ROOM)) return;

  if (roster_usr->topic) {
    g_free((gchar*)roster_usr->topic);
    roster_usr->topic = NULL;
  }
  if (newtopic)
    roster_usr->topic = g_strdup(newtopic);
}

const char *buddy_gettopic(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->topic;
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

void buddy_settype(gpointer rosterdata, guint type)
{
  roster *roster_usr = rosterdata;
  roster_usr->type = type;
}

guint buddy_gettype(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->type;
}

guint buddy_getsubscription(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->subscription;
}

enum imstatus buddy_getstatus(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->status;
  return offline;
}

const char *buddy_getstatusmsg(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->status_msg;
  return NULL;
}

time_t buddy_getstatustime(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->status_timestamp;
  return 0;
}

gchar buddy_getresourceprio(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->prio;
  return 0;
}

enum imrole buddy_getrole(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->role;
  return role_none;
}

enum imaffiliation buddy_getaffil(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->affil;
  return affil_none;
}

const char *buddy_getrjid(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->realjid;
  return NULL;
}

//  buddy_getresources(roster_data)
// Return a singly-linked-list of resource names
// Note: the caller should free the list (and data) after use
// If roster_data is null, the current buddy is selected
GSList *buddy_getresources(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  GSList *reslist = NULL, *lp;

  if (!roster_usr) {
    if (!current_buddy) return NULL;
    roster_usr = BUDDATA(current_buddy);
  }
  for (lp = roster_usr->resource; lp; lp = g_slist_next(lp))
    reslist = g_slist_append(reslist, g_strdup(((res*)lp->data)->name));

  return reslist;
}

/*
//  buddy_isresource(roster_data)
// Return true if there is at least one resource
// (which means, for a room, that it isn't empty)
int buddy_isresource(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  if (!roster_usr)
    return FALSE;
  if (roster_usr->resource)
    return TRUE;
  return FALSE;
}
*/

//  buddy_resource_setname(roster_data, oldname, newname)
// Useful for nickname change in a MUC room
void buddy_resource_setname(gpointer rosterdata, const char *resname,
                            const char *newname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res) {
    if (p_res->name) {
      g_free((gchar*)p_res->name);
      p_res->name = NULL;
    }
    if (newname)
      p_res->name = g_strdup(newname);
  }
}

//  buddy_del_all_resources()
// Remove all resources from the specified buddy
void buddy_del_all_resources(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;

  while (roster_usr->resource) {
    res *r = roster_usr->resource->data;
    del_resource(roster_usr, r->name);
  }
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

//  buddy_search_jid(jid)
// Look for a buddy with specified jid.
// Search begins at buddylist; if no match is found in the the buddylist,
// return NULL;
GList *buddy_search_jid(char *jid)
{
  GList *buddy;
  roster *roster_usr;

  if (!buddylist) return NULL;

  for (buddy = buddylist; buddy; buddy = g_list_next(buddy)) {
    roster_usr = (roster*)buddy->data;
    if (roster_usr->jid && !strcasecmp(roster_usr->jid, jid))
      return buddy;
  }
  return NULL;
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
    gchar *jid_locale, *name_locale;
    char *found = NULL;

    buddy = g_list_next(buddy);
    if (!buddy)
      buddy = buddylist;

    roster_usr = (roster*)buddy->data;

    jid_locale = from_utf8(roster_usr->jid);
    if (jid_locale) {
      found = strcasestr(jid_locale, string);
      g_free(jid_locale);
      if (found)
        return buddy;
    }
    name_locale = from_utf8(roster_usr->name);
    if (name_locale) {
      found = strcasestr(name_locale, string);
      g_free(name_locale);
      if (found)
        return buddy;
    }

    if (buddy == current_buddy)
      return NULL; // Back to the beginning, and no match found
  }
}

//  foreach_buddy(roster_type, pfunction, param)
// Call pfunction(buddy, param) for each buddy from the roster with
// type matching roster_type.
void foreach_buddy(guint roster_type,
                   void (*pfunc)(gpointer rosterdata, void *param),
                   void *param)
{
  GSList *sl_roster_elt = groups;
  roster *roster_elt;
  GSList *sl_roster_usrelt;
  roster *roster_usrelt;

  while (sl_roster_elt) {       // group list loop
    roster_elt = (roster*) sl_roster_elt->data;
    sl_roster_usrelt = roster_elt->list;
    while (sl_roster_usrelt) {  // user list loop
      roster_usrelt = (roster*) sl_roster_usrelt->data;

      if (roster_usrelt->type & roster_type)
        pfunc(roster_usrelt, param);

      sl_roster_usrelt = g_slist_next(sl_roster_usrelt);
    }
    sl_roster_elt = g_slist_next(sl_roster_elt);
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
          list = g_slist_append(list, from_utf8(bname));
      }
    } else { // ROSTER_TYPE_USER (jid) (or agent, or chatroom...)
        const char *bjid = buddy_getjid(BUDDATA(buddy));
        if (bjid)
          list = g_slist_append(list, from_utf8(bjid));
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

  if (!unread_list)
    return NULL;

  // First unread message
  if (!rosterdata)
    return unread_list->data;

  unread = g_slist_find(unread_list, rosterdata);
  if (!unread)
    return unread_list->data;

  next_unread = g_slist_next(unread);
  if (next_unread)
    return next_unread->data;
  return unread_list->data;
}


/* ### "unread_jids" functions ###
 *
 * The unread_jids hash table is used to keep track of the buddies with
 * unread messages when a disconnection occurs.
 * When removing a buddy with an unread message from the roster, the
 * jid should be added to the unread_jids table.  When adding a buddy to
 * the roster, we check if (s)he had a pending unread message.
 */

//  unread_jid_add(jid)
// Add jid to the unread_jids hash table
void unread_jid_add(const char *jid)
{
  if (!unread_jids) {
    // Initialize unread_jids hash table
    unread_jids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  }
  // The 2nd unread_jids is an arbitrary non-null pointer:
  g_hash_table_insert(unread_jids, g_strdup(jid), unread_jids);
}

//  unread_jid_del(jid)
// Return TRUE if jid is found in the table (and remove it), FALSE if not
int unread_jid_del(const char *jid)
{
  if (!unread_jids)
    return FALSE;
  return g_hash_table_remove(unread_jids, jid);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
