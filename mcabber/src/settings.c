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
GSList *settings_find(GSList *list, gchar *key)
{
  GSList *ptr;
  
  if (!list) return NULL;

  for (ptr = list ; ptr; ptr = g_slist_next(ptr))
    if (!strcasecmp(key, ((T_setting*)ptr->data)->name))
      break;

  return ptr;
}

/* -- */

void settings_set(guint type, gchar *key, gchar *value)
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

void settings_del(guint type, gchar *key)
{
  settings_set(type, key, NULL);
}

gchar *settings_get(guint type, gchar *key)
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

int settings_get_int(guint type, gchar *key)
{
  gchar *setval = settings_get(type, key);

  if (setval) return atoi(setval);
  return 0;
}

