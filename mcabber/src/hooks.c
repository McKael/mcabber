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
#include "utf8.h"

static char *extcmd;

static const char *COMMAND_ME = "/me ";

inline void hk_message_in(const char *bjid, const char *resname,
                          time_t timestamp, const char *msg, const char *type,
                          guint encrypted)
{
  int new_guy = FALSE;
  int is_groupchat = FALSE; // groupchat message
  int is_room = FALSE;      // window is a room window
  int log_muc_conf = FALSE;
  int active_window = FALSE;
  int message_flags = 0;
  guint rtype = ROSTER_TYPE_USER;
  char *wmsg = NULL, *bmsg = NULL, *mmsg = NULL;
  GSList *roster_usr;

  if (encrypted)
    message_flags |= HBB_PREFIX_PGPCRYPT;

  if (type && !strcmp(type, "groupchat")) {
    rtype = ROSTER_TYPE_ROOM;
    is_groupchat = TRUE;
    log_muc_conf = settings_opt_get_int("log_muc_conf");
    if (!resname) {
      message_flags = HBB_PREFIX_INFO | HBB_PREFIX_NOFLAG;
      resname = "";
      wmsg = bmsg = g_strdup_printf("~ %s", msg);
    } else {
      wmsg = bmsg = g_strdup_printf("<%s> %s", resname, msg);
      if (!strncmp(msg, COMMAND_ME, strlen(COMMAND_ME)))
        wmsg = mmsg = g_strdup_printf("*%s %s", resname, msg+4);
    }
  } else {
    bmsg = g_strdup(msg);
    if (!strncmp(msg, COMMAND_ME, strlen(COMMAND_ME))) {
      gchar *shortid = g_strdup(bjid);
      if (settings_opt_get_int("buddy_me_fulljid") == FALSE) {
        gchar *p = strchr(shortid, '@'); // Truncate the jid
        if (p)
          *p = '\0';
      }
      wmsg = mmsg = g_strdup_printf("*%s %s", shortid, msg+4);
      g_free(shortid);
    } else
      wmsg = (char*) msg;
  }

  // If this user isn't in the roster, we add it
  roster_usr = roster_find(bjid, jidsearch, 0);
  if (!roster_usr) {
    new_guy = TRUE;
    roster_usr = roster_add_user(bjid, NULL, NULL, rtype, sub_none);
    if (!roster_usr) { // Shouldn't happen...
      scr_LogPrint(LPRINT_LOGNORM, "ERROR: unable to add buddy!");
      g_free(bmsg);
      g_free(mmsg);
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
      g_free(bmsg);
      if (!resname) {
        resname = "";
        wmsg = bmsg = g_strdup(msg);
      } else {
        wmsg = bmsg = g_strdup_printf("PRIV#<%s> %s", resname, msg);
        if (!strncmp(msg, COMMAND_ME, strlen(COMMAND_ME))) {
          g_free(mmsg);
          wmsg = mmsg = g_strdup_printf("PRIV#*%s %s", resname, msg+4);
        }
      }
      message_flags |= HBB_PREFIX_HLIGHT;
    } else {
      // This is a regular chatroom message.
      const char *nick = buddy_getnickname(roster_usr->data);

      if (nick) {
        // Let's see if we are the message sender, in which case we'll
        // highlight it.
        if (resname && !strcmp(resname, nick)) {
          message_flags |= HBB_PREFIX_HLIGHT_OUT;
        } else {
          // We're not the sender.  Can we see our nick?
          if (startswith(msg, nick, TRUE)) {
            // The message starts with our nick.  Let's check it's not
            // followed immediately by an alphnumeric character.
            if (!iswalnum(get_char(msg+strlen(nick))))
              message_flags |= HBB_PREFIX_HLIGHT;
          }
          // We could do a more global check...
        }
      }
    }
  }

  if (type && !strcmp(type, "error")) {
    message_flags = HBB_PREFIX_ERR | HBB_PREFIX_IN;
    scr_LogPrint(LPRINT_LOGNORM, "Error message received from <%s>", bjid);
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteIncomingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteIncomingMessage(bjid, wmsg, timestamp, message_flags);

  // We don't log the modified message, but the original one
  if (wmsg == mmsg)
    wmsg = bmsg;

  // - We don't log the message if it is an error message
  // - We don't log the message if it is a private conf. message
  // - We don't log the message if it is groupchat message and the log_muc_conf
  //   option is off (and it is not a history line)
  if (!(message_flags & HBB_PREFIX_ERR) &&
      (!is_room || (is_groupchat && log_muc_conf && !timestamp)))
    hlog_write_message(bjid, timestamp, FALSE, wmsg);

  if (settings_opt_get_int("events_ignore_active_window") &&
      current_buddy && scr_get_chatmode()) {
    gpointer bud = BUDDATA(current_buddy);
    if (bud) {
      const char *cjid = buddy_getjid(bud);
      if (cjid && !strcasecmp(cjid, bjid))
        active_window = TRUE;
    }
  }

  // External command
  // - We do not call hk_ext_cmd() for history lines in MUC
  // - We do call hk_ext_cmd() for private messages in a room
  // - We do call hk_ext_cmd() for messages to the current window
  if (!active_window && ((is_groupchat && !timestamp) || !is_groupchat))
    hk_ext_cmd(bjid, (is_groupchat ? 'G' : 'M'), 'R', wmsg);

  // Display the sender in the log window
  if ((!is_groupchat) && !(message_flags & HBB_PREFIX_ERR) &&
      settings_opt_get_int("log_display_sender")) {
    const char *name = roster_getname(bjid);
    if (!name) name = "";
    scr_LogPrint(LPRINT_NORMAL, "Message received from %s <%s/%s>",
                 name, bjid, (resname ? resname : ""));
  }

  // Beep, if enabled
  if ((!is_groupchat) && !(message_flags & HBB_PREFIX_ERR) &&
      settings_opt_get_int("beep_on_message")) {
    scr_Beep();
  }

  // We need to update the roster if the sender is unknown or
  // if the sender is offline/invisible and hide_offline_buddies is set
  if (new_guy ||
      (buddy_getstatus(roster_usr->data, NULL) == offline &&
       buddylist_get_hide_offline_buddies()))
  {
    update_roster = TRUE;
  }

  g_free(bmsg);
  g_free(mmsg);
}

//  hk_message_out()
// nick should be set for private messages in a chat room, and null for
// normal messages.
inline void hk_message_out(const char *bjid, const char *nick,
                           time_t timestamp, const char *msg, guint encrypted)
{
  char *wmsg = NULL, *bmsg = NULL, *mmsg = NULL;

  if (nick) {
    wmsg = bmsg = g_strdup_printf("PRIV#<%s> %s", nick, msg);
    if (!strncmp(msg, COMMAND_ME, strlen(COMMAND_ME))) {
      const char *mynick = roster_getnickname(bjid);
      wmsg = mmsg = g_strdup_printf("PRIV#<%s> *%s %s", nick,
                                    (mynick ? mynick : "me"), msg+4);
    }
  } else {
    wmsg = (char*)msg;
    if (!strncmp(msg, COMMAND_ME, strlen(COMMAND_ME))) {
      const char *myid = settings_opt_get("username");
      if (myid)
        wmsg = mmsg = g_strdup_printf("*%s %s", settings_opt_get("username"),
                                      msg+4);
    }
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteOutgoingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteOutgoingMessage(bjid, wmsg, (encrypted ? HBB_PREFIX_PGPCRYPT : 0));

  // We don't log private messages
  if (!nick)
    hlog_write_message(bjid, timestamp, TRUE, msg);

  // External command
  hk_ext_cmd(bjid, 'M', 'S', NULL);

  g_free(bmsg);
  g_free(mmsg);
}

inline void hk_statuschange(const char *bjid, const char *resname, gchar prio,
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
    const char *name = roster_getname(bjid);
    if (name && strcmp(name, bjid)) {
      if (buddy_format == 1)
        bn = g_strdup_printf("%s <%s/%s>", name, bjid, rn);
      else if (buddy_format == 2)
        bn = g_strdup_printf("%s/%s", name, rn);
      else if (buddy_format == 3)
        bn = g_strdup_printf("%s", name);
    }
  }

  if (!bn) {
    bn = g_strdup_printf("<%s/%s>", bjid, rn);
  }

  logsmsg = g_strdup(status_msg ? status_msg : "");
  replace_nl_with_dots(logsmsg);

  oldstat = roster_getstatus(bjid, resname);
  scr_LogPrint(LPRINT_LOGNORM, "Buddy status has changed: [%c>%c] %s %s",
               imstatus2char[oldstat], imstatus2char[status], bn, logsmsg);
  g_free(logsmsg);
  g_free(bn);

  if (st_in_buf == 2 ||
      (st_in_buf == 1 && (status == offline || oldstat == offline))) {
    // Write the status change in the buddy's buffer, only if it already exists
    if (scr_BuddyBufferExists(bjid)) {
      bn = g_strdup_printf("Buddy status has changed: [%c>%c] %s",
                           imstatus2char[oldstat], imstatus2char[status],
                           ((status_msg) ? status_msg : ""));
      scr_WriteIncomingMessage(bjid, bn, timestamp,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG);
      g_free(bn);
    }
  }

  roster_setstatus(bjid, rn, prio, status, status_msg, timestamp,
                   role_none, affil_none, NULL);
  buddylist_build();
  scr_DrawRoster();
  hlog_write_status(bjid, timestamp, status, status_msg);
  // External command
  hk_ext_cmd(bjid, 'S', imstatus2char[status], NULL);
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
    extcmd = expand_filename(command);
}

//  hk_ext_cmd()
// Launch an external command (process) for the given event.
// For now, data should be NULL.
void hk_ext_cmd(const char *bjid, guchar type, guchar info, const char *data)
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
    char *prefix_xp = NULL;
    char *data_locale;

    data_locale = from_utf8(data);
    prefix = settings_opt_get("event_log_dir");
    if (prefix)
      prefix = prefix_xp = expand_filename(prefix);
    else
      prefix = ut_get_tmpdir();
    datafname = g_strdup_printf("%s/mcabber-%d.XXXXXX", prefix, getpid());
    g_free(prefix_xp);

    // XXX Some old systems may require us to set umask first.
    fd = mkstemp(datafname);
    if (fd == -1) {
      g_free(datafname);
      datafname = NULL;
      scr_LogPrint(LPRINT_LOGNORM,
                   "Unable to create temp file for external command.");
    } else {
      write(fd, data_locale, strlen(data_locale));
      write(fd, "\n", 1);
      close(fd);
      arg_data = datafname;
    }
    g_free(data_locale);
  }

  if ((pid=fork()) == -1) {
    scr_LogPrint(LPRINT_LOGNORM, "Fork error, cannot launch external command.");
    g_free(datafname);
    return;
  }

  if (pid == 0) { // child
    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if (execl(extcmd, extcmd, arg_type, arg_info, bjid, arg_data, NULL) == -1) {
      // scr_LogPrint(LPRINT_LOGNORM, "Cannot execute external command.");
      exit(1);
    }
  }
  g_free(datafname);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
