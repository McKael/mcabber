/*
 * settings.c   -- Configuration stuff
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

#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

#include "settings.h"

static GSList *option;
static GSList *alias;
static GSList *binding;


typedef struct {
  gchar *name;
  gchar *value;
} T_setting;

inline GSList **get_list_ptr(guint type)
{
  if      (type == SETTINGS_TYPE_OPTION)  return &option;
  else if (type == SETTINGS_TYPE_ALIAS)   return &alias;
  else if (type == SETTINGS_TYPE_BINDING) return &binding;
  return NULL;
}

// Return a pointer to the node with the requested key, or NULL if none found
GSList *settings_find(GSList *list, const gchar *key)
{
  GSList *ptr;
  
  if (!list) return NULL;

  for (ptr = list ; ptr; ptr = g_slist_next(ptr))
    if (!strcasecmp(key, ((T_setting*)ptr->data)->name))
      break;

  return ptr;
}

/* -- */

//  parse_assigment(assignment, pkey, pval)
// Read assignment and split it to key, value
//
// If this is an assignment, the function will return TRUE and
// set *pkey and *pval (*pval is set to NULL if value field is empty).
//
// If this isn't a assignment (no = char), the function will set *pval
// to NULL and return FALSE.
//
// The called should g_free() *pkey and *pval (if not NULL) after use.
guint parse_assigment(gchar *assignment, const gchar **pkey, const gchar **pval)
{
  char *key, *val, *t, *p;

  *pkey = *pval = NULL;

  key = assignment;
  // Remove leading spaces in option name
  while ((!isalnum(*key)) && (*key != '=') && *key) {
    //if (!isblank(*key))
    //  scr_LogPrint("Error in assignment parsing!");
    key++;
  }
  if (!*key) return FALSE; // Empty assignment

  if (*key == '=') {
    //scr_LogPrint("Cannot parse assignment!");
    return FALSE;
  }
  // Ok, key points to the option name

  for (val = key+1 ; *val && (*val != '=') ; val++)
    if (!isalnum(*val) && !isblank(*val) && (*val != '_') && (*val != '-')) {
      // Key should only have alnum chars...
      //scr_LogPrint("Error in assignment parsing!");
      return FALSE;
    }
  // Remove trailing spaces in option name:
  for (t = val-1 ; t > key && isblank(*t) ; t--)
    ;
  // Check for embedded whitespace characters
  for (p = key; p < t; p++) {
    if (isblank(*p)) {
      //scr_LogPrint("Error in assignment parsing!"
      //             " (Name should not contain space chars)");
      return FALSE;
    }
  }

  *pkey = g_strndup(key, t+1-key);

  if (!*val) return FALSE; // Not an assignment

  // Remove leading and trailing spaces in option value:
  for (val++; *val && isblank(*val) ; val++) ;
  for (t = val ; *t ; t++) ;
  for (t-- ; t >= val && isblank(*t) ; t--) ;

  if (t < val) return TRUE; // no value (variable reset for example)

  *pval = g_strndup(val, t+1-val);
  return TRUE;
}

void settings_set(guint type, const gchar *key, const gchar *value)
{
  GSList **plist;
  GSList *sptr;
  T_setting *setting;

  plist = get_list_ptr(type);
  if (!plist) return;

  sptr = settings_find(*plist, key);
  if (sptr) {
    // The setting has been found.  We will update it or delete it.
    setting = (T_setting*)sptr->data;
    if (setting->value)
      g_free(setting->value);
    if (!value) {
      // Let's remove the setting
      g_free(setting->name);
      *plist = g_slist_delete_link(*plist, sptr);
    } else {
      // Let's update the setting
      setting->value = g_strdup(value);
    }
  } else if (value) {
    setting = g_new(T_setting, 1);
    setting->name  = g_strdup(key);
    setting->value = g_strdup(value);
    *plist = g_slist_append(*plist, setting);
  }
}

void settings_del(guint type, const gchar *key)
{
  settings_set(type, key, NULL);
}

const gchar *settings_get(guint type, const gchar *key)
{
  GSList **plist;
  GSList *sptr;
  T_setting *setting;

  plist = get_list_ptr(type);
  sptr = settings_find(*plist, key);
  if (!sptr) return NULL;

  setting = (T_setting*)sptr->data;
  return setting->value;
}

int settings_get_int(guint type, const gchar *key)
{
  const gchar *setval = settings_get(type, key);

  if (setval) return atoi(setval);
  return 0;
}

// Return the command the key is bound to, or NULL.
const gchar *isbound(int key)
{
  gchar asciikey[16];
  g_snprintf(asciikey, 15, "%d", key);
  return settings_get(SETTINGS_TYPE_BINDING, asciikey);
}

//  settings_get_status_msg(status)
// Return a string with the current status message:
// - if there is a user-defined message ("message" option),
//   return this message
// - if there is a user-defined message for the given status (and no
//   generic user message), it is returned
// - if no user-defined message is found, return the mcabber default msg
// - if there is no default (offline, invisible), return an empty string
const gchar *settings_get_status_msg(enum imstatus status)
{
  const gchar *rstatus = settings_opt_get("message");

  if (rstatus) return rstatus;

  switch(status) {
    case available:
        if ((rstatus = settings_opt_get("message_avail")) == NULL)
          rstatus = MSG_AVAIL;
        break;

    case freeforchat:
        if ((rstatus = settings_opt_get("message_free")) == NULL)
          rstatus = MSG_FREE;
        break;

    case dontdisturb:
        if ((rstatus = settings_opt_get("message_dnd")) == NULL)
          rstatus = MSG_DND;
        break;

    case notavail:
        if ((rstatus = settings_opt_get("message_notavail")) == NULL)
          rstatus = MSG_NOTAVAIL;
        break;

    case away:
        if ((rstatus = settings_opt_get("message_away")) == NULL)
          rstatus = MSG_AWAY;
        break;

    default:
        rstatus = "";
  }
  return rstatus;
}
