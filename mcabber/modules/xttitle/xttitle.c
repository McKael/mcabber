/*
 *  Module "xttitle"    -- Update X terminal title
 *
 * Copyright (C) 2010 Mikael Berthe <mikael@lilotux.net>
 *
 * The option 'xttitle_short_format' can be set to 1 to use a very
 * short terminal title.
 *
 * This module is free software; you can redistribute it and/or modify
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <mcabber/modules.h>
#include <mcabber/settings.h>
#include <mcabber/hooks.h>
#include <mcabber/logprint.h>

static void xttitle_init(void);
static void xttitle_uninit(void);

/* Module description */
module_info_t info_xttitle = {
        .branch         = MCABBER_BRANCH,
        .api            = MCABBER_API_VERSION,
        .version        = MCABBER_VERSION,
        .description    = "Show unread message count in X terminal title",
        .requires       = NULL,
        .init           = xttitle_init,
        .uninit         = xttitle_uninit,
        .next           = NULL,
};

// Hook handler id
static guint unread_list_hid;

// Event handler for HOOK_UNREAD_LIST_CHANGE events
static guint unread_list_hh(const gchar *hookname, hk_arg_t *args,
                            gpointer userdata)
{
  guint all_unread = 0;
  guint muc_unread = 0;
  guint muc_attention = 0;
  guint unread; // private message count
  static gchar buf[128];

  // Note: We can add "attention" string later, but it isn't used
  // yet in mcabber...
  for ( ; args->name; args++) {
    if (!g_strcmp0(args->name, "unread")) {
      all_unread = atoi(args->value);
    } else if (!g_strcmp0(args->name, "muc_unread")) {
      muc_unread = atoi(args->value);
    } else if (!g_strcmp0(args->name, "muc_attention")) {
      muc_attention = atoi(args->value);
    }
  }

  // Let's not count the MUC unread buffers that don't have the attention
  // flag (that is, MUC buffer that have no highlighted messages).
  unread = all_unread - (muc_unread - muc_attention);

  // TODO: let the user use a format string, instead of hard-coded defaults...
  if (settings_opt_get_int("xttitle_short_format") == 1) {
    // Short title message
    if (!all_unread)
      snprintf(buf, sizeof(buf), "MCabber");
    else if (unread == all_unread)
      snprintf(buf, sizeof(buf), "MCabber (%u)", unread);
    else
      snprintf(buf, sizeof(buf), "MCabber (%u/%u)", unread, all_unread);
  } else {
    // Long title message
    if (muc_unread) {
      snprintf(buf, sizeof(buf), "MCabber -- %u message%c (total:%u / MUC:%u)",
               unread, (unread > 1 ? 's' : ' '), all_unread, muc_unread);
    } else {
      if (unread)
        snprintf(buf, sizeof(buf), "MCabber -- %u message%c", unread,
                 (unread > 1 ? 's' : ' '));
      else
        snprintf(buf, sizeof(buf), "MCabber -- No message");
    }
  }

  // Update the terminal title
  printf("\033]0;%s\007", buf);

  return HOOK_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

// Initialization
static void xttitle_init(void)
{
  // Add hook handler for unread message data
  unread_list_hid = hk_add_handler(unread_list_hh, HOOK_UNREAD_LIST_CHANGE,
                                   G_PRIORITY_DEFAULT_IDLE, NULL);
  // Default title
  printf("\033]0;MCabber\007");
}

// Uninitialization
static void xttitle_uninit(void)
{
  // Unregister handler
  hk_del_handler(HOOK_UNREAD_LIST_CHANGE, unread_list_hid);
  // Reset title
  printf("\033]0;MCabber\007");
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
