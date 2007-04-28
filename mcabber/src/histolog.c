/*
 * histolog.c   -- File history handling
 *
 * Copyright (C) 2005-2007 Mikael Berthe <mikael@lilotux.net>
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

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "histolog.h"
#include "hbuf.h"
#include "jabglue.h"
#include "utils.h"
#include "logprint.h"
#include "settings.h"
#include "utils.h"

static guint UseFileLogging;
static guint FileLoadLogs;
static char *RootDir;


//  user_histo_file(jid)
// Returns history filename for the given jid
// Note: the caller *must* free the filename after use (if not null).
static char *user_histo_file(const char *bjid)
{
  char *filename;
  char *lowerid;

  if (!(UseFileLogging || FileLoadLogs))
    return NULL;

  lowerid = g_strdup(bjid);
  if (!lowerid)
    return NULL;
  mc_strtolower(lowerid);

  filename = g_strdup_printf("%s%s", RootDir, lowerid);
  g_free(lowerid);
  return filename;
}

char *hlog_get_log_jid(const char *bjid)
{
  struct stat bufstat;
  char *path;
  char *log_jid = NULL;
  
  path = user_histo_file(bjid);
  do {  
    /*scr_LogPrint(LPRINT_NORMAL, "path=%s", path);*/
    if(lstat(path, &bufstat) != 0)
      break;
    if(S_ISLNK(bufstat.st_mode)) {
      g_free(log_jid);
      log_jid = g_new(char, bufstat.st_size+1);
      readlink(path, log_jid, bufstat.st_size);
      g_free(path);
      log_jid[bufstat.st_size] = '\0';
      path = user_histo_file(log_jid);
    } else {
      g_free(path);
      path = NULL;
    }
  } while( path );

  return log_jid;
}

//  write_histo_line()
// Adds a history (multi-)line to the jid's history logfile
static void write_histo_line(const char *bjid,
        time_t timestamp, guchar type, guchar info, const char *data)
{
  guint len = 0;
  FILE *fp;
  time_t ts;
  const char *p;
  char *filename;
  char str_ts[20];
  int err;

  if (!UseFileLogging)
    return;

  // Do not log status messages when 'logging_ignore_status' is set
  if (type == 'S' && settings_opt_get_int("logging_ignore_status"))
    return;

  filename = user_histo_file(bjid);

  // If timestamp is null, get current date
  if (timestamp)
    ts = timestamp;
  else
    time(&ts);

  if (!data)
    data = "";

  // Count number of extra lines
  for (p=data ; *p ; p++)
    if (*p == '\n') len++;

  /* Line format: "TI yyyymmddThh:mm:ssZ [data]"
   * (Old format: "TI DDDDDDDDDD LLL [data])"
   * T=Type, I=Info, yyyymmddThh:mm:ssZ=date, LLL=0-padded-len
   *
   * Types:
   * - M message    Info: S (send) R (receive)
   * - S status     Info: [_oifdna]
   * We don't check them, we'll trust the caller.
   */

  fp = fopen(filename, "a");
  g_free(filename);
  if (!fp) {
    scr_LogPrint(LPRINT_LOGNORM, "Unable to write history "
                 "(cannot open logfile)");
    return;
  }

  to_iso8601(str_ts, ts);
  err = fprintf(fp, "%c%c %-18.18s %03d %s\n", type, info, str_ts, len, data);
  fclose(fp);
  if (err < 0) {
    scr_LogPrint(LPRINT_LOGNORM, "Error while writing to log file: %s",
                 strerror(errno));
  }
}

