/*
 * histolog.c     -- File history handling
 * 
 * Copyright (C) 2005 Mikael Berthe <bmikael@lists.lilotux.net>
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
#include "screen.h"

static guint UseFileLogging;
static guint FileLoadLogs;
static char *RootDir;


//  user_histo_file()
// Returns history filename for the given jid
// Note: the caller *must* free the filename after use (if not null).
static char *user_histo_file(const char *jid)
{
  char *filename;
  char *lowerid, *p;
  if (!UseFileLogging && !FileLoadLogs) return NULL;

  lowerid = g_strdup(jid);
  for (p=lowerid; *p ; p++)
    *p = tolower(*p);

  filename = g_new(char, strlen(RootDir) + strlen(jid) + 1);
  strcpy(filename, RootDir);
  strcat(filename, lowerid);
  g_free(lowerid);
  return filename;
}

//  write_histo_line()
// Adds a history (multi-)line to the jid's history logfile
static void write_histo_line(const char *jid,
        time_t timestamp, guchar type, guchar info, const char *data)
{
  guint len = 0;
  FILE *fp;
  time_t ts;
  const char *p;
  char *filename;

  if (!UseFileLogging) return;

  filename = user_histo_file(jid);

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

  /* Line format: "TI DDDDDDDDDD LLL [data]"
   * T=Type, I=Info, DDDDDDDDDD=date, LLL=0-padded-len
   *
   * Types:
   * - M message    Info: S (send) R (receive)
   * - S status     Info: [oaifdcn]
   * We don't check them, we'll trust the caller.
   */

  fp = fopen(filename, "a");
  g_free(filename);
  if (!fp) return;

  fprintf(fp, "%c%c %10u %03d %s\n", type, info, (unsigned int)ts, len, data);
  fclose(fp);
}

//  hlog_read_history()
// Reads the jid's history logfile
void hlog_read_history(const char *jid, GList **p_buddyhbuf, guint width)
{
  char *filename;
  guchar type, info;
  char *data, *tail;
  time_t timestamp;
  guint prefix_flags;
  guint len;
  FILE *fp;
  guint err = 0;

  if (!FileLoadLogs) return;

  data = g_new(char, HBB_BLOCKSIZE+32);
  if (!data) {
    scr_LogPrint("Not enough memory to read history file");
    return;
  }

  filename = user_histo_file(jid);

  fp = fopen(filename, "r");
  g_free(filename);
  if (!fp) { g_free(data); return; }

  /* See write_histo_line() for line format... */
  while (!feof(fp)) {
    if (fgets(data, HBB_BLOCKSIZE+24, fp) == NULL) break;

    for (tail = data; *tail; tail++) ;

    type = data[0];
    info = data[1];
    if ((type != 'M' && type != 'S') || 
        (data[13] != ' ') || (data[17] != ' ')) {
      if (!err) {
        scr_LogPrint("Error in history file format (%s)", jid);
        err = 1;
      }
      //break;
      continue;
    }
    data[13] = data[17] = 0;
    timestamp = (unsigned long) atol(&data[3]);
    len = (unsigned long) atol(&data[14]);
    
    // Some checks
    if (((type == 'M') && (info != 'S' && info != 'R')) ||
        ((type == 'I') && (!strchr("OAIFDCN", info)))) {
      if (!err) {
        scr_LogPrint("Error in history file format (%s)", jid);
        err = 1;
      }
      //break;
      continue;
    }

    // FIXME This will fail when a message is too big
    while (len--) {
      if (fgets(tail, HBB_BLOCKSIZE+24 - (tail-data), fp) == NULL) break;

      while (*tail) tail++;
    }
    if ((tail > data+18) && (*(tail-1) == '\n'))
      *(tail-1) = 0;

    if (type == 'M') {
      if (info == 'S')
        prefix_flags = HBB_PREFIX_OUT;
      else
        prefix_flags = HBB_PREFIX_IN;
      hbuf_add_line(p_buddyhbuf, &data[18], timestamp, prefix_flags, width);
      err = 0;
    }
  }
  fclose(fp);
  g_free(data);
}

//  hlog_enable()
// Enable logging to files.  If root_dir is NULL, then $HOME/.mcabber is used.
// If loadfiles is TRUE, we will try to load buddies history logs from file.
void hlog_enable(guint enable, char *root_dir, guint loadfiles)
{
  UseFileLogging = enable;
  FileLoadLogs = loadfiles;

  if (enable || loadfiles) {
    if (root_dir) {
      int l = strlen(root_dir);
      if (l < 1) {
        scr_LogPrint("root_dir too short");
        UseFileLogging = FALSE;
        return;
      }
      // RootDir must be slash-terminated
      if (root_dir[l-1] == '/')
        RootDir = g_strdup(root_dir);
      else {
        RootDir = g_new(char, l+2);
        strcpy(RootDir, root_dir);
        strcat(RootDir, "/");
      }
    } else {
      char *home = getenv("HOME");
      char *dir = "/.mcabber/histo/";
      RootDir = g_new(char, strlen(home) + strlen(dir) + 1);
      strcpy(RootDir, home);
      strcat(RootDir, dir);
    }
    // FIXME
    // We should check the directory actually exists
  } else    // Disable history logging
    if (RootDir) {
    g_free(RootDir);
  }
}

inline void hlog_write_message(const char *jid, time_t timestamp, int sent,
        const char *msg)
{
  write_histo_line(jid, timestamp, 'M', ((sent) ? 'S' : 'R'), msg);
}

inline void hlog_write_status(const char *jid, time_t timestamp,
        enum imstatus status)
{
  // #1 XXX Check status value?
  // #2 We could add a user-readable comment
  write_histo_line(jid, timestamp, 'S', toupper(imstatus2char[status]),
          NULL);
}

