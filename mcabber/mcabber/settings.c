/*
 * settings.c   -- Configuration stuff
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "settings.h"
#include "commands.h"
#include "logprint.h"
#include "otr.h"
#include "utils.h"
#include "xmpp.h"
#include "main.h"

// Maximum line length
// (probably best to use the same value as INPUTLINE_LENGTH)
#define CONFLINE_LENGTH  1024

static GHashTable *option;
static GHashTable *alias;
static GHashTable *binding;
static GHashTable *guards;

#ifdef HAVE_GPGME     /* PGP settings */
static GHashTable *pgpopt;

typedef struct {
  gchar *pgp_keyid;   /* KeyId the contact is supposed to use */
  guint pgp_disabled; /* If TRUE, PGP is disabled for outgoing messages */
  guint pgp_force;    /* If TRUE, PGP is used w/o negotiation */
} T_pgpopt;
#endif

typedef struct {
  settings_guard_t guard;
} installed_guard_t;

#ifdef HAVE_LIBOTR
static GHashTable *otrpolicy;
static enum otr_policy default_policy;
#endif

static inline GHashTable *get_hash(guint type)
{
  if      (type == SETTINGS_TYPE_OPTION)  return option;
  else if (type == SETTINGS_TYPE_ALIAS)   return alias;
  else if (type == SETTINGS_TYPE_BINDING) return binding;
#ifdef HAVE_LIBOTR
  else if (type == SETTINGS_TYPE_OTR)     return otrpolicy;
#endif
  return NULL;
}

/* -- */

void settings_init(void)
{
  option  = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
  alias   = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
  binding = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
  guards  = g_hash_table_new_full(&g_str_hash, &g_str_equal, &g_free, &g_free);
#ifdef HAVE_GPGME
  pgpopt = g_hash_table_new(&g_str_hash, &g_str_equal);
#endif
#ifdef HAVE_LIBOTR
  otrpolicy = g_hash_table_new(&g_str_hash, &g_str_equal);
#endif
}

