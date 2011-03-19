/*
 *  Module "urlregex"   -- Display received URL in the log window
 *
 * Copyright 2011 Mikael Berthe <mikael@lilotux.net>
 * Copyright 2008 Frank Zschockelt <mcabber@freakysoft.de>
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

#include <string.h>

#include <mcabber/logprint.h>
#include <mcabber/commands.h>
#include <mcabber/compl.h>
#include <mcabber/hooks.h>
#include <mcabber/screen.h>
#include <mcabber/settings.h>
#include <mcabber/modules.h>
#include <mcabber/config.h>

static void urlregex_init   (void);
static void urlregex_uninit (void);

/* Module description */
module_info_t info_urlregex = {
        .branch          = MCABBER_BRANCH,
        .api             = MCABBER_API_VERSION,
        .version         = MCABBER_VERSION,
        .description     = "Simple URL extractor\n"
                           " Displays URL in the log window.",
        .requires        = NULL,
        .init            = urlregex_init,
        .uninit          = urlregex_uninit,
        .next            = NULL,
};

static guint urlregex_hid = 0;  /* Hook handler id */

#ifdef HAVE_GLIB_REGEX
static GRegex *url_regex = NULL;
#endif

/* Helper function */
#ifdef HAVE_GLIB_REGEX
static inline void scr_log_urls(const gchar *string)
{
  GMatchInfo *match_info;

  g_regex_match_full(url_regex, string, -1, 0, 0, &match_info, NULL);
  while (g_match_info_matches(match_info)) {
    gchar *url = g_match_info_fetch(match_info, 0);
    scr_print_logwindow(url);
    g_free(url);
    g_match_info_next(match_info, NULL);
  }
  g_match_info_free(match_info);
}
#endif

/* Event handler */
static guint urlregex_hh(const gchar *hookname, hk_arg_t *args, gpointer userdata)
{
#ifdef HAVE_GLIB_REGEX
  if (url_regex) {
    int i;
    const char *msg = NULL;

    for (i = 0; args[i].name; i++) {
      if (!strcmp(args[i].name, "message")) {
        msg = args[i].value;
        break;
      }
    }
    if (msg)
      scr_log_urls(msg);
  }
#endif

  /* We're done, let the other handlers do their job! */
  return HOOK_HANDLER_RESULT_ALLOW_MORE_HANDLERS;
}

/* Initialization */
static void urlregex_init(void)
{
  if (settings_opt_get("url_regex")) {
#ifdef HAVE_GLIB_REGEX
    url_regex = g_regex_new(settings_opt_get("url_regex"),
                            G_REGEX_OPTIMIZE, 0, NULL);
    /* Add handler
     * We are only interested in incoming message events
     */
    urlregex_hid = hk_add_handler(urlregex_hh, HOOK_POST_MESSAGE_IN,
                                  G_PRIORITY_DEFAULT_IDLE, NULL);
#else
    scr_log_print(LPRINT_LOGNORM, "ERROR: Your glib version is too old, "
                  "cannot use url_regex.");
#endif // HAVE_GLIB_REGEX
  }
}

/* Uninitialization */
static void urlregex_uninit(void)
{
  /* Unregister event handler */
  hk_del_handler(HOOK_POST_MESSAGE_IN, urlregex_hid);
#ifdef HAVE_GLIB_REGEX
  if (url_regex) {
    g_regex_unref(url_regex);
    url_regex = NULL;
  }
#endif
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
