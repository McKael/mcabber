/*
 * roster.c     -- Local roster implementation
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "roster.h"
#include "utils.h"
#include "hooks.h"

extern void hlog_save_state(void);

char *strrole[] = {   /* Should match enum in roster.h */
  "none",
  "moderator",
  "participant",
  "visitor"
};

char *straffil[] = {  /* Should match enum in roster.h */
  "none",
  "owner",
  "admin",
  "member",
  "outcast"
};

char *strprintstatus[] = {  /* Should match enum in roster.h */
  "default",
  "none",
  "in_and_out",
  "all"
};

char *strautowhois[] = {    /* Should match enum in roster.h */
  "default",
  "off",
  "on",
};

char *strflagjoins[] = {    /* Should match enum in roster.h */
  "default",
  "none",
  "joins",
  "all"
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
  guint events;
  char *caps;
#ifdef XEP0085
  struct xep0085 xep85;
#endif
#ifdef HAVE_GPGME
  struct pgp_data pgpdata;
#endif
} res;

/* This is a private structure type for the roster */

typedef struct {
  gchar *name;
  gchar *jid;
  guint type;
  enum subscr subscription;
  GSList *resource;
  res *active_resource;

  /* For groupchats */
  gchar *nickname;
  gchar *topic;
  guint inside_room;
  guint print_status;
  guint auto_whois;
  guint flag_joins;

  /* on_server is TRUE if the item is present on the server roster */
  guint on_server;

  /* To keep track of last status message */
  gchar *offline_status_message;

  /* Flag used for the UI */
  guint flags;
  guint ui_prio;  // Boolean, positive if "attention" is requested

  // list: user -> points to his group; group -> points to its users list
  GSList *list;
} roster;


/* ### Variables ### */

static guchar display_filter;
static GSList *groups;
static GSList *unread_list;
static GHashTable *unread_jids;
GList *buddylist;
static gboolean _rebuild_buddylist = FALSE;
GList *current_buddy;
GList *alternate_buddy;
GList *last_activity_buddy;

static roster roster_special;

static int  unread_jid_del(const char *jid);

#define DFILTER_ALL     63
#define DFILTER_ONLINE  62


/* ### Initialization ### */

void roster_init(void)
{
  roster_special.name = SPECIAL_BUFFER_STATUS_ID;
  roster_special.type = ROSTER_TYPE_SPECIAL;
}

/* ### Resources functions ### */

static inline void free_resource_data(res *p_res)
{
  if (!p_res)
    return;
  g_free((gchar*)p_res->status_msg);
  g_free((gchar*)p_res->name);
  g_free((gchar*)p_res->realjid);
#ifdef HAVE_GPGME
  g_free(p_res->pgpdata.sign_keyid);
#endif
  g_free(p_res->caps);
  g_free(p_res);
}

static void free_all_resources(GSList **reslist)
{
  GSList *lip;

  for (lip = *reslist; lip ; lip = g_slist_next(lip))
    free_resource_data((res*)lip->data);
  // Free all nodes but the first (which is static)
  g_slist_free(*reslist);
  *reslist = NULL;
}

// Resources are sorted in ascending order
static gint resource_compare_prio(res *a, res *b) {
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
    if (!strcmp(r->name, resname)) {
      if (prio != r->prio) {
        r->prio = prio;
        rost->resource = g_slist_sort(rost->resource,
                                      (GCompareFunc)&resource_compare_prio);
      }
      return r;
    }
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

  // Keep a copy of the status message when a buddy goes offline
  if (g_slist_length(rost->resource) == 1) {
    g_free(rost->offline_status_message);
    rost->offline_status_message = p_res->status_msg;
    p_res->status_msg = NULL;
  }

  if (rost->active_resource == p_res)
    rost->active_resource = NULL;

  // Free allocations and delete resource node
  free_resource_data(p_res);
  rost->resource = g_slist_delete_link(rost->resource, p_res_elt);
  return;
}


/* ### Roster functions ### */

static inline void free_roster_user_data(roster *roster_usr)
{
  if (!roster_usr)
    return;
  g_free((gchar*)roster_usr->jid);
  //g_free((gchar*)roster_usr->active_resource);
  g_free((gchar*)roster_usr->name);
  g_free((gchar*)roster_usr->nickname);
  g_free((gchar*)roster_usr->topic);
  g_free((gchar*)roster_usr->offline_status_message);
  free_all_resources(&roster_usr->resource);
  g_free(roster_usr);
}

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
  return strcmp(a->name, b->name);
}

