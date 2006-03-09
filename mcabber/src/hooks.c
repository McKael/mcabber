/*
 * hooks.c      -- Hooks layer
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

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "hooks.h"
#include "screen.h"
#include "roster.h"
#include "histolog.h"
#include "hbuf.h"
#include "settings.h"
#include "utils.h"

static char *extcmd;

inline void hk_message_in(const char *jid, const char *resname,
                          time_t timestamp, const char *msg, const char *type)
{
  int new_guy = FALSE;
  int is_groupchat = FALSE; // groupchat message
  int is_room = FALSE;      // window is a room window
  int log_muc_conf = FALSE;
  int message_flags = 0;
  guint rtype = ROSTER_TYPE_USER;
  char *wmsg = NULL, *bmsg = NULL, *mmsg = NULL;
  GSList *roster_usr;

  if (type && !strcmp(type, "groupchat")) {
    rtype = ROSTER_TYPE_ROOM;
    is_groupchat = TRUE;
    log_muc_conf = settings_opt_get_int("log_muc_conf");
    if (!resname) {
      message_flags = HBB_PREFIX_INFO | HBB_PREFIX_NOFLAG;
      resname = "";
      bmsg = g_strdup_printf("~ %s", msg);
    } else {
      bmsg = g_strdup_printf("<%s> %s", resname, msg);
    }
    wmsg = bmsg;
    if (!strncmp(msg, "/me ", 4))
      wmsg = mmsg = g_strdup_printf("*%s %s", resname, msg+4);
  } else {
    if (!strncmp(msg, "/me ", 4))
      wmsg = mmsg = g_strdup_printf("*%s %s", jid, msg+4);
    else
      wmsg = (char*) msg;
  }

  // If this user isn't in the roster, we add it
  roster_usr = roster_find(jid, jidsearch, 0);
  if (!roster_usr) {
    new_guy = TRUE;
    roster_usr = roster_add_user(jid, NULL, NULL, rtype, sub_none);
    if (!roster_usr) { // Shouldn't happen...
      scr_LogPrint(LPRINT_LOGNORM, "ERROR: unable to add buddy!");
      if (bmsg) g_free(bmsg);
      if (mmsg) g_free(mmsg);
      return;
    }
  } else if (is_groupchat) {
    // Make sure the type is ROOM
    buddy_settype(roster_usr->data, ROSTER_TYPE_ROOM);
  }

  is_room = !!(buddy_gettype(roster_usr->data) & ROSTER_TYPE_ROOM);

  if (is_room) {
    if (!is_groupchat) {
      // This is a private message from a room participant
      if (!resname) {
        resname = "";
        wmsg = bmsg = g_strdup(msg);
      } else {
        wmsg = bmsg = g_strdup_printf("PRIV#<%s> %s", resname, msg);
        if (!strncmp(msg, "/me ", 4))
          wmsg = mmsg = g_strdup_printf("PRIV#*%s %s", resname, msg+4);
      }
      /*message_flags |= HBB_PREFIX_HLIGHT;*/
    } else {
      // This is a regular chatroom message.
      // Let's see if we are the message sender, in which case we'll
      // highlight it.
      const char *nick = buddy_getnickname(roster_usr->data);
      if (resname && nick && !strcmp(resname, nick))
        message_flags |= HBB_PREFIX_HLIGHT;
    }
  }

  if (type && !strcmp(type, "error")) {
    message_flags = HBB_PREFIX_ERR | HBB_PREFIX_IN;
    scr_LogPrint(LPRINT_LOGNORM, "Error message received from <%s>", jid);
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteIncomingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteIncomingMessage(jid, wmsg, timestamp, message_flags);

  // We don't log the modified message, but the original one
  if (wmsg == mmsg)
    wmsg = bmsg;

  // - We don't log the message if it is an error message
  // - We don't log the message if it is a private conf. message
  // - We don't log the message if it is groupchat message and the log_muc_conf
  //   option is off (and it is not a history line)
  if (!(message_flags & HBB_PREFIX_ERR) &&
      (!is_room || (is_groupchat && log_muc_conf && !timestamp)))
    hlog_write_message(jid, timestamp, FALSE, wmsg);

  // External command
  // - We do not call hk_ext_cmd() for history lines in MUC
  // - We do call hk_ext_cmd() for private messages in a room
  if ((is_groupchat && !timestamp) || !is_groupchat)
    hk_ext_cmd(jid, (is_groupchat ? 'G' : 'M'), 'R', wmsg);

  // Beep, if enabled
  if (settings_opt_get_int("beep_on_message"))
    scr_Beep();

  // We need to rebuild the list if the sender is unknown or
  // if the sender is offline/invisible and hide_offline_buddies is set
  if (new_guy ||
      (buddy_getstatus(roster_usr->data, NULL) == offline &&
       buddylist_get_hide_offline_buddies()))
  {
    buddylist_build();
    update_roster = TRUE;
  }

  if (bmsg) g_free(bmsg);
  if (mmsg) g_free(mmsg);
}