//  hlog_read_history()
// Reads the jid's history logfile
void hlog_read_history(const char *bjid, GList **p_buddyhbuf, guint width)
{
  char *filename;
  guchar type, info;
  char *data, *tail;
  char *xtext;
  time_t timestamp;
  guint prefix_flags;
  guint len;
  FILE *fp;
  struct stat bufstat;
  guint err = 0;
  guint ln = 0; // line number
  time_t starttime;
  int max_num_of_blocks;

  if (!FileLoadLogs)
    return;

  if ((roster_gettype(bjid) & ROSTER_TYPE_ROOM) &&
      (settings_opt_get_int("load_muc_logs") != 1))
    return;

  data = g_new(char, HBB_BLOCKSIZE+32);
  if (!data) {
    scr_LogPrint(LPRINT_LOGNORM, "Not enough memory to read history file");
    return;
  }

  filename = user_histo_file(bjid);

  fp = fopen(filename, "r");
  g_free(filename);
  if (!fp) {
    g_free(data);
    return;
  }

  // If file is large (> 3MB here), display a message to inform the user
  // (it can take a while...)
  if (!fstat(fileno(fp), &bufstat)) {
    if (bufstat.st_size > 3145728)
      scr_LogPrint(LPRINT_LOGNORM, "Reading <%s> history file...", bjid);
  }

  max_num_of_blocks = get_max_history_blocks();

  starttime = 0L;
  if (settings_opt_get_int("max_history_age") > 0) {
    int maxdays = settings_opt_get_int("max_history_age");
    time(&starttime);
    if (maxdays >= starttime/86400L)
      starttime = 0L;
    else
      starttime -= maxdays * 86400L;
  }

  /* See write_histo_line() for line format... */
  while (!feof(fp)) {
    guint dataoffset = 25;

    if (fgets(data, HBB_BLOCKSIZE+27, fp) == NULL)
      break;
    ln++;

    for (tail = data; *tail; tail++) ;

    type = data[0];
    info = data[1];

    if ((type != 'M' && type != 'S') ||
        ((data[11] != 'T') || (data[20] != 'Z') ||
         (data[21] != ' ') ||
         (data[25] != ' ' && data[26] != ' '))) {
      if (!err) {
        scr_LogPrint(LPRINT_LOGNORM,
                     "Error in history file format (%s), l.%u", bjid, ln);
        err = 1;
      }
      continue;
    }
    // The number of lines can be written with 3 or 4 bytes.
    if (data[25] != ' ') dataoffset = 26;
    data[21] = data[dataoffset] = 0;
    timestamp = from_iso8601(&data[3], 1);
    len = (guint) atoi(&data[22]);

    // Some checks
    if (((type == 'M') && (info != 'S' && info != 'R')) ||
        ((type == 'I') && (!strchr("OAIFDN", info)))) {
      if (!err) {
        scr_LogPrint(LPRINT_LOGNORM, "Error in history file format (%s), l.%u",
                     bjid, ln);
        err = 1;
      }
      continue;
    }

    // XXX This will fail when a message is too big
    while (len--) {
      ln++;
      if (fgets(tail, HBB_BLOCKSIZE+27 - (tail-data), fp) == NULL)
        break;

      while (*tail) tail++;
    }
    // Small check for too long messages
    if (tail >= HBB_BLOCKSIZE+dataoffset+1 + data) {
      // Maybe we will have a parse error on next, because this
      // message is big (maybe too big).
      scr_LogPrint(LPRINT_LOGNORM, "A message could be too big "
                   "in history file...");
    }
    // Remove last CR (we keep it if the line is empty, too)
    if ((tail > data+dataoffset+1) && (*(tail-1) == '\n'))
      *(tail-1) = 0;

    // Check if the data is older than max_history_age
    if (starttime) {
      if (timestamp > starttime)
        starttime = 0L; // From now on, load everything
      else
        continue;
    }

    if (type == 'M') {
      char *converted;
      if (info == 'S')
        prefix_flags = HBB_PREFIX_OUT | HBB_PREFIX_HLIGHT_OUT;
      else
        prefix_flags = HBB_PREFIX_IN;
      converted = from_utf8(&data[dataoffset+1]);
      if (converted) {
        xtext = ut_expand_tabs(converted); // Expand tabs
        hbuf_add_line(p_buddyhbuf, xtext, timestamp, prefix_flags, width,
                      max_num_of_blocks);
        if (xtext != converted)
          g_free(xtext);
        g_free(converted);
      }
      err = 0;
    }
  }
  fclose(fp);
  g_free(data);
}

//  hlog_enable()
// Enable logging to files.  If root_dir is NULL, then $HOME/.mcabber is used.
// If loadfiles is TRUE, we will try to load buddies history logs from file.
void hlog_enable(guint enable, const char *root_dir, guint loadfiles)
{
  UseFileLogging = enable;
  FileLoadLogs = loadfiles;

  if (enable || loadfiles) {
    if (root_dir) {
      char *xp_root_dir;
      int l = strlen(root_dir);
      if (l < 1) {
        scr_LogPrint(LPRINT_LOGNORM, "Error: logging dir name too short");
        UseFileLogging = FileLoadLogs = FALSE;
        return;
      }
      xp_root_dir = expand_filename(root_dir);
      // RootDir must be slash-terminated
      if (root_dir[l-1] == '/') {
        RootDir = xp_root_dir;
      } else {
        RootDir = g_strdup_printf("%s/", xp_root_dir);
        g_free(xp_root_dir);
      }
    } else {
      char *home = getenv("HOME");
      const char *dir = "/.mcabber/histo/";
      RootDir = g_strdup_printf("%s%s", home, dir);
    }
    // Check directory permissions (should not be readable by group/others)
    if (checkset_perm(RootDir, TRUE) == -1) {
      // The directory does not actually exists
      g_free(RootDir);
      RootDir = NULL;
      scr_LogPrint(LPRINT_LOGNORM, "ERROR: Cannot access "
                   "history log directory, logging DISABLED");
      UseFileLogging = FileLoadLogs = FALSE;
    }
  } else {  // Disable history logging
    g_free(RootDir);
    RootDir = NULL;
  }
}

inline void hlog_write_message(const char *bjid, time_t timestamp, int sent,
        const char *msg)
{
  write_histo_line(bjid, timestamp, 'M', ((sent) ? 'S' : 'R'), msg);
}

inline void hlog_write_status(const char *bjid, time_t timestamp,
        enum imstatus status, const char *status_msg)
{
  // XXX Check status value?
  write_histo_line(bjid, timestamp, 'S', toupper(imstatus2char[status]),
          status_msg);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
