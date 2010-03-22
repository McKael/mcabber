/*
 * help.c       -- Help command
 *
 * Copyright (C) 2006-2009 Mikael Berthe <mikael@lilotux.net>
 * Copyrigth (C) 2009      Myhailo Danylenko <isbear@ukrpost.net>
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

/*
 * How it works
 *
 * Main calls help_init, that installs option guards. These guards do
 * nothing, but set help_dirs_stalled flag. When user issues help command,
 * it checks, if help_dirs_stalled flag is set, and if it is, it calls
 * init_help_dirs before performing help search.
 *
 * Options:
 *   lang       List of semicolon-separated language codes. If unset, will
 *              be detected from locale, with fallback to english.
 *   help_dirs  List of semicolon-seaparated directories, where search for
 *              help (in language subdirectories) will be performed.
 *              Defaults to DATA_DIR/mcabber/help.
 *   help_to_current  Print help to current buddy's buffer.
 *
 * XXX:
 *   Remove command list from hlp.txt and print detected list of all help
 *   topics?
 */

#include <glib.h>
#include <string.h>
#include <locale.h>
#include <sys/types.h>
#include <dirent.h>

#include "logprint.h"
#include "screen.h"
#include "hbuf.h"
#include "settings.h"
#include "utils.h"

static GSList   *help_dirs         = NULL;
static gboolean  help_dirs_stalled = TRUE;

void free_help_dirs(void)
{
  GSList *hel;

  for (hel = help_dirs; hel; hel = hel->next)
    g_free(hel->data);

  g_slist_free(help_dirs);

  help_dirs = NULL;
}

void dir_push_languages(const char *langs, const char *dir)
{
  const char *lstart = langs;
  const char *lend;
  char       *path   = expand_filename(dir);

  for (lend = strchr(lstart, ';'); lend; lend = strchr(lstart, ';')) {
    char *lang = g_strndup(lstart, lend - lstart);
    char *dir  = g_strdup_printf("%s/%s", path, lang);

    help_dirs = g_slist_append(help_dirs, dir);

    g_free(lang);
    lstart = lend + 1;
  }

  { // finishing element
    char *dir = g_strdup_printf("%s/%s", path, lstart);

    help_dirs = g_slist_append(help_dirs, dir);
  }

  g_free(path);
}

void init_help_dirs(void)
{
  const char *paths;
  const char *langs;
  char        lang[6];

  if (help_dirs)
    free_help_dirs();

  // initialize variables
  paths = settings_opt_get("help_dirs");
  if (!paths || !*paths)
#ifdef DATA_DIR
    paths = DATA_DIR "/mcabber/help";
#else
    paths = "/usr/local/share/mcabber/help;/usr/share/mcabber/help";
#endif

  langs = settings_opt_get("lang");

  if (!langs || !*langs) {
    char *locale = setlocale(LC_MESSAGES, NULL);

    // XXX crude method to distinguish between xx_XX xx xx@xxx
    // and C POSIX NULL etc.
    if (locale && isalpha(locale[0]) && isalpha(locale[1])
        && !isalpha(locale[2])) {
      lang[0] = locale[0];
      lang[1] = locale[1];

      if (lang[0] == 'e' && lang[1] == 'n')
        lang[2] = '\0';
      else {
        lang[2] = ';';
        lang[3] = 'e';
        lang[4] = 'n';
        lang[5] = '\0';
      }

      langs = lang;
    } else
      langs = "en";
  }

  { // parse
    const char *pstart = paths;
    const char *pend;

    for (pend = strchr(pstart, ';'); pend; pend = strchr(pstart, ';')) {
      char *path = g_strndup(pstart, pend - pstart);

      dir_push_languages(langs, path);

      g_free(path);
      pstart = pend + 1;
    }

    // last element
    dir_push_languages(langs, pstart);
  }

  help_dirs_stalled = FALSE;
}

