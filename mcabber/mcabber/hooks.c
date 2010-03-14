/*
 * hooks.c      -- Hooks layer
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

#include <loudmouth/loudmouth.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "hooks.h"
#include "screen.h"
#include "roster.h"
#include "histolog.h"
#include "hbuf.h"
#include "settings.h"
#include "utils.h"
#include "utf8.h"
#include "commands.h"
#include "main.h"

#ifdef MODULES_ENABLE
#include <glib.h>

typedef struct {
  hk_handler_t handler;
  guint32      flags;
  gpointer     userdata;
} hook_list_data_t;

static GSList *hk_handler_queue = NULL;

void hk_add_handler (hk_handler_t handler, guint32 flags, gpointer userdata)
{
  hook_list_data_t *h = g_new (hook_list_data_t, 1);
  h->handler  = handler;
  h->flags    = flags;
  h->userdata = userdata;
  hk_handler_queue = g_slist_append (hk_handler_queue, h);
}

static gint hk_queue_search_cb (hook_list_data_t *a, hook_list_data_t *b)
{
  if (a->handler == b->handler && a->userdata == b->userdata)
    return 0;
  else
    return 1;
}

void hk_del_handler (hk_handler_t handler, gpointer userdata)
{
  hook_list_data_t h = { handler, 0, userdata };
  GSList *el = g_slist_find_custom (hk_handler_queue, &h, (GCompareFunc) hk_queue_search_cb);
  if (el) {
    g_free (el->data);
    hk_handler_queue = g_slist_delete_link (hk_handler_queue, el);
  }
}
#endif

static char *extcmd;

static const char *COMMAND_ME = "/me ";

void hk_message_in(const char *bjid, const char *resname,
                   time_t timestamp, const char *msg, LmMessageSubType type,
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
  unsigned mucnicklen = 0;
  const char *ename = NULL;

  if (encrypted == ENCRYPTED_PGP)
    message_flags |= HBB_PREFIX_PGPCRYPT;
  else if (encrypted == ENCRYPTED_OTR)
    message_flags |= HBB_PREFIX_OTRCRYPT;

  if (type == LM_MESSAGE_SUB_TYPE_GROUPCHAT) {
    rtype = ROSTER_TYPE_ROOM;
    is_groupchat = TRUE;
    log_muc_conf = settings_opt_get_int("log_muc_conf");
    if (!resname) {
      message_flags = HBB_PREFIX_INFO | HBB_PREFIX_NOFLAG;
      resname = "";
      wmsg = bmsg = g_strdup_printf("~ %s", msg);
    } else {
      wmsg = bmsg = g_strdup_printf("<%s> %s", resname, msg);
      mucnicklen = strlen(resname) + 2;
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
    roster_usr = roster_add_user(bjid, NULL, NULL, rtype, sub_none, -1);
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
        } else if (!settings_opt_get_int("muc_disable_nick_hl")) {
          // We're not the sender.  Can we see our nick?
          const char *msgptr = msg;
          while ((msgptr = strcasestr(msgptr, nick)) != NULL) {
            const char *leftb, *rightb;
            // The message contains our nick.  Let's check it's not
            // in the middle of another word (i.e. preceded/followed
            // immediately by an alphanumeric character or an underscore.
            rightb = msgptr+strlen(nick);
            if (msgptr == msg)
              leftb = NULL;
            else
              leftb = prev_char((char*)msgptr, msg);
            msgptr = next_char((char*)msgptr);
            // Check left boundary
            if (leftb && (iswalnum(get_char(leftb)) || get_char(leftb) == '_'))
              continue;
            // Check right boundary
            if (!iswalnum(get_char(rightb)) && get_char(rightb) != '_')
              message_flags |= HBB_PREFIX_HLIGHT;
          }
        }
      }
    }
  }

  if (type  == LM_MESSAGE_SUB_TYPE_ERROR) {
    message_flags = HBB_PREFIX_ERR | HBB_PREFIX_IN;
    scr_LogPrint(LPRINT_LOGNORM, "Error message received from <%s>", bjid);
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteIncomingMessage() will load the history and we'd
  // have the message twice...
  scr_WriteIncomingMessage(bjid, wmsg, timestamp, message_flags, mucnicklen);

  // We don't log the modified message, but the original one
  if (wmsg == mmsg)
    wmsg = bmsg;

  // - We don't log the message if it is an error message
  // - We don't log the message if it is a private conf. message
  // - We don't log the message if it is groupchat message and the log_muc_conf
  //   option is off (and it is not a history line)
  if (!(message_flags & HBB_PREFIX_ERR) &&
      (!is_room || (is_groupchat && log_muc_conf && !timestamp)))
    hlog_write_message(bjid, timestamp, 0, wmsg);

  if (settings_opt_get_int("events_ignore_active_window") &&
      current_buddy && scr_get_chatmode()) {
    gpointer bud = BUDDATA(current_buddy);
    if (bud) {
      const char *cjid = buddy_getjid(bud);
      if (cjid && !strcasecmp(cjid, bjid))
        active_window = TRUE;
    }
  }

  if (settings_opt_get_int("eventcmd_use_nickname"))
    ename = roster_getname(bjid);

#ifdef MODULES_ENABLE
  {
    GSList *h = hk_handler_queue;
    if (h) {
#if 0
      hk_arg_t *args = g_new (hk_arg_t, 5);
      args[0].name = "hook";
      args[0].value = "hook-message-in";
      args[1].name = "jid";
      args[1].value = bjid;
      args[2].name = "message";
      args[2].value = wmsg;
      args[3].name = "groupchat";
      args[3].value = is_groupchat ? "true" : "false";
      args[4].name = NULL;
      args[4].value = NULL;
#else
      // We can use a const array for keys/static values, so modules
      // can do fast known to them args check by just comparing pointers...
      hk_arg_t args[] = {
        { "hook", "hook-message-in" },
        { "jid", bjid },
        { "message", wmsg },
        { "groupchat", is_groupchat ? "true" : "false" },
        { NULL, NULL },
      };
#endif
      while (h) {
        hook_list_data_t *data = h->data;
        if (data->flags & HOOK_MESSAGE_IN)
          (data->handler) (HOOK_MESSAGE_IN, args, data->userdata);
        h = g_slist_next (h);
      }
    }
  }
#endif

  // External command
  // - We do not call hk_ext_cmd() for history lines in MUC
  // - We do call hk_ext_cmd() for private messages in a room
  // - We do call hk_ext_cmd() for messages to the current window
  if (!active_window && ((is_groupchat && !timestamp) || !is_groupchat))
    hk_ext_cmd(ename ? ename : bjid, (is_groupchat ? 'G' : 'M'), 'R', wmsg);

  // Display the sender in the log window
  if ((!is_groupchat) && !(message_flags & HBB_PREFIX_ERR) &&
      settings_opt_get_int("log_display_sender")) {
    const char *name = roster_getname(bjid);
    if (!name) name = "";
    scr_LogPrint(LPRINT_NORMAL, "Message received from %s <%s/%s>",
                 name, bjid, (resname ? resname : ""));
  }

  // Beep, if enabled:
  // - if it's a private message
  // - if it's a public message and it's highlighted
  if (settings_opt_get_int("beep_on_message")) {
    if ((!is_groupchat && !(message_flags & HBB_PREFIX_ERR)) ||
        (is_groupchat  && (message_flags & HBB_PREFIX_HLIGHT)))
      scr_beep();
  }

  // We need to update the roster if the sender is unknown or
  // if the sender is offline/invisible and a filter is set.
  if (new_guy ||
      (buddy_getstatus(roster_usr->data, NULL) == offline &&
       buddylist_isset_filter()))
  {
    update_roster = TRUE;
  }

  g_free(bmsg);
  g_free(mmsg);
}

//  hk_message_out()
// nick should be set for private messages in a chat room, and null for
// normal messages.
void hk_message_out(const char *bjid, const char *nick,
                    time_t timestamp, const char *msg,
                    guint encrypted, gpointer xep184)
{
  char *wmsg = NULL, *bmsg = NULL, *mmsg = NULL;
  guint cryptflag = 0;

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
      char *myid = jid_get_username(settings_opt_get("jid"));
      if (myid) {
        wmsg = mmsg = g_strdup_printf("*%s %s", myid, msg+4);
        g_free(myid);
      }
    }
  }

  // Note: the hlog_write should not be called first, because in some
  // cases scr_WriteOutgoingMessage() will load the history and we'd
  // have the message twice...
  if (encrypted == ENCRYPTED_PGP)
    cryptflag = HBB_PREFIX_PGPCRYPT;
  else if (encrypted == ENCRYPTED_OTR)
    cryptflag = HBB_PREFIX_OTRCRYPT;
  scr_WriteOutgoingMessage(bjid, wmsg, cryptflag, xep184);

  // We don't log private messages
  if (!nick)
    hlog_write_message(bjid, timestamp, 1, msg);

#ifdef MODULES_ENABLE
  {
    GSList *h = hk_handler_queue;
    if (h) {
      hk_arg_t args[] = {
        { "hook", "hook-message-out" },
        { "jid", bjid },
        { "message", wmsg },
        { NULL, NULL },
      };
      while (h) {
        hook_list_data_t *data = h->data;
        if (data->flags & HOOK_MESSAGE_OUT)
          (data->handler) (HOOK_MESSAGE_OUT, args, data->userdata);
        h = g_slist_next (h);
      }
    }
  }
#endif

  // External command
  hk_ext_cmd(bjid, 'M', 'S', NULL);

  g_free(bmsg);
  g_free(mmsg);
}

void hk_statuschange(const char *bjid, const char *resname, gchar prio,
                     time_t timestamp, enum imstatus status,
                     const char *status_msg)
{
  int st_in_buf;
  enum imstatus oldstat;
  char *bn;
  char *logsmsg;
  const char *rn = (resname ? resname : "");
  const char *ename = NULL;

  if (settings_opt_get_int("eventcmd_use_nickname"))
    ename = roster_getname(bjid);

  oldstat = roster_getstatus(bjid, resname);

  st_in_buf = settings_opt_get_int("show_status_in_buffer");

  if (settings_opt_get_int("log_display_presence")) {
    int buddy_format = settings_opt_get_int("buddy_format");
    bn = NULL;
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

    if (!bn)
      bn = g_strdup_printf("<%s/%s>", bjid, rn);

    logsmsg = g_strdup(status_msg ? status_msg : "");
    replace_nl_with_dots(logsmsg);

    scr_LogPrint(LPRINT_LOGNORM, "Buddy status has changed: [%c>%c] %s %s",
                 imstatus2char[oldstat], imstatus2char[status], bn, logsmsg);
    g_free(logsmsg);
    g_free(bn);
  }

  if (st_in_buf == 2 ||
      (st_in_buf == 1 && (status == offline || oldstat == offline))) {
    // Write the status change in the buddy's buffer, only if it already exists
    if (scr_buddy_buffer_exists(bjid)) {
      bn = g_strdup_printf("Buddy status has changed: [%c>%c] %s",
                           imstatus2char[oldstat], imstatus2char[status],
                           ((status_msg) ? status_msg : ""));
      scr_WriteIncomingMessage(bjid, bn, timestamp,
                               HBB_PREFIX_INFO|HBB_PREFIX_NOFLAG, 0);
      g_free(bn);
    }
  }

  roster_setstatus(bjid, rn, prio, status, status_msg, timestamp,
                   role_none, affil_none, NULL);
  buddylist_build();
  scr_draw_roster();
  hlog_write_status(bjid, timestamp, status, status_msg);

#ifdef MODULES_ENABLE
  {
    GSList *h = hk_handler_queue;
    if (h) {
      char os[2] = " \0";
      char ns[2] = " \0";
      hk_arg_t args[] = {
        { "hook", "hook-status-change" },
        { "jid", bjid },
        { "resource", rn },
        { "old_status", os },
        { "new_status", ns },
        { "message", status_msg ? status_msg : "" },
        { NULL, NULL },
      };
      os[0] = imstatus2char[oldstat];
      ns[0] = imstatus2char[status];
      while (h) {
        hook_list_data_t *data = h->data;
        if (data->flags & HOOK_STATUS_CHANGE)
          (data->handler) (HOOK_STATUS_CHANGE, args, data->userdata);
        h = g_slist_next (h);
      }
    }
  }
#endif

  // External command
  hk_ext_cmd(ename ? ename : bjid, 'S', imstatus2char[status], NULL);
}

void hk_mystatuschange(time_t timestamp, enum imstatus old_status,
                              enum imstatus new_status, const char *msg)
{
  scr_LogPrint(LPRINT_LOGNORM, "Your status has been set: [%c>%c] %s",
               imstatus2char[old_status], imstatus2char[new_status],
               (msg ? msg : ""));

#ifdef MODULES_ENABLE
  {
    GSList *h = hk_handler_queue;
    if (h) {
      char ns[2] = " \0";
      hk_arg_t args[] = {
        { "hook", "hook-my-status-change" },
        { "new_status", ns },
        { "message", msg ? msg : "" },
        { NULL, NULL },
      };
      ns[0] = imstatus2char[new_status];
      while (h) {
        hook_list_data_t *data = h->data;
        if (data->flags & HOOK_MY_STATUS_CHANGE)
          (data->handler) (HOOK_MY_STATUS_CHANGE, args, data->userdata);
        h = g_slist_next (h);
      }
    }
  }
#endif

  //hlog_write_status(NULL, 0, status);
}

void hk_postconnect(void)
{
  const char *hook_command;
  char *cmdline;

#ifdef MODULES_ENABLE
  {
    GSList *h = hk_handler_queue;
    if (h) {
      hk_arg_t args[] = {
        { "hook", "hook-post-connect" },
        { NULL, NULL },
      };
      while (h) {
        hook_list_data_t *data = h->data;
        if (data->flags & HOOK_POST_CONNECT)
          (data->handler) (HOOK_POST_CONNECT, args, data->userdata);
        h = g_slist_next (h);
      }
    }
  }
#endif

  hook_command = settings_opt_get("hook-post-connect");
  if (!hook_command)
    return;

  scr_LogPrint(LPRINT_LOGNORM, "Running hook-post-connect...");

  cmdline = from_utf8(hook_command);
  if (process_command(cmdline, TRUE) == 255)
    mcabber_set_terminate_ui();

  g_free(cmdline);
}

void hk_predisconnect(void)
{
  const char *hook_command;
  char *cmdline;

#ifdef MODULES_ENABLE
  {
    GSList *h = hk_handler_queue;
    if (h) {
      hk_arg_t args[] = {
        { "hook", "hook-pre-disconnect" },
        { NULL, NULL },
      };
      while (h) {
        hook_list_data_t *data = h->data;
        if (data->flags & HOOK_PRE_DISCONNECT)
          (data->handler) (HOOK_PRE_DISCONNECT, args, data->userdata);
        h = g_slist_next (h);
      }
    }
  }
#endif

  hook_command = settings_opt_get("hook-pre-disconnect");
  if (!hook_command)
    return;

  scr_LogPrint(LPRINT_LOGNORM, "Running hook-pre-disconnect...");

  cmdline = from_utf8(hook_command);
  if (process_command(cmdline, TRUE) == 255)
    mcabber_set_terminate_ui();

  g_free(cmdline);
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
  char unread_str[16];

  if (!extcmd) return;

  // Prepare arg_* (external command parameters)
  switch (type) {
    case 'M': /* Normal message */
        arg_type = "MSG";
        if (info == 'R')
          arg_info = "IN";
        else if (info == 'S')
          arg_info = "OUT";
        break;
    case 'G': /* Groupchat message */
        arg_type = "MSG";
        arg_info = "MUC";
        break;
    case 'S': /* Status change */
        arg_type = "STATUS";
        if (strchr(imstatus2char, tolower(info))) {
          status_str[0] = toupper(info);
          status_str[1] = 0;
          arg_info = status_str;
        }
        break;
    case 'U': /* Unread buffer count */
        arg_type = "UNREAD";
        g_snprintf(unread_str, sizeof unread_str, "%d", info);
        arg_info = unread_str;  /* number of remaining unread bjids */
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
      size_t data_locale_len = strlen(data_locale);
      ssize_t a = write(fd, data_locale, data_locale_len);
      ssize_t b = write(fd, "\n", 1);
      if ((size_t)a != data_locale_len || b != 1) {
        g_free(datafname);
        datafname = NULL;
        scr_LogPrint(LPRINT_LOGNORM,
                     "Unable to write to temp file for external command.");
      }
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
    if (execl(extcmd, extcmd, arg_type, arg_info, bjid, arg_data,
              (char *)NULL) == -1) {
      // scr_LogPrint(LPRINT_LOGNORM, "Cannot execute external command.");
      exit(1);
    }
  }
  g_free(datafname);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