//  cfg_read_file(filename, mainfile)
// Read and parse config file "filename".  If filename is NULL,
// try to open the configuration file at the default locations.
// mainfile must be set to TRUE for the startup config file.
// If mainfile is TRUE, the permissions of the configuration file will
// be fixed if they're insecure.
//
int cfg_read_file(char *filename, guint mainfile)
{
  static unsigned int runtime;
  FILE *fp;
  char *buf;
  char *line, *eol;
  unsigned int ln = 0;
  int err = 0;

  if (!filename) {
    // Use default config file locations
    char *home;
    GString *sfilename;

    if (!mainfile) {
      scr_LogPrint(LPRINT_LOGNORM, "No file name provided");
      return -1;
    }

    home = getenv("HOME");
    if (!home) {
      scr_LogPrint(LPRINT_LOG, "Can't find home dir!");
      fprintf(stderr, "Can't find home dir!\n");
      err = -1;
      goto cfg_read_file_return;
    }
    sfilename = g_string_new("");
    g_string_printf(sfilename, "%s/.mcabber/mcabberrc", home);
    if ((fp = fopen(sfilename->str, "r")) == NULL) {
      // 2nd try...
      g_string_printf(sfilename, "%s/.mcabberrc", home);
      if ((fp = fopen(sfilename->str, "r")) == NULL) {
        fprintf(stderr, "Cannot open config file!\n");
        g_string_free(sfilename, TRUE);
        err = -1;
        goto cfg_read_file_return;
      }
    }
    // Check configuration file permissions
    // As it could contain sensitive data, we make it user-readable only.
    checkset_perm(sfilename->str, TRUE);
    scr_LogPrint(LPRINT_LOGNORM, "Reading %s", sfilename->str);
    // Check mcabber dir.  Here we just warn, we don't change the modes.
    g_string_printf(sfilename, "%s/.mcabber/", home);
    checkset_perm(sfilename->str, FALSE);
    g_string_free(sfilename, TRUE);
  } else {
    // filename was specified
    if ((fp = fopen(filename, "r")) == NULL) {
      const char *msg = "Cannot open configuration file";
      if (mainfile)
        perror(msg);
      else
        scr_LogPrint(LPRINT_LOGNORM, "%s (%s).", msg, filename);
      err = -2;
      goto cfg_read_file_return;
    }
    // Check configuration file permissions (see above)
    // We don't change the permissions if that's not the main file.
    if (mainfile)
      checkset_perm(filename, TRUE);
    scr_LogPrint(LPRINT_LOGNORM, "Reading %s", filename);
  }

  buf = g_new(char, CONFLINE_LENGTH+1);

  while (fgets(buf+1, CONFLINE_LENGTH, fp) != NULL) {
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

    // If we aren't in runtime (i.e. startup) we'll only accept "safe" commands
    if (!runtime) {
      const gchar *cmdend = strchr(line, ' ');
      gchar *cmdname = NULL;
      gboolean safe;
      if (cmdend)
        cmdname = g_strndup(line, cmdend - line);
      safe = cmd_is_safe(cmdname ? cmdname : line);
      g_free(cmdname);
      if (!safe) {
        scr_log_print(LPRINT_LOGNORM, "Error in configuration file (l. %d): "
                      "this command can't be used here", ln);
        err++;
        continue;
      }
    }

    // Set the leading COMMAND_CHAR to build a command line
    // and process the command
    *(--line) = COMMAND_CHAR;
    process_command(line, TRUE);
  }
  g_free(buf);
  fclose(fp);

  if (filename)
    scr_LogPrint(LPRINT_LOGNORM, "Loaded %s.", filename);

cfg_read_file_return:
  // If we're done with the main file parsing, we can assume that
  // the next time this function is called will be at run time.
  if (mainfile)
    runtime = TRUE;
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
guint parse_assigment(gchar *assignment, gchar **pkey, gchar **pval)
{
  char *key, *val, *t, *p;

  *pkey = *pval = NULL;

  key = assignment;
  // Remove leading spaces in option name
  while ((!isalnum(*key)) && (*key != '=') && *key) {
    key++;
  }
  if (!*key) return FALSE; // Empty assignment

  if (*key == '=') {
    return FALSE;
  }
  // Ok, key points to the option name

  for (val = key+1 ; *val && (*val != '=') ; val++)
    if (!isalnum(*val) && !isblank(*val) && (*val != '_') && (*val != '-')) {
      // Key should only have alnum chars...
      return FALSE;
    }
  // Remove trailing spaces in option name:
  for (t = val-1 ; t > key && isblank(*t) ; t--)
    ;
  // Check for embedded whitespace characters
  for (p = key; p < t; p++) {
    if (isblank(*p)) {
      // Name should not contain space chars...
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

gboolean settings_set_guard(const gchar *key, settings_guard_t guard)
{
  if (!guard)
    g_hash_table_remove(guards, key);
  else {
    installed_guard_t *iguard = g_hash_table_lookup(guards, key);
    if (iguard)
      return FALSE;
    iguard = g_new(installed_guard_t, 1);
    iguard->guard = guard;
    g_hash_table_insert(guards, g_strdup(key), iguard);
  }
  return TRUE;
}

void settings_del_guard(const gchar *key)
{
  settings_set_guard(key, NULL);
}

void settings_opt_set_raw(const gchar *key, const gchar *value)
{
  if (!value)
    g_hash_table_remove(option, key);
  else
    g_hash_table_insert(option, g_strdup(key), g_strdup(value));
}

void settings_set(guint type, const gchar *key, const gchar *value)
{
  GHashTable *hash;
  gchar *dup_value = NULL;
  installed_guard_t *guard = NULL;

  if (type == SETTINGS_TYPE_OPTION) {
    guard = g_hash_table_lookup(guards, key);
    if (guard)
      dup_value = guard->guard(key, value);
  }

  hash = get_hash(type);
  if (!hash)
    return;

  if (!value && !dup_value)
    g_hash_table_remove(hash, key);
  else if (!guard)
    g_hash_table_insert(hash, g_strdup(key), g_strdup(value));
  else if (dup_value)
    g_hash_table_insert(hash, g_strdup(key), dup_value);
  else
    g_hash_table_remove(option, key);
}

void settings_del(guint type, const gchar *key)
{
  settings_set(type, key, NULL);
}

const gchar *settings_get(guint type, const gchar *key)
{
  GHashTable *hash;

  hash = get_hash(type);
  if (!hash)
    return NULL;

  return g_hash_table_lookup(hash, key);
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
// Call pfunction(key, value, param) for each setting with requested type.
void settings_foreach(guint type, void (*pfunc)(char *k, char *v, void *param),
                      void *param)
{
  GHashTable *hash;

  hash = get_hash(type);
  if (!hash)
    return;

  g_hash_table_foreach(hash, (GHFunc)pfunc, param);
}


//  default_muc_nickname()
// Return the user's default nickname
// The caller should free the string after use
char *default_muc_nickname(const char *roomid)
{
  char *nick;

  nick = (char*)xmpp_get_bookmark_nick(roomid);
  if (nick)
    return g_strdup(nick);

  // We try the "nickname" option, then the username part of the jid.
  nick = (char*)settings_opt_get("nickname");
  if (nick)
    return g_strdup(nick);

  nick = jid_get_username(settings_opt_get("jid"));
  return nick;
}


/* PGP settings */

//  settings_pgp_setdisabled(jid, value)
// Enable/disable PGP encryption for jid.
// (Set value to TRUE to disable encryption)
void settings_pgp_setdisabled(const char *bjid, guint value)
{
#ifdef HAVE_GPGME
  T_pgpopt *pgpdata;
  pgpdata = g_hash_table_lookup(pgpopt, bjid);
  if (!pgpdata) {
    // If value is 0, we do not need to create a structure (that's
    // the default value).
    if (value) {
      pgpdata = g_new0(T_pgpopt, 1);
      pgpdata->pgp_disabled = value;
      g_hash_table_insert(pgpopt, g_strdup(bjid), pgpdata);
    }
  } else {
    pgpdata->pgp_disabled = value;
    // We could remove the key/value if pgp_disabled is 0 and
    // pgp_keyid is NULL, actually.
  }
#endif
}

//  settings_pgp_getdisabled(jid)
// Return TRUE if PGP encryption should be disabled for jid.
guint settings_pgp_getdisabled(const char *bjid)
{
#ifdef HAVE_GPGME
  T_pgpopt *pgpdata;
  pgpdata = g_hash_table_lookup(pgpopt, bjid);
  if (pgpdata)
    return pgpdata->pgp_disabled;
  else
    return FALSE; // Default: not disabled
#else
  return TRUE;    // No PGP support, let's say it's disabled.
#endif
}

//  settings_pgp_setforce(jid, value)
// Force (or not) PGP encryption for jid.
// When value is TRUE, PGP support will be assumed for the remote client.
void settings_pgp_setforce(const char *bjid, guint value)
{
#ifdef HAVE_GPGME
  T_pgpopt *pgpdata;
  pgpdata = g_hash_table_lookup(pgpopt, bjid);
  if (!pgpdata) {
    // If value is 0, we do not need to create a structure (that's
    // the default value).
    if (value) {
      pgpdata = g_new0(T_pgpopt, 1);
      pgpdata->pgp_force = value;
      g_hash_table_insert(pgpopt, g_strdup(bjid), pgpdata);
    }
  } else {
    pgpdata->pgp_force = value;
  }
  if (value && pgpdata && !pgpdata->pgp_keyid)
    scr_LogPrint(LPRINT_NORMAL, "Warning: the Key Id is not set!");
#endif
}

//  settings_pgp_getforce(jid)
// Return TRUE if PGP enforcement is set for jid.
guint settings_pgp_getforce(const char *bjid)
{
#ifdef HAVE_GPGME
  T_pgpopt *pgpdata;
  pgpdata = g_hash_table_lookup(pgpopt, bjid);
  if (pgpdata)
    return pgpdata->pgp_force;
  else
    return FALSE; // Default
#else
  return FALSE;   // No PGP support
#endif
}

//  settings_pgp_setkeyid(jid, keyid)
// Set the PGP KeyId for user jid.
// Use keyid = NULL to erase the previous KeyId.
void settings_pgp_setkeyid(const char *bjid, const char *keyid)
{
#ifdef HAVE_GPGME
  T_pgpopt *pgpdata;
  pgpdata = g_hash_table_lookup(pgpopt, bjid);
  if (!pgpdata) {
    // If keyid is NULL, we do not need to create a structure (that's
    // the default value).
    if (keyid) {
      pgpdata = g_new0(T_pgpopt, 1);
      pgpdata->pgp_keyid = g_strdup(keyid);
      g_hash_table_insert(pgpopt, g_strdup(bjid), pgpdata);
    }
  } else {
    g_free(pgpdata->pgp_keyid);
    if (keyid)
      pgpdata->pgp_keyid = g_strdup(keyid);
    else
      pgpdata->pgp_keyid = NULL;
    // We could remove the key/value if pgp_disabled is 0 and
    // pgp_keyid is NULL, actually.
  }
#endif
}

//  settings_pgp_getkeyid(jid)
// Get the PGP KeyId for user jid.
const char *settings_pgp_getkeyid(const char *bjid)
{
#ifdef HAVE_GPGME
  T_pgpopt *pgpdata;
  pgpdata = g_hash_table_lookup(pgpopt, bjid);
  if (pgpdata)
    return pgpdata->pgp_keyid;
#endif
  return NULL;
}

/* otr settings */

#ifdef HAVE_LIBOTR
static void remove_default_policies(char *k, char *policy, void *defaultp)
{
  if (*(enum otr_policy *)policy == *(enum otr_policy *)defaultp) {
    g_free((enum otr_policy *) policy);
    g_hash_table_remove(otrpolicy, k);
  }
}
#endif

void settings_otr_setpolicy(const char *bjid, guint value)
{
#ifdef HAVE_LIBOTR
  enum otr_policy *otrdata;

  if (!bjid) {
    default_policy = value;
    /* refresh hash */
    settings_foreach(SETTINGS_TYPE_OTR, &remove_default_policies, &value);
    return;
  }

  otrdata = g_hash_table_lookup(otrpolicy, bjid);

  if (value == default_policy) {
    if (otrdata) {
      g_free(otrdata);
      g_hash_table_remove(otrpolicy, bjid);
    }
  } else if (otrdata) {
    *otrdata = value;
  } else {
    otrdata = g_new(enum otr_policy, 1);
    *otrdata = value;
    g_hash_table_insert(otrpolicy, g_strdup(bjid), otrdata);
  }
#endif
}

guint settings_otr_getpolicy(const char *bjid)
{
#ifdef HAVE_LIBOTR
  enum otr_policy *otrdata;
  if (!bjid)
    return default_policy;

  otrdata = g_hash_table_lookup(otrpolicy, bjid);
  if (otrdata)
    return *otrdata;
  else
    return default_policy;
#else
  return 0;
#endif
}

guint get_max_history_blocks(void)
{
  int max_num_of_blocks = settings_opt_get_int("max_history_blocks");
  if (max_num_of_blocks < 0)
    max_num_of_blocks = 0;
  else if (max_num_of_blocks == 1)
    max_num_of_blocks = 2;
  return (guint)max_num_of_blocks;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