//  hk_message_out()
// nick should be set for private messages in a chat room, and null for
// normal messages.
inline void hk_message_out(const char *jid, const char *nick,
                           time_t timestamp, const char *msg)
{
  char *wmsg = NULL, *bmsg = NULL, *mmsg = NULL;;

  if (nick) {
    wmsg = bmsg = g_strdup_printf("PRIV#<%s> %s", nick, msg);
  } else {
    wmsg = (char*)msg;
    if (!strncmp(msg, "/me ", 4)) {
      const char *myid = settings_opt_get("username");
      if (myid)
        wmsg = mmsg = g_strdup_printf("*%s %s", settings_opt_get("username"),
                                      msg+4);
    }
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteOutgoingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteOutgoingMessage(jid, wmsg);

  // We don't log private messages
  if (!nick) hlog_write_message(jid, timestamp, TRUE, msg);

  // External command
  hk_ext_cmd(jid, 'M', 'S', NULL);

  if (bmsg) g_free(bmsg);
  if (mmsg) g_free(mmsg);
}

inline void hk_statuschange(const char *jid, const char *resname, gchar prio,
                            time_t timestamp, enum imstatus status,
                            const char *status_msg)
{
  int buddy_format;
  int st_in_buf;
  enum imstatus oldstat;
  char *bn = NULL;
  char *logsmsg;
  const char *rn = (resname ? resname : "");

  st_in_buf = settings_opt_get_int("show_status_in_buffer");
  buddy_format = settings_opt_get_int("buddy_format");
  if (buddy_format) {
    const char *name = roster_getname(jid);
    if (name && strcmp(name, jid)) {
      if (buddy_format == 1)
        bn = g_strdup_printf("%s <%s/%s>", name, jid, rn);
      else if (buddy_format == 2)
        bn = g_strdup_printf("%s/%s", name, rn);
      else if (buddy_format == 3)
        bn = g_strdup_printf("%s", name);
    }
  }

  if (!bn) {
    bn = g_strdup_printf("<%s/%s>", jid, rn);
  }

  logsmsg = g_strdup(status_msg ? status_msg : "");
  replace_nl_with_dots(logsmsg);

  oldstat = roster_getstatus(jid, resname);
  scr_LogPrint(LPRINT_LOGNORM, "Buddy status has changed: [%c>%c] %s %s",
               imstatus2char[oldstat], imstatus2char[status], bn, logsmsg);
  g_free(logsmsg);
  g_free(bn);

  if (st_in_buf == 2 ||
      (st_in_buf == 1 && (status == offline || oldstat == offline))) {
    // Write the status change in the buddy's buffer, only if it already exists
    if (scr_BuddyBufferExists(jid)) {
      bn = g_strdup_printf("Buddy status has changed: [%c>%c] %s",
                           imstatus2char[oldstat], imstatus2char[status],
                           ((status_msg) ? status_msg : ""));
      scr_WriteIncomingMessage(jid, bn, 0, HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG);
      g_free(bn);
    }
  }

  roster_setstatus(jid, rn, prio, status, status_msg, timestamp,
                   role_none, affil_none, NULL);
  buddylist_build();
  scr_DrawRoster();
  hlog_write_status(jid, 0, status, status_msg);
  // External command
  hk_ext_cmd(jid, 'S', imstatus2char[status], NULL);
}

inline void hk_mystatuschange(time_t timestamp, enum imstatus old_status,
                              enum imstatus new_status, const char *msg)
{
  scr_LogPrint(LPRINT_LOGNORM, "Your status has been set: [%c>%c] %s",
               imstatus2char[old_status], imstatus2char[new_status],
               (msg ? msg : ""));
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
  char *datafname = NULL;

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
    case 'G':
        arg_type = "MSG";
        arg_info = "MUC";
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

  if (strchr("MG", type) && data && settings_opt_get_int("event_log_files")) {
    int fd;
    const char *prefix;
    prefix = settings_opt_get("event_log_dir");
    if (!prefix)
      prefix = ut_get_tmpdir();
    datafname = g_strdup_printf("%s/mcabber-%d.XXXXXX", prefix, getpid());
    // XXX Some old systems may require us to set umask first.
    fd = mkstemp(datafname);
    if (fd == -1) {
      g_free(datafname);
      datafname = NULL;
      scr_LogPrint(LPRINT_LOGNORM,
                   "Unable to create temp file for external command.");
    }
    write(fd, data, strlen(data));
    write(fd, "\n", 1);
    close(fd);
    arg_data = datafname;
  }

  if ((pid=fork()) == -1) {
    scr_LogPrint(LPRINT_LOGNORM, "Fork error, cannot launch external command.");
    if (datafname)
      g_free(datafname);
    return;
  }

  if (pid == 0) { // child
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if (execl(extcmd, extcmd, arg_type, arg_info, jid, arg_data, NULL) == -1) {
      // scr_LogPrint(LPRINT_LOGNORM, "Cannot execute external command.");
      exit(1);
    }
  }
  if (datafname)
    g_free(datafname);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
