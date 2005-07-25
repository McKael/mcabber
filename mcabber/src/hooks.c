/*
 * hooks.c     -- Hooks layer
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

#include <sys/types.h>
#include <unistd.h>
#include <screen.h>

#include "hooks.h"
#include "roster.h"
#include "histolog.h"
#include "utf8.h"
#include "hbuf.h"

static char *extcmd;

inline void hk_message_in(const char *jid, time_t timestamp, const char *msg,
                          const char *type)
{
  int new_guy = FALSE;
  int message_flags;

  // If this user isn't in the roster, we add it
  if (!roster_exists(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT)) {
    roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER);
    new_guy = TRUE;
  }

  if (type && !strcmp(type, "error")) {
    message_flags = HBB_PREFIX_ERR | HBB_PREFIX_IN;
    scr_LogPrint(LPRINT_LOGNORM, "Error message received from <%s>", jid);
  } else
    message_flags = 0;

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteIncomingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteIncomingMessage(jid, msg, timestamp, message_flags);
  // We don't log the message if it is an error message
  if (!(message_flags & HBB_PREFIX_ERR))
    hlog_write_message(jid, timestamp, FALSE, msg);
  // External command
  hk_ext_cmd(jid, 'M', 'R', NULL);
  // We need to rebuild the list if the sender is unknown or
  // if the sender is offline/invisible and hide_offline_buddies is set
  if (new_guy ||
     (roster_getstatus(jid) == offline && buddylist_get_hide_offline_buddies()))
  {
    buddylist_build();
    update_roster = TRUE;
  }
}

inline void hk_message_out(const char *jid, time_t timestamp, const char *msg)
{
  scr_WriteOutgoingMessage(jid, msg);
  hlog_write_message(jid, timestamp, TRUE, msg);
  // External command
  hk_ext_cmd(jid, 'M', 'S', NULL);
}

inline void hk_statuschange(const char *jid, time_t timestamp, 
        enum imstatus status, const char *status_msg)
{
  scr_LogPrint(LPRINT_LOGNORM, "Buddy status has changed: [%c>%c] <%s> %s",
          imstatus2char[roster_getstatus(jid)], imstatus2char[status], jid,
          ((status_msg) ? status_msg : ""));
  roster_setstatus(jid, status, status_msg);
  buddylist_build();
  scr_DrawRoster();
  hlog_write_status(jid, 0, status, status_msg);
  // External command
  hk_ext_cmd(jid, 'S', imstatus2char[status], NULL);
}

inline void hk_mystatuschange(time_t timestamp,
        enum imstatus old_status, enum imstatus new_status, const char *msg)
{
  if (!msg && (old_status == new_status))
    return;

  scr_LogPrint(LPRINT_LOGNORM, "Your status has changed:  [%c>%c] %s",
          imstatus2char[old_status], imstatus2char[new_status],
          ((msg) ? msg : ""));
  //hlog_write_status(NULL, 0, status);
}


/* External commands */

//  hk_ext_cmd_init()
// Initialize external command variable.
// Can be called with parameter NULL to reset and free memory.
void hk_ext_cmd_init(const char *command)
{
  if (extcmd) {
    g_free(extcmd);
    extcmd = NULL;
  }
  if (command)
    extcmd = g_strdup(command);
}

//  hk_ext_cmd()
// Launch an external command (process) for the given event.
// For now, data should be NULL.
void hk_ext_cmd(const char *jid, guchar type, guchar info, const char *data)
{
  pid_t pid;
  char *arg_type = NULL;
  char *arg_info = NULL;
  char *arg_data = NULL;
  char status_str[2];

  if (!extcmd) return;

  // Prepare arg_* (external command parameters)
  switch (type) {
    case 'M':
        arg_type = "MSG";
        if (info == 'R')
          arg_info = "IN";
        else if (info == 'S')
          arg_info = "OUT";

        break;
    case 'S':
        arg_type = "STATUS";
        if (strchr(imstatus2char, tolower(info))) {
          status_str[0] = toupper(info);
          status_str[1] = 0;
          arg_info = status_str;
        }
        break;
    default:
        return;
  }

  if (!arg_type || !arg_info) return;

  if ((pid=fork()) == -1) {
    scr_LogPrint(LPRINT_LOGNORM, "Fork error, cannot launch external command.");
    return;
  }

  if (pid == 0) { // child
    if (execl(extcmd, extcmd, arg_type, arg_info, jid, arg_data) == -1) {
      // scr_LogPrint(LPRINT_LOGNORM, "Cannot execute external command.");
      exit(1);
    }
  }
}

