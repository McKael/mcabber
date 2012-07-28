/*
 * events.c     -- Events fonctions
 *
 * Copyright (C) 2006-2010 Mikael Berthe <mikael@lilotux.net>
 * Copyright (C) 2010      Myhailo Danylenko <isbear@ukrpost.net>
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

#include <glib.h>
#include <string.h>
#include "events.h"
#include "screen.h"

typedef struct {
  char           *id;
  char           *description;
  time_t          timeout;
  guint           source;
  evs_callback_t  callback;
  gpointer        data;
  GDestroyNotify  notify;
} evs_t;

static GSList *evs_list; // Events list

static evs_t *evs_find(const char *evid);

static gboolean evs_check_timeout (gpointer userdata)
{
  evs_t *event = userdata;
  if (event->callback &&
      !event->callback(EVS_CONTEXT_TIMEOUT, NULL, event->data)) {
    evs_del(event->id);
    return FALSE;
  }
  return TRUE; // XXX
}

//  evs_new(type, timeout)
// Create new event. If id is omitted, generates unique
// numerical id (recommended). If timeout is specified, sets
// up timeout source, that will call handler in timeout
// context after specified number of seconds. If supplied id
// already exists, returns NULL, calling destroy notifier, if
// one is specified.
const char *evs_new(const char *desc, const char *id, time_t timeout, evs_callback_t callback, gpointer udata, GDestroyNotify notify)
{
  static guint evs_idn;
  evs_t *event;
  char *stridn;

  if (!id) {
    if (!++evs_idn)
      evs_idn = 1;
    /* Check for wrapping, we shouldn't reuse ids */
    stridn = g_strdup_printf("%d", evs_idn);
    if (evs_find(stridn))  {
      g_free(stridn);
      // We could try another id but for now giving up should be fine...
      if (notify)
        notify(udata);
      return NULL;
    }
  } else if (!evs_find(id))
    stridn = g_strdup(id);
  else {
    if (notify)
      notify(udata);
    return NULL;
  }

  event = g_new(evs_t, 1);

  event->id          = stridn;
  event->description = g_strdup(desc);
  event->timeout     = timeout;
  event->callback    = callback;
  event->data        = udata;
  event->notify      = notify;

  if (timeout)
    g_timeout_add_seconds(timeout, evs_check_timeout, event);

  evs_list = g_slist_append(evs_list, event);
  return stridn;
}

static evs_t *evs_find(const char *evid)
{
  GSList *p;

  if (!evid)
    return NULL;

  for (p = evs_list; p; p = g_slist_next(p)) {
    evs_t *i = p->data;
    if (!strcmp(evid, i->id))
      return i;
  }
  return NULL;
}

//  evs_del(evid)
// Deletes event.
// This will not call event handler, however this will
// call destroy notify function.
// Returns 0 in case of success, -1 if the evid hasn't been found.
int evs_del(const char *evid)
{
  evs_t *event = evs_find(evid);

  if (!event)
    return -1;

  if (event->notify)
    event->notify(event->data);
  if (event->source)
    g_source_remove(event->source);

  evs_list = g_slist_remove(evs_list, event);
  g_free(event->id);
  g_free(event->description);
  g_free(event);

  return 0; // Ok, deleted
}

//  evs_callback(evid, evcontext, argument)
// Callback processing for the specified event.
// If event handler will return FALSE, event will be destroyed.
// Return 0 in case of success, -1 if the evid hasn't been found.
// evcontext and argument are transparently passed to event handler.
int evs_callback(const char *evid, guint context, const char *arg)
{
  evs_t *event;

  event = evs_find(evid);
  if (!event)
    return -1;

  if (event->callback &&
      !event->callback(context, arg, event->data))
    evs_del(evid);
  return 0;
}

//  evs_display_list()
// Prints list of events to mcabber log window.
void evs_display_list(void)
{
  guint count = 0;
  GSList *p;

  scr_LogPrint(LPRINT_NORMAL, "Events list:");
  for (p = evs_list; p; p = g_slist_next(p)) {
    evs_t *i = p->data;
    scr_LogPrint(LPRINT_NORMAL,
                 "Id: %-3s %s", i->id,
                 (i->description ? i->description : ""));
    count++;
  }
  scr_LogPrint(LPRINT_NORMAL, "End of events list.");
  if (count+2 > scr_getlogwinheight()) {
    scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
    scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                   ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
  }
}

//  evs_geteventslist()
// Return a singly-linked-list of events ids.
// Data in list should not be modified and can disappear,
// you must strdup them, if you want them to persist.
// Note: the caller should free the list after use.
GSList *evs_geteventslist(void)
{
  GSList *evidlist = NULL, *p;

  for (p = evs_list; p; p = g_slist_next(p)) {
    evs_t *i = p->data;
    evidlist = g_slist_append(evidlist, i->id);
  }

  return evidlist;
}

//  evs_deinit()
// Frees all events.
void evs_deinit(void)
{
  GSList *eel;
  for (eel = evs_list; eel; eel = eel->next) {
    evs_t *event = eel->data;
    if (event->notify)
      event->notify(event->data);
    if (event->source)
      g_source_remove(event->source);

    evs_list = g_slist_remove(evs_list, event);
    g_free(event->id);
    g_free(event->description);
    g_free(event);
  }
  g_slist_free(evs_list);
  evs_list = NULL;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