// Comparison function used to sort the roster (by name)
static gint roster_compare_name(roster *a, roster *b) {
  return strcmp(a->name, b->name);
}

// Finds a roster element (user, group, agent...), by jid or name
// If roster_type is 0, returns match of any type.
// Returns the roster GSList element, or NULL if jid/name not found
GSList *roster_find(const char *jidname, enum findwhat type, guint roster_type)
{
  GSList *sl_roster_elt = groups;
  GSList *resource;
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
    return NULL;    // Should not happen...

  while (sl_roster_elt) {
    roster *roster_elt = (roster*)sl_roster_elt->data;
    if (roster_type & ROSTER_TYPE_GROUP) {
      if ((type == namesearch) && !strcmp(jidname, roster_elt->name))
        return sl_roster_elt;
    }
    resource = g_slist_find_custom(roster_elt->list, &sample, comp);
    if (resource) return resource;
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

// Comparison function used to sort the unread list by ui (attn) priority
static gint _roster_compare_uiprio(roster *a, roster *b) {
  return (b->ui_prio - a->ui_prio);
}

// Returns a pointer to the new user, or existing user with that name
// Note: if onserver is -1, the flag won't be changed.
GSList *roster_add_user(const char *jid, const char *name, const char *group,
                        guint type, enum subscr esub, gint onserver)
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
    if (onserver >= 0)
      buddy_setonserverflag(slist->data, onserver);
    if (name)
      buddy_setname(slist->data, (char*)name);
    // Let's check if the group name has changed
    oldgroupname = ((roster*)((GSList*)roster_usr->list)->data)->name;
    if (group && strcmp(oldgroupname, group)) {
      buddy_setgroup(slist->data, (char*)group);
      // Note: buddy_setgroup() updates the user lists so we cannot
      // use slist anymore.
      return roster_find(jid, jidsearch, 0);
    }
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
    p = strchr(str, JID_RESOURCE_SEPARATOR);
    if (p)  *p = '\0';
    roster_usr->name = g_strdup(str);
    g_free(str);
  }
  if (unread_jid_del(jid)) {
    roster_usr->flags |= ROSTER_FLAG_MSG;
    // Append the roster_usr to unread_list
    unread_list = g_slist_insert_sorted(unread_list, roster_usr,
                                        (GCompareFunc)&_roster_compare_uiprio);
  }
  roster_usr->type = type;
  roster_usr->subscription = esub;
  roster_usr->list = slist;    // (my_group SList element)
  if (onserver == 1)
    roster_usr->on_server = TRUE;
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

  sl_group = roster_usr->list;

  // Let's free roster_usr memory (jid, name, status message...)
  free_roster_user_data(roster_usr);

  // That's a little complex, we need to dereference twice
  sl_group_listptr = &((roster*)(sl_group->data))->list;
  *sl_group_listptr = g_slist_delete_link(*sl_group_listptr, sl_user);

  // We need to rebuild the list
  if (current_buddy)
    buddylist_defer_build();
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
      // Free roster_usr data (jid, name, status message...)
      free_roster_user_data(roster_usr);
      sl_usr = g_slist_next(sl_usr);
    }
    // Free group's users list
    if (roster_grp->list)
      g_slist_free(roster_grp->list);
    // Free group's name and jid
    g_free((gchar*)roster_grp->jid);
    g_free((gchar*)roster_grp->name);
    g_free(roster_grp);
    sl_grp = g_slist_next(sl_grp);
  }
  // Free groups list
  if (groups) {
    g_slist_free(groups);
    groups = NULL;
    // Update (i.e. free) buddylist
    if (buddylist)
      buddylist_defer_build();
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
    sl_user = roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER,
                              sub_none, -1);

  // If there is no resource name, we can leave now
  if (!resname) return;

  roster_usr = (roster*)sl_user->data;

  // New or updated resource
  p_res = get_or_add_resource(roster_usr, resname, prio);
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

  // If bstat is offline, we MUST delete the resource, actually
  if (bstat == offline) {
    del_resource(roster_usr, resname);
    return;
  }
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

