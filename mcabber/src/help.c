/*
 * help.c       -- Help command
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>

#include "settings.h"
#include "logprint.h"
#include "utils.h"

#define DEFAULT_LANG "en"

//  get_lang()
// Return the language code string (a 2-letters string).
static const char *get_lang(void) {
  static const char *lang_str = DEFAULT_LANG;
#ifdef DATA_ROOT_DIR
  static char lang[3];
  const char *opt_l;
  opt_l = settings_opt_get("lang");
  if (opt_l && strlen(opt_l) == 2 && isalpha(opt_l[0]) && isalpha(opt_l[1])) {
    strncpy(lang, opt_l, sizeof(lang));
    mc_strtolower(lang);
    lang_str = lang;
  }
#endif /* DATA_ROOT_DIR */
  return lang_str;
}

//  help_process(string)
// Display help about the "string" command.
// If string is null, display general help.
// Return 0 in case of success.
int help_process(char *string)
{
#ifndef DATA_ROOT_DIR
  scr_LogPrint(LPRINT_NORMAL, "Help isn't available.");
  return -1;
#else
  const char *lang;
  FILE *fp;
  char *helpfiles_dir, *filename;
  char *data;
  const int datasize = 4096;
  char *p;

  // Check string is ok
  for (p = string; p && *p; p++) {
    if (!isalnum(*p) && *p != '_' && *p != '-') {
      scr_LogPrint(LPRINT_NORMAL, "Cannot find help (invalid keyword).");
      return 1;
    }
  }

  // Look for help file
  lang = get_lang();
  helpfiles_dir = g_strdup_printf("%s/mcabber/help", DATA_ROOT_DIR);
  if (string && *string) {
    p = g_strdup(string);
    mc_strtolower(p);
    filename = g_strdup_printf("%s/%s/hlp_%s.txt", helpfiles_dir, lang, p);
    g_free(p);
  } else
    filename = g_strdup_printf("%s/%s/hlp.txt", helpfiles_dir, lang);

  fp = fopen(filename, "r");
  g_free(filename);
  g_free(helpfiles_dir);

  if (!fp) {
    scr_LogPrint(LPRINT_NORMAL, "No help found.");
    return -1;
  }

  data = g_new(char, datasize);
  while (!feof(fp)) {
    if (fgets(data, datasize, fp) == NULL) break;
    // Strip trailing newline
    for (p = data; *p; p++) ;
    if (p > data)
      p--;
    if (*p == '\n' || *p == '\r')
      *p = '\0';
    // Displaty the help line
    scr_LogPrint(LPRINT_NORMAL, "%s", data);
  }
  fclose(fp);
  g_free(data);

  return 0;
#endif /* DATA_ROOT_DIR */
}


/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