static gboolean do_help_in_dir(const char *arg, const char *path, const char *jid)
{
  char       *fname;
  GIOChannel *channel;
  GString    *line;
  int         lines   = 0;

  if (arg && *arg)
    fname = g_strdup_printf("%s/hlp_%s.txt", path, arg);
  else
    fname = g_strdup_printf("%s/hlp.txt", path);

  channel = g_io_channel_new_file(fname, "r", NULL);

  if (!channel)
    return FALSE;

  line = g_string_new(NULL);

  while (TRUE) {
    gsize     endpos;
    GIOStatus ret;

    ret = g_io_channel_read_line_string(channel, line, &endpos, NULL);
    if (ret != G_IO_STATUS_NORMAL) // XXX G_IO_STATUS_AGAIN?
      break;

    line->str[endpos] = '\0';

    if (jid)
      scr_WriteIncomingMessage(jid, line->str, 0,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
    else
      scr_LogPrint(LPRINT_NORMAL, "%s", line->str);

    ++lines;
  }

  g_string_free(line, TRUE);

  if (!lines)
    return FALSE;

  if (!jid) {
    scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
    scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                   ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
    update_roster = TRUE;
  }

  return TRUE;
}

void help_process(char *arg)
{
  gchar      *string;
  const char *jid    = NULL;
  gboolean    done   = FALSE;

  if (help_dirs_stalled)
    init_help_dirs();

  { // check input
    char *c;

    for (c = arg; *c; ++c)
      if (!isalnum(*c) && *c != '-' && *c != '_') {
        scr_LogPrint(LPRINT_NORMAL, "Wrong help expression, "
                     "it can contain only alphbetic, numeric"
                     " characters and symbols '-' and '_'.");
        return;
      }

    string = g_strdup(arg);
    mc_strtolower(string);
  }

  if (settings_opt_get_int("help_to_current") && CURRENT_JID)
    jid = CURRENT_JID;

  { // search
    GSList *hel;

    for (hel = help_dirs; hel && !done; hel = hel->next) {
      char *dir = (char *)hel->data;
      done = do_help_in_dir(string, dir, jid);
    }
  }

  if (!done && string && *string) { // match and print any similar topics
    GSList *hel;
    GSList *matches = NULL;

    for (hel = help_dirs; hel; hel = hel->next) {
      const char *path = (const char *)hel->data;
      DIR        *dd   = opendir(path);

      if (dd) {
        struct dirent *file;

        for (file = readdir(dd); file; file = readdir(dd)) {
          const char *name = file->d_name;

          if (name && name[0] == 'h' && name[1] == 'l' &&
                      name[2] == 'p' && name[3] == '_') {
            const char *nstart = name + 4;
            const char *nend   = strrchr(nstart, '.');

            if (nend) {
              gsize len = nend - nstart;

              if (g_strstr_len(nstart, len, string)) {
                gchar *match = g_strndup(nstart, len);

                if (!g_slist_find_custom(matches, match,
                                         (GCompareFunc)strcmp))
                  matches = g_slist_append(matches, match);
                else
                  g_free(match);

                done = TRUE;
              }
            }
          }
        }

        closedir(dd);
      }
    }

    if (done) {
      GString *message = g_string_new("No exact match found. "
                                      "Keywords, that contain this word:");
      GSList  *wel;

      for (wel = matches; wel; wel = wel->next) {
        gchar *word = (gchar *)wel->data;

        g_string_append_printf(message, " %s,", word);

        g_free(wel->data);
      }

      message->str[message->len - 1] = '.';

      g_slist_free(matches);

      {
        char *msg = g_string_free(message, FALSE);

        if (jid)
          scr_WriteIncomingMessage(jid, msg, 0,
                                   HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
        else
          scr_LogPrint(LPRINT_NORMAL, "%s", msg);

        g_free(msg);
      }
    }
  }

  if (!done) {
    if (jid) // XXX
      scr_WriteIncomingMessage(jid, "No help found.", 0,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
    else
      scr_LogPrint(LPRINT_NORMAL, "No help found.");
  }

  g_free(string);
}

static gchar *help_guard(const gchar *key, const gchar *new_value)
{
  help_dirs_stalled = TRUE;
  return g_strdup(new_value);
}

void help_init(void)
{
  settings_set_guard("lang", help_guard);
  settings_set_guard("help_dirs", help_guard);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
