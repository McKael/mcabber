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

static char *extcommand;

inline void hk_message_in(const char *jid, time_t timestamp, const char *msg)
{
  int new_guy = FALSE;

  // If this user isn't in the roster, we add it
  if (!roster_exists(jid, jidsearch, ROSTER_TYPE_USER|ROSTER_TYPE_AGENT)) {
    roster_add_user(jid, NULL, NULL, ROSTER_TYPE_USER);
    new_guy = TRUE;
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteIncomingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteIncomingMessage(jid, msg, timestamp, 0);
  hlog_write_message(jid, timestamp, FALSE, msg);
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
}

inline void hk_statuschange(const char *jid, time_t timestamp, 
        enum imstatus status, const char *status_msg)
{
  scr_LogPrint("Buddy status has changed: [%c>%c] <%s>",
          imstatus2char[roster_getstatus(jid)], imstatus2char[status], jid);
  roster_setstatus(jid, status, status_msg);
  buddylist_build();
  scr_DrawRoster();
  hlog_write_status(jid, 0, status, status_msg);
}

inline void hk_mystatuschange(time_t timestamp,
        enum imstatus old_status, enum imstatus new_status)
{
  if (old_status == new_status)
    return;

  scr_LogPrint("Your status has changed:  [%c>%c]",
          imstatus2char[old_status], imstatus2char[new_status]);
  //hlog_write_status(NULL, 0, status);
}


/* External commands */

//  hk_ext_cmd_init()
// Initialize external command variable.
// Can be called with parameter NULL to reset and free memory.
void hk_ext_cmd_init(const char *command)
{
  if (extcommand) {
    g_free(extcommand);
    extcommand = NULL;
  }
  if (command)
    extcommand = g_strdup(command);
}

//  hk_ext_cmd()
// Launch an external command (process) for the given event.
// For now, data should be NULL.
void hk_ext_cmd(const char *jid, guchar type, guchar info, const char *data)
{
  pid_t pid;

  if (!extcommand) return;

  // For now we'll only handle incoming messages
  if (type != 'M') return;
  if (info != 'R') return;

  if ((pid=fork()) == -1) {
    scr_LogPrint("Fork error, cannot launch external command.");
    return;
  }

  // I don't remember what I should do with the parent process...
  if (pid == 0) { // child
    if (execl(extcommand, extcommand, "MSG", "IN", jid, NULL) == -1) {
      // ut_WriteLog("Cannot execute external command.\n");
      exit(1);
    }
  }
}

