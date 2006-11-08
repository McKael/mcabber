/*
 * settings.c   -- Configuration stuff
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

#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

#include "settings.h"
#include "commands.h"
#include "utils.h"
#include "logprint.h"

static GSList *option;
static GSList *alias;
static GSList *binding;


typedef struct {
  gchar *name;
  gchar *value;
} T_setting;

static inline GSList **get_list_ptr(guint type)
{
  if      (type == SETTINGS_TYPE_OPTION)  return &option;
  else if (type == SETTINGS_TYPE_ALIAS)   return &alias;
  else if (type == SETTINGS_TYPE_BINDING) return &binding;
  return NULL;
}

// Return a pointer to the node with the requested key, or NULL if none found
static GSList *settings_find(GSList *list, const gchar *key)
{
  GSList *ptr;

  if (!list) return NULL;

  for (ptr = list ; ptr; ptr = g_slist_next(ptr))
    if (!strcasecmp(key, ((T_setting*)ptr->data)->name))
      break;

  return ptr;
}

/* -- */

//  cfg_read_file(filename)
// Read and parse config file "filename".  If filename is NULL,
// try to open the configuration file at the default locations.
//
int cfg_read_file(char *filename)
{
  FILE *fp;
  char *buf;
  char *line, *eol;
  unsigned int ln = 0;
  int err = 0;

  if (!filename) {
    // Use default config file locations
    char *home = getenv("HOME");
    if (!home) {
      scr_LogPrint(LPRINT_LOG, "Can't find home dir!");
      fprintf(stderr, "Can't find home dir!\n");
      return -1;
    }
    filename = g_new(char, strlen(home)+24);
    sprintf(filename, "%s/.mcabber/mcabberrc", home);
    if ((fp = fopen(filename, "r")) == NULL) {
      // 2nd try...
      sprintf(filename, "%s/.mcabberrc", home);
      if ((fp = fopen(filename, "r")) == NULL) {
        fprintf(stderr, "Cannot open config file!\n");
        g_free(filename);
        return -1;
      }
    }
    // Check configuration file permissions
    // As it could contain sensitive data, we make it user-readable only
    checkset_perm(filename, TRUE);
    // Check mcabber dir.  There we just warn, we don't change the modes
    sprintf(filename, "%s/.mcabber/", home);
    checkset_perm(filename, FALSE);
    g_free(filename);
  } else {
    if ((fp = fopen(filename, "r")) == NULL) {
      perror("Cannot open configuration file");
      return -2;
    }
    // Check configuration file permissions (see above)
    checkset_perm(filename, TRUE);
  }

  buf = g_new(char, 512);

  while (fgets(buf+1, 511, fp) != NULL) {
    // The first char is reserved to add a '/', to make a command line
    line = buf+1;
    ln++;

    // Strip leading spaces
    while (isspace(*line))
      line++;

    // Make eol point to the last char of the line
    for (eol = line ; *eol ; eol++)
      ;
    if (eol > line)
      eol--;

    // Strip trailing spaces
    while (eol > line && isspace(*eol))
      *eol-- = 0;

    // Ignore empty lines and comments
    if ((*line == '\n') || (*line == '\0') || (*line == '#'))
      continue;

    if ((strchr(line, '=') != NULL)) {
      // Only accept the set, alias and bind commands
      if (strncmp(line, "set ", strlen("set ")) &&
          strncmp(line, "bind ", strlen("bind ")) &&
          strncmp(line, "alias ", strlen("alias "))) {
        scr_LogPrint(LPRINT_LOGNORM,
                     "Error in configuration file (l. %d): bad command", ln);
        err++;
        continue;
      }
      // Set the leading COMMAND_CHAR to build a command line
      // and process the command
      *(--line) = COMMAND_CHAR;
      process_command(line);
    } else {
      scr_LogPrint(LPRINT_LOGNORM,
                   "Error in configuration file (l. %d): no assignment", ln);
      err++;
    }
  }
  g_free(buf);
  fclose(fp);
  return err;
}

//  parse_assigment(assignment, pkey, pval)
// Read assignment and split it to key, value
//
// If this is an assignment, the function will return TRUE and
// set *pkey and *pval (*pval is set to NULL if value field is empty).
//
// If this isn't a assignment (no = char), the function will set *pval
// to NULL and return FALSE.
//
// The caller should g_free() *pkey and *pval (if not NULL) after use.
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

  // If the value begins and ends with quotes ("), these quotes are
  // removed and whitespace is not stripped
  if ((t>val) && (*val == '"' && *t == '"')) {
    val++;
    t--;
  }
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

//  settings_get_status_msg(status)
// Return a string with the current status message:
// - if there is a user-defined message ("message" option),
//   return this message
// - if there is a user-defined message for the given status (and no
//   generic user message), it is returned
// - if no message is found, return NULL
const gchar *settings_get_status_msg(enum imstatus status)
{
  const gchar *rstatus = settings_opt_get("message");

  if (rstatus) return rstatus;

  switch(status) {
    case available:
        rstatus = settings_opt_get("message_avail");
        break;

    case freeforchat:
        rstatus = settings_opt_get("message_free");
        break;

    case dontdisturb:
        rstatus = settings_opt_get("message_dnd");
        break;

    case notavail:
        rstatus = settings_opt_get("message_notavail");
        break;

    case away:
        rstatus = settings_opt_get("message_away");
        break;

    default: // offline, invisible
        break;
  }
  return rstatus;
}

//  settings_foreach(type, pfunction, param)
// Call pfunction(param, key, value) for each setting with requested type.
void settings_foreach(guint type, void (*pfunc)(void *param, char *k, char *v),
                      void *param)
{
  GSList **plist;
  GSList *ptr;
  T_setting *setting;

  plist = get_list_ptr(type);

  if (!*plist) return;

  for (ptr = *plist ; ptr; ptr = g_slist_next(ptr)) {
    setting = ptr->data;
    pfunc(param, setting->name, setting->value);
  }
}


//  default_muc_nickname()
// Return the user's default nickname
// The caller should free the string after use
char *default_muc_nickname(void)
{
  char *nick;

  // We try the "nickname" option, then the username part of the jid.
  nick = (char*)settings_opt_get("nickname");
  if (nick)
    return g_strdup(nick);

  nick = g_strdup(settings_opt_get("username"));
  if (nick) {
    char *p = strchr(nick, JID_DOMAIN_SEPARATOR);
    if (p > nick)
      *p = 0;
  }
  return nick;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
