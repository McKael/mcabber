/*
 * histolog.c   -- File history handling
 *
 * Copyright (C) 2005-2008 Mikael Berthe <mikael@lilotux.net>
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "histolog.h"
#include "hbuf.h"
#include "utils.h"
#include "screen.h"
#include "settings.h"
#include "utils.h"
#include "roster.h"
#include "xmpp.h"

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
  while (path) {
    if (lstat(path, &bufstat) != 0)
      break;
    if (S_ISLNK(bufstat.st_mode)) {
      g_free(log_jid);
      log_jid = g_new0(char, bufstat.st_size+1);
      if (readlink(path, log_jid, bufstat.st_size) < 0) return NULL;
      g_free(path);
      path = user_histo_file(log_jid);
    } else
      break;
  }

  g_free(path);
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

  /* Line format: "TI yyyymmddThh:mm:ssZ LLL [data]"
   * T=Type, I=Info, yyyymmddThh:mm:ssZ=date, LLL=0-padded-len
   *
   * Types:
   * - M message    Info: S (send) R (receive) I (info)
   * - S status     Info: [_ofdnai]
   * We don't check them, we trust the caller.
   * (Info messages are not sent nor received, they're generated
   * locally by mcabber.)
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
  guint data_size;
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

  data_size = HBB_BLOCKSIZE+32;
  data = g_new(char, data_size);
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
    if (bufstat.st_size > 3145728) {
      scr_LogPrint(LPRINT_NORMAL, "Reading <%s> history file...", bjid);
      scr_DoUpdate();
    }
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
    guint noeol;

    if (fgets(data, data_size-1, fp) == NULL)
      break;
    ln++;

    while (1) {
      for (tail = data; *tail; tail++) ;
      noeol = (*(tail-1) != '\n');
      if (!noeol)
        break;
      /* TODO: duplicated code... could do better... */
      if (tail == data + data_size-2) {
        // The buffer is too small to contain the whole line.
        // Let's allocate some more space.
        if (!max_num_of_blocks ||
            data_size/HBB_BLOCKSIZE < 5U*max_num_of_blocks) {
          guint toffset = tail - data;
          // Allocate one more block.
          data_size = HBB_BLOCKSIZE * (1 + data_size/HBB_BLOCKSIZE);
          data = g_renew(char, data, data_size);
          // Update the tail pointer, as the data may have been moved.
          tail = data + toffset;
          if (fgets(tail, data_size-1 - (tail-data), fp) == NULL)
            break;
        } else {
          scr_LogPrint(LPRINT_LOGNORM, "Line too long in history file!");
          ln--;
          break;
        }
      }
    }

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
    if (((type == 'M') && (info != 'S' && info != 'R' && info != 'I')) ||
        ((type == 'S') && (!strchr("_OFDNAI", info)))) {
      if (!err) {
        scr_LogPrint(LPRINT_LOGNORM, "Error in history file format (%s), l.%u",
                     bjid, ln);
        err = 1;
      }
      continue;
    }

    while (len--) {
      ln++;
      if (fgets(tail, data_size-1 - (tail-data), fp) == NULL)
        break;

      while (*tail) tail++;
      noeol = (*(tail-1) != '\n');
      if (tail == data + data_size-2 && (len || noeol)) {
        // The buffer is too small to contain the whole message.
        // Let's allocate some more space.
        if (!max_num_of_blocks ||
            data_size/HBB_BLOCKSIZE < 5U*max_num_of_blocks) {
          guint toffset = tail - data;
          // If the line hasn't been read completely and we reallocate the
          // buffer, we want to read one more time.
          if (noeol)
            len++;
          // Allocate one more block.
          data_size = HBB_BLOCKSIZE * (1 + data_size/HBB_BLOCKSIZE);
          data = g_renew(char, data, data_size);
          // Update the tail pointer, as the data may have been moved.
          tail = data + toffset;
        } else {
          // There will probably be a parse error on next read, because
          // this message hasn't been read entirely.
          scr_LogPrint(LPRINT_LOGNORM, "Message too big in history file!");
        }
      }
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
      if (info == 'S') {
        prefix_flags = HBB_PREFIX_OUT | HBB_PREFIX_HLIGHT_OUT;
      } else {
        prefix_flags = HBB_PREFIX_IN;
        if (info == 'I')
          prefix_flags = HBB_PREFIX_INFO;
      }
      converted = from_utf8(&data[dataoffset+1]);
      if (converted) {
        xtext = ut_expand_tabs(converted); // Expand tabs
        hbuf_add_line(p_buddyhbuf, xtext, timestamp, prefix_flags, width,
                      max_num_of_blocks, 0);
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

guint hlog_is_enabled(void)
{
  return UseFileLogging;
}

inline void hlog_write_message(const char *bjid, time_t timestamp, int sent,
        const char *msg)
{
  guchar info;
  /* sent=1   message sent by mcabber
   * sent=0   message received by mcabber
   * sent=-1  local info message
   */
  if (sent == 1)
    info = 'S';
  else if (sent == 0)
    info = 'R';
  else
    info = 'I';
  write_histo_line(bjid, timestamp, 'M', info, msg);
}

inline void hlog_write_status(const char *bjid, time_t timestamp,
        enum imstatus status, const char *status_msg)
{
  // XXX Check status value?
  write_histo_line(bjid, timestamp, 'S', toupper(imstatus2char[status]),
          status_msg);
}


//  hlog_save_state()
// If enabled, save the current state of the roster
// (i.e. pending messages) to a temporary file.
void hlog_save_state(void)
{
  gpointer unread_ptr, first_unread;
  const char *bjid;
  char *statefile_xp;
  FILE *fp;
  const char *statefile = settings_opt_get("statefile");

  if (!statefile || !UseFileLogging)
    return;

  statefile_xp = expand_filename(statefile);
  fp = fopen(statefile_xp, "w");
  if (!fp) {
    scr_LogPrint(LPRINT_NORMAL, "Cannot open state file [%s]",
                 strerror(errno));
    goto hlog_save_state_return;
  }

  if (!lm_connection_is_authenticated(lconnection)) {
    // We're not connected.  Let's use the unread_jids hash.
    GList *unread_jid = unread_jid_get_list();
    unread_ptr = unread_jid;
    for ( ; unread_jid ; unread_jid = g_list_next(unread_jid))
      fprintf(fp, "%s\n", (char*)unread_jid->data);
    g_list_free(unread_ptr);
    goto hlog_save_state_return;
  }

  if (!current_buddy) // Safety check -- shouldn't happen.
    goto hlog_save_state_return;

  // We're connected.  Let's use unread_msg().
  unread_ptr = first_unread = unread_msg(NULL);
  if (!first_unread)
    goto hlog_save_state_return;

  do {
    guint type = buddy_gettype(unread_ptr);
    if (type & (ROSTER_TYPE_USER|ROSTER_TYPE_AGENT)) {
      bjid = buddy_getjid(unread_ptr);
      if (bjid)
        fprintf(fp, "%s\n", bjid);
    }
    unread_ptr = unread_msg(unread_ptr);
  } while (unread_ptr && unread_ptr != first_unread);

hlog_save_state_return:
  if (fp) {
    long filelen = ftell(fp);
    fclose(fp);
    if (!filelen)
      unlink(statefile_xp);
  }
  g_free(statefile_xp);
}

//  hlog_load_state()
// If enabled, load the current state of the roster
// (i.e. pending messages) from a temporary file.
// This function adds the JIDs to the unread_jids hash table,
// so it should only be called at startup.
void hlog_load_state(void)
{
  char bjid[1024];
  char *statefile_xp;
  FILE *fp;
  const char *statefile = settings_opt_get("statefile");

  if (!statefile || !UseFileLogging)
    return;

  statefile_xp = expand_filename(statefile);
  fp = fopen(statefile_xp, "r");
  if (fp) {
    char *eol;
    while (!feof(fp)) {
      if (fgets(bjid, sizeof bjid, fp) == NULL)
        break;
      // Let's remove the trailing newline.
      // Also remove whitespace, if the file as been (badly) manually modified.
      for (eol = bjid; *eol; eol++) ;
      for (eol--; eol >= bjid && (*eol == '\n' || *eol == ' '); *eol-- = 0) ;
      // Safety checks...
      if (!bjid[0])
        continue;
      if (check_jid_syntax(bjid)) {
        scr_LogPrint(LPRINT_LOGNORM,
                     "ERROR: Invalid JID in state file.  Corrupted file?");
        break;
      }
      // Display a warning if there are pending messages but the user
      // won't see them because load_log isn't set.
      if (!FileLoadLogs) {
        scr_LogPrint(LPRINT_LOGNORM, "WARNING: unread message from <%s>.",
                     bjid);
        scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
      }
      // Add the JID to unread_jids.  It will be used when the contact is
      // added to the roster.
      unread_jid_add(bjid);
    }
    fclose(fp);
  }
  g_free(statefile_xp);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