//  roster_unread_check()
static void roster_unread_check(void)
{
  guint unread_count = 0;
  gpointer unread_ptr, first_unread;
  guint muc_unread = 0, muc_attention = 0;
  guint attention_count = 0;

  unread_ptr = first_unread = unread_msg(NULL);
  if (first_unread) {
    do {
      guint type = buddy_gettype(unread_ptr);
      unread_count++;

      if (type & ROSTER_TYPE_ROOM) {
        muc_unread++;
        if (buddy_getuiprio(unread_ptr) >= ROSTER_UI_PRIO_MUC_HL_MESSAGE)
          muc_attention++;
      } else {
        if (buddy_getuiprio(unread_ptr) >= ROSTER_UI_PRIO_ATTENTION_MESSAGE)
          attention_count++;
      }
      unread_ptr = unread_msg(unread_ptr);
    } while (unread_ptr && unread_ptr != first_unread);
  }

  hk_unread_list_change(unread_count, attention_count,
                        muc_unread, muc_attention);
}

//  roster_msg_setflag()
// Set the ROSTER_FLAG_MSG to the given value for the given jid.
// It will update the buddy's group message flag.
// Update the unread messages list too.
void roster_msg_setflag(const char *jid, guint special, guint value)
{
  GSList *sl_user;
  roster *roster_usr, *roster_grp;
  int new_roster_item = FALSE;
  guint unread_list_modified = FALSE;

  if (special) {
    //sl_user = roster_find(jid, namesearch, ROSTER_TYPE_SPECIAL);
    //if (!sl_user) return;
    //roster_usr = (roster*)sl_user->data;
    roster_usr = &roster_special;
    if (value) {
      if (!(roster_usr->flags & ROSTER_FLAG_MSG))
        unread_list_modified = TRUE;
      roster_usr->flags |= ROSTER_FLAG_MSG;
      // Append the roster_usr to unread_list, but avoid duplicates
      if (!g_slist_find(unread_list, roster_usr))
        unread_list = g_slist_insert_sorted(unread_list, roster_usr,
                                        (GCompareFunc)&_roster_compare_uiprio);
    } else {
      if (roster_usr->flags & ROSTER_FLAG_MSG)
        unread_list_modified = TRUE;
      roster_usr->flags &= ~ROSTER_FLAG_MSG;
      roster_usr->ui_prio = 0;
      if (unread_list) {
        GSList *node = g_slist_find(unread_list, roster_usr);
        if (node)
          unread_list = g_slist_delete_link(unread_list, node);
      }
    }
    goto roster_msg_setflag_return;
  }

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
  // If we can't find it, we add it
  if (sl_user == NULL) {
    sl_user = roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER, sub_none, -1);
    new_roster_item = TRUE;
  }

  roster_usr = (roster*)sl_user->data;
  roster_grp = (roster*)roster_usr->list->data;
  if (value) {
    if (!(roster_usr->flags & ROSTER_FLAG_MSG))
      unread_list_modified = TRUE;
    // Message flag is TRUE.  This is easy, we just have to set both flags
    // to TRUE...
    roster_usr->flags |= ROSTER_FLAG_MSG;
    roster_grp->flags |= ROSTER_FLAG_MSG; // group
    // Append the roster_usr to unread_list, but avoid duplicates
    if (!g_slist_find(unread_list, roster_usr))
      unread_list = g_slist_insert_sorted(unread_list, roster_usr,
                                      (GCompareFunc)&_roster_compare_uiprio);
  } else {
    // Message flag is FALSE.
    guint msg = FALSE;
    if (roster_usr->flags & ROSTER_FLAG_MSG)
      unread_list_modified = TRUE;
    roster_usr->flags &= ~ROSTER_FLAG_MSG;
    roster_usr->ui_prio = 0;
    if (unread_list) {
      GSList *node = g_slist_find(unread_list, roster_usr);
      if (node)
        unread_list = g_slist_delete_link(unread_list, node);
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

  if (buddylist && (new_roster_item || !g_list_find(buddylist, roster_usr)))
    buddylist_defer_build();

roster_msg_setflag_return:
  if (unread_list_modified) {
    hlog_save_state();
    roster_unread_check();
  }
}

//  roster_setuiprio(jid, special, prio_value, action)
// Set the "attention" priority value for the given roster item.
// Note that this function doesn't create the roster item if it doesn't exist.
void roster_setuiprio(const char *jid, guint special, guint value,
                      enum setuiprio_ops action)
{
  guint oldval, newval;
  roster *roster_usr;

  if (special) {
    roster_usr = &roster_special;
  } else {
    GSList *sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
    if (!sl_user)
      return;

    roster_usr = (roster*)sl_user->data;
  }
  oldval = roster_usr->ui_prio;

  if (action == prio_max)
    newval = MAX(oldval, value);
  else if (action == prio_inc)
    newval = oldval + value;
  else // prio_set
    newval = value;

  roster_usr->ui_prio = newval;
  unread_list = g_slist_sort(unread_list,
                             (GCompareFunc)&_roster_compare_uiprio);
  roster_unread_check();
}

guint roster_getuiprio(const char *jid, guint special)
{
  roster *roster_usr;
  GSList *sl_user;

  if (special) {
    roster_usr = &roster_special;
    return roster_usr->ui_prio;
  }

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
  if (!sl_user)
    return 0;
  roster_usr = (roster*)sl_user->data;
  return roster_usr->ui_prio;
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

const char *roster_getnickname(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  sl_user = roster_find(jid, jidsearch,
                        ROSTER_TYPE_USER|ROSTER_TYPE_ROOM|ROSTER_TYPE_AGENT);
  if (sl_user == NULL)
    return NULL; // Not in the roster...

  roster_usr = (roster*)sl_user->data;
  return roster_usr->nickname;
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
  return roster_usr->offline_status_message;
}

char roster_getprio(const char *jid, const char *resname)
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
    return p_res->prio;
  return 0;
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

guint roster_getsubscription(const char *jid)
{
  GSList *sl_user;
  roster *roster_usr;

  if ((sl_user = roster_find(jid, jidsearch, 0)) == NULL)
    return 0;

  roster_usr = (roster*)sl_user->data;
  return roster_usr->subscription;
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
  if (hide < 0) {               // NEG   (invert)
    if (display_filter == DFILTER_ALL)
      display_filter = DFILTER_ONLINE;
    else
      display_filter = DFILTER_ALL;
  } else if (hide == 0) {       // FALSE (don't hide -- andfo_)
    display_filter = DFILTER_ALL;
  } else {                      // TRUE  (hide -- andfo)
    display_filter = DFILTER_ONLINE;
  }
}

int buddylist_isset_filter(void)
{
  return (display_filter != DFILTER_ALL);
}

int buddylist_is_status_filtered(enum imstatus status)
{
  return display_filter & (1 << status);
}

void buddylist_set_filter(guchar filter)
{
  display_filter = filter;
}

guchar buddylist_get_filter(void)
{
  return display_filter;
}

void buddylist_defer_build(void)
{
  _rebuild_buddylist = TRUE;
}

//  buddylist_build()
// Creates the buddylist from the roster entries.
void buddylist_build(void)
{
  GSList *sl_roster_elt = groups;
  roster *roster_elt;
  roster *roster_current_buddy = NULL;
  roster *roster_alternate_buddy = NULL;
  roster *roster_last_activity_buddy = NULL;
  int shrunk_group;

  if (_rebuild_buddylist == FALSE)
    return;
  _rebuild_buddylist = FALSE;

  // We need to remember which buddy is selected.
  if (current_buddy)
    roster_current_buddy = BUDDATA(current_buddy);
  current_buddy = NULL;
  if (alternate_buddy)
    roster_alternate_buddy = BUDDATA(alternate_buddy);
  alternate_buddy = NULL;
  if (last_activity_buddy)
    roster_last_activity_buddy = BUDDATA(last_activity_buddy);
  last_activity_buddy = NULL;

  // Destroy old buddylist
  if (buddylist) {
    g_list_free(buddylist);
    buddylist = NULL;
  }

  buddylist = g_list_append(buddylist, &roster_special);

  // Create the new list
  while (sl_roster_elt) {
    GSList *sl_roster_usrelt;
    roster *roster_usrelt;
    guint pending_group = TRUE;
    roster_elt = (roster*) sl_roster_elt->data;

    shrunk_group = roster_elt->flags & ROSTER_FLAG_HIDE;

    sl_roster_usrelt = roster_elt->list;
    while (sl_roster_usrelt) {
      roster_usrelt = (roster*) sl_roster_usrelt->data;

      // Buddy will be added if either:
      // - buddy's status matches the display_filter
      // - buddy has a lock (for example the buddy window is currently open)
      // - buddy has a pending (non-read) message
      // - group isn't hidden (shrunk)
      // - this is the current_buddy
      if (roster_usrelt == roster_current_buddy ||
          buddylist_is_status_filtered(buddy_getstatus((gpointer)roster_usrelt,
                                                       NULL)) ||
          (buddy_getflags((gpointer)roster_usrelt) &
               (ROSTER_FLAG_LOCK | ROSTER_FLAG_USRLOCK | ROSTER_FLAG_MSG))) {
        // This user should be added.  Maybe the group hasn't been added yet?
        if (pending_group) {
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
  if (roster_last_activity_buddy)
    last_activity_buddy = g_list_find(buddylist, roster_last_activity_buddy);
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
  if (!rosterdata)
    return NULL;
  return roster_usr->jid;
}

//  buddy_setgroup()
// Change the group of current buddy
//
// Note: buddy_setgroup() updates the user lists.
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
    g_free((gchar*)roster_grp->jid);
    g_free((gchar*)roster_grp->name);
    g_free(roster_grp);
    groups = g_slist_remove(groups, roster_grp);
  }

  // Add the buddy to its new group
  roster_usr->list = sl_newgroup;    // (my_newgroup SList element)
  my_newgroup->list = g_slist_insert_sorted(my_newgroup->list, roster_usr,
                                            (GCompareFunc)&roster_compare_name);

  buddylist_defer_build();
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

  buddylist_defer_build();
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
void buddy_setinsideroom(gpointer rosterdata, guint inside)
{
  roster *roster_usr = rosterdata;

  if (!(roster_usr->type & ROSTER_TYPE_ROOM)) return;

  roster_usr->inside_room = inside;
}

guint buddy_getinsideroom(gpointer rosterdata)
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

void buddy_setprintstatus(gpointer rosterdata, enum room_printstatus pstatus)
{
  roster *roster_usr = rosterdata;
  roster_usr->print_status = pstatus;
}

enum room_printstatus buddy_getprintstatus(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->print_status;
}

void buddy_setautowhois(gpointer rosterdata, enum room_autowhois awhois)
{
  roster *roster_usr = rosterdata;
  roster_usr->auto_whois = awhois;
}

enum room_autowhois buddy_getautowhois(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->auto_whois;
}

void buddy_setflagjoins(gpointer rosterdata, enum room_flagjoins fjoins)
{
  roster *roster_usr = rosterdata;
  roster_usr->flag_joins = fjoins;
}

enum room_flagjoins buddy_getflagjoins(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->flag_joins;
}

//  buddy_getgroupname()
// Returns a pointer on buddy's group name.
const char *buddy_getgroupname(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;

  if (roster_usr->type & ROSTER_TYPE_GROUP)
    return roster_usr->name;

  if (roster_usr->type & ROSTER_TYPE_SPECIAL)
    return NULL;

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

  if (roster_usr->type & ROSTER_TYPE_SPECIAL)
    return NULL;

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
  return roster_usr->offline_status_message;
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

guint buddy_resource_getevents(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->events;
  return ROSTER_EVENT_NONE;
}

void buddy_resource_setevents(gpointer rosterdata, const char *resname,
                              guint events)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    p_res->events = events;
}

char *buddy_resource_getcaps(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return p_res->caps;
  return NULL;
}

void buddy_resource_setcaps(gpointer rosterdata, const char *resname,
                            const char *caps)
{
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res) {
    g_free(p_res->caps);
    p_res->caps = g_strdup(caps);
  }
}

struct xep0085 *buddy_resource_xep85(gpointer rosterdata, const char *resname)
{
#ifdef XEP0085
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return &p_res->xep85;
#endif
  return NULL;
}

struct pgp_data *buddy_resource_pgp(gpointer rosterdata, const char *resname)
{
#ifdef HAVE_GPGME
  roster *roster_usr = rosterdata;
  res *p_res = get_resource(roster_usr, resname);
  if (p_res)
    return &p_res->pgpdata;
#endif
  return NULL;
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

//  buddy_getresources_locale(roster_data)
// Same as buddy_getresources() but names are converted to user's locale
// Note: the caller should free the list (and data) after use
GSList *buddy_getresources_locale(gpointer rosterdata)
{
  GSList *reslist, *lp;

  reslist = buddy_getresources(rosterdata);
  // Convert each item to UI's locale
  for (lp = reslist; lp; lp = g_slist_next(lp)) {
    gchar *oldname = lp->data;
    lp->data = from_utf8(oldname);
    if (lp->data)
      g_free(oldname);
    else
      lp->data = oldname;
  }
  return reslist;
}

//  buddy_getactiveresource(roster_data)
// Returns name of active (selected for chat) resource
const char *buddy_getactiveresource(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  res *resource;

  if (!roster_usr) {
    if (!current_buddy) return NULL;
    roster_usr = BUDDATA(current_buddy);
  }

  resource = roster_usr->active_resource;
  if (!resource) return NULL;
  return resource->name;
}

void buddy_setactiveresource(gpointer rosterdata, const char *resname)
{
  roster *roster_usr = rosterdata;
  res *p_res = NULL;
  if (resname)
    p_res = get_resource(roster_usr, resname);
  roster_usr->active_resource = p_res;
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

guint buddy_getuiprio(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->ui_prio;
}

//  buddy_setonserverflag()
// Set the on_server flag
void buddy_setonserverflag(gpointer rosterdata, guint onserver)
{
  roster *roster_usr = rosterdata;
  roster_usr->on_server = onserver;
}

guint buddy_getonserverflag(gpointer rosterdata)
{
  roster *roster_usr = rosterdata;
  return roster_usr->on_server;
}

//  buddy_search_jid(jid)
// Look for a buddy with specified jid.
// Search begins at buddylist; if no match is found in the the buddylist,
// return NULL;
GList *buddy_search_jid(const char *jid)
{
  GList *buddy;
  roster *roster_usr;

  buddylist_build();
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
  buddylist_build();
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
    if (roster_elt->type & ROSTER_TYPE_SPECIAL)
      continue; // Skip special items
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

//  foreach_group_member(group, pfunction, param)
// Call pfunction(buddy, param) for each buddy in the specified group.
void foreach_group_member(gpointer groupdata,
                   void (*pfunc)(gpointer rosterdata, void *param),
                   void *param)
{
  roster *roster_elt;
  GSList *sl_roster_usrelt;
  roster *roster_usrelt;

  roster_elt = groupdata;

  if (!(roster_elt->type & ROSTER_TYPE_GROUP))
    return;

  sl_roster_usrelt = roster_elt->list;
  while (sl_roster_usrelt) {  // user list loop
    GSList *next_sl_usrelt;
    roster_usrelt = (roster*) sl_roster_usrelt->data;

    next_sl_usrelt = g_slist_next(sl_roster_usrelt);
    pfunc(roster_usrelt, param);
    sl_roster_usrelt = next_sl_usrelt;
  }
}

//  compl_list(type)
// Returns a list of jid's or groups.  (For commands completion)
// type: ROSTER_TYPE_USER (jid's) or ROSTER_TYPE_GROUP (group names)
// The list should be freed by the caller after use.
GSList *compl_list(guint type)
{
  GSList *list = NULL;
  GSList *sl_roster_elt = groups;
  roster *roster_elt;
  GSList *sl_roster_usrelt;
  roster *roster_usrelt;

  while (sl_roster_elt) {       // group list loop
    roster_elt = (roster*) sl_roster_elt->data;

    if (roster_elt->type & ROSTER_TYPE_SPECIAL)
      continue; // Skip special items

    if (type == ROSTER_TYPE_GROUP) { // (group names)
      if (roster_elt->name && *(roster_elt->name))
        list = g_slist_append(list, from_utf8(roster_elt->name));
    } else { // ROSTER_TYPE_USER (jid) (or agent, or chatroom...)
      sl_roster_usrelt = roster_elt->list;
      while (sl_roster_usrelt) {  // user list loop
        roster_usrelt = (roster*) sl_roster_usrelt->data;

        if (roster_usrelt->jid)
          list = g_slist_append(list, from_utf8(roster_usrelt->jid));

        sl_roster_usrelt = g_slist_next(sl_roster_usrelt);
      }
    }
    sl_roster_elt = g_slist_next(sl_roster_elt);
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
static int unread_jid_del(const char *jid)
{
  if (!unread_jids)
    return FALSE;
  return g_hash_table_remove(unread_jids, jid);
}

// Helper function for unread_jid_get_list()
static void add_to_unreadjids(gpointer key, gpointer value, gpointer udata)
{
  GList **listp = udata;
  *listp = g_list_append(*listp, key);
}

//  unread_jid_get_list()
// Return the JID list.
// The content of the list should not be modified or freed.
// The caller should call g_list_free() after use.
GList *unread_jid_get_list(void)
{
  GList *list = NULL;

  if (!unread_jids)
    return NULL;

  // g_hash_table_get_keys() is only in glib >= 2.14
  //return g_hash_table_get_keys(unread_jids);

  g_hash_table_foreach(unread_jids, add_to_unreadjids, &list);
  return list;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
