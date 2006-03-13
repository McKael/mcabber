/*
 * events.c     -- Events fonctions
 *
 * Copyright (C) 2006 Mikael Berthe <bmikael@lists.lilotux.net>
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
#include "events.h"
#include "logprint.h"

static GSList *evs_list; // Events list


//  evs_new(type, timeout)
// Create an events structure.
eviqs *evs_new(guint8 type, time_t timeout)
{
  static guint evs_idn;
  eviqs *new_evs;
  time_t now_t;

  if (!++evs_idn)
    evs_idn = 1;
  /* TODO: check for wrapping, we shouldn't reuse ids */

  new_evs = g_new0(eviqs, 1);
  time(&now_t);
  new_evs->ts_create = now_t;
  if (timeout)
    new_evs->ts_expire = now_t + timeout;
  new_evs->type = type;
  new_evs->id = g_strdup_printf("%d", evs_idn);

  evs_list = g_slist_append(evs_list, new_evs);
  return new_evs;
}

int evs_del(const char *evid)
{
  GSList *p;
  eviqs *i;

  if (!evid) return 1;

  for (p = evs_list; p; p = g_slist_next(p)) {
    i = p->data;
    if (!strcmp(evid, i->id))
      break;
  }
  if (p) {
    g_free(i->id);
    if (i->xmldata) xmlnode_free(i->xmldata);
    if (i->data)    g_free(i->data);
    g_free(i);
    evs_list = g_slist_remove(evs_list, p->data);
    return 0; // Ok, deleted
  }
  return -1;  // Not found
}

static eviqs *evs_find(const char *evid)
{
  GSList *p;
  eviqs *i;

  if (!evid) return NULL;

  for (p = evs_list; p; p = g_slist_next(p)) {
    i = p->data;
    if (!strcmp(evid, i->id))
      return i;
  }
  return NULL;
}

//  evs_callback(evid, evcontext)
// Callback processing for the specified event.
// Return 0 in case of success, -1 if the evid hasn't been found.
int evs_callback(const char *evid, guint evcontext)
{
  eviqs *i;

  i = evs_find(evid);
  if (!i) return -1;

  // IQ processing
  // Note: If xml_result is NULL, this is a timeout
  if (i->callback)
    (*i->callback)(i, evcontext);

  evs_del(evid);
  return 0;
}

void evs_check_timeout(time_t now_t)
{
  GSList *p;
  eviqs *i;

  p = evs_list;
  while (p) {
    i = p->data;
    // We must get next IQ eviqs element now because the current one
    // could be freed.
    p = g_slist_next(p);

    if ((!i->ts_expire && now_t > i->ts_create + EVS_MAX_TIMEOUT) ||
        (i->ts_expire && now_t > i->ts_expire)) {
      evs_callback(i->id, EVS_CONTEXT_TIMEOUT);
    }
  }
}

void evs_display_list(void)
{
  GSList *p;
  eviqs *i;

  scr_LogPrint(LPRINT_LOGNORM, "Events list:");
  for (p = evs_list; p; p = g_slist_next(p)) {
    i = p->data;
    scr_LogPrint(LPRINT_LOGNORM, "Id: %s", i->id);
  }
  scr_LogPrint(LPRINT_LOGNORM, "End of events list.");
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
