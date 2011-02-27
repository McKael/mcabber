/*
 * commands.c   -- user commands handling
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

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>

#include "config.h"
#include "commands.h"
#include "help.h"
#include "roster.h"
#include "screen.h"
#include "compl.h"
#include "hooks.h"
#include "hbuf.h"
#include "utils.h"
#include "settings.h"
#include "events.h"
#include "otr.h"
#include "utf8.h"
#include "xmpp.h"
#include "main.h"

#define IMSTATUS_AWAY           "away"
#define IMSTATUS_ONLINE         "online"
#define IMSTATUS_OFFLINE        "offline"
#define IMSTATUS_FREE4CHAT      "free"
#define IMSTATUS_INVISIBLE      "invisible"
#define IMSTATUS_AVAILABLE      "avail"
#define IMSTATUS_NOTAVAILABLE   "notavail"
#define IMSTATUS_DONOTDISTURB   "dnd"

// Return value container for the following functions
static int retval_for_cmds;

// Commands callbacks
static void do_roster(char *arg);
static void do_status(char *arg);
static void do_status_to(char *arg);
static void do_add(char *arg);
static void do_del(char *arg);
static void do_group(char *arg);
static void do_say(char *arg);
static void do_msay(char *arg);
static void do_say_to(char *arg);
static void do_buffer(char *arg);
static void do_clear(char *arg);
static void do_info(char *arg);
static void do_rename(char *arg);
static void do_move(char *arg);
static void do_set(char *arg);
static void do_alias(char *arg);
static void do_bind(char *arg);
static void do_connect(char *arg);
static void do_disconnect(char *arg);
static void do_rawxml(char *arg);
static void do_room(char *arg);
static void do_authorization(char *arg);
static void do_version(char *arg);
static void do_request(char *arg);
static void do_event(char *arg);
static void do_help(char *arg);
static void do_pgp(char *arg);
static void do_iline(char *arg);
static void do_screen_refresh(char *arg);
static void do_chat_disable(char *arg);
static void do_source(char *arg);
static void do_color(char *arg);
static void do_otr(char *arg);
static void do_otrpolicy(char *arg);
static void do_echo(char *arg);
static void do_module(char *arg);

// Global variable for the commands list
static GSList *Commands;

#ifdef MODULES_ENABLE
#include "modules.h"

gpointer cmd_del(const char *name)
{
  GSList *sl_cmd;
  for (sl_cmd = Commands; sl_cmd; sl_cmd = sl_cmd->next) {
    cmd *command = (cmd *) sl_cmd->data;
    if (!strcmp (command->name, name)) {
      gpointer userdata = command->userdata;
      Commands = g_slist_delete_link(Commands, sl_cmd);
      compl_del_category_word(COMPL_CMD, command->name);
      g_free(command);
      return userdata;
    }
  }
  return NULL;
}
#endif

//  cmd_add()
// Adds a command to the commands list and to the CMD completion list
void cmd_add(const char *name, const char *help, guint flags_row1,
             guint flags_row2, void (*f)(char*), gpointer userdata)
{
  cmd *n_cmd = g_new0(cmd, 1);
  strncpy(n_cmd->name, name, 32-1);
  n_cmd->help = help;
  n_cmd->completion_flags[0] = flags_row1;
  n_cmd->completion_flags[1] = flags_row2;
  n_cmd->func = f;
  n_cmd->userdata = userdata;
  Commands = g_slist_prepend(Commands, n_cmd);
  // Add to completion CMD category
  compl_add_category_word(COMPL_CMD, name);
}

//  cmd_init()
// Commands table initialization
// !!!
// After changing commands names and it arguments names here, you must change
// ones in init_bindings()!
//
void cmd_init(void)
{
  cmd_add("add", "Add a jabber user", COMPL_JID, 0, &do_add, NULL);
  cmd_add("alias", "Add an alias", 0, 0, &do_alias, NULL);
  cmd_add("authorization", "Manage subscription authorizations",
          COMPL_AUTH, COMPL_JID, &do_authorization, NULL);
  cmd_add("bind", "Add an key binding", 0, 0, &do_bind, NULL);
  cmd_add("buffer", "Manipulate current buddy's buffer (chat window)",
          COMPL_BUFFER, 0, &do_buffer, NULL);
  cmd_add("chat_disable", "Disable chat mode", 0, 0, &do_chat_disable, NULL);
  cmd_add("clear", "Clear the dialog window", 0, 0, &do_clear, NULL);
  cmd_add("color", "Set coloring options", COMPL_COLOR, 0, &do_color, NULL);
  cmd_add("connect", "Connect to the server", 0, 0, &do_connect, NULL);
  cmd_add("del", "Delete the current buddy", 0, 0, &do_del, NULL);
  cmd_add("disconnect", "Disconnect from server", 0, 0, &do_disconnect, NULL);
  cmd_add("echo", "Display a string in the log window", 0, 0, &do_echo, NULL);
  cmd_add("event", "Process an event", COMPL_EVENTSID, COMPL_EVENTS, &do_event,
          NULL);
  cmd_add("group", "Change group display settings",
          COMPL_GROUP, COMPL_GROUPNAME, &do_group, NULL);
  cmd_add("help", "Display some help", COMPL_CMD, 0, &do_help, NULL);
  cmd_add("iline", "Manipulate input buffer", 0, 0, &do_iline, NULL);
  cmd_add("info", "Show basic info on current buddy", 0, 0, &do_info, NULL);
  cmd_add("module", "Manipulations with modules", COMPL_MODULE, 0, &do_module,
          NULL);
  cmd_add("move", "Move the current buddy to another group", COMPL_GROUPNAME,
          0, &do_move, NULL);
  cmd_add("msay", "Send a multi-lines message to the selected buddy",
          COMPL_MULTILINE, 0, &do_msay, NULL);
  cmd_add("otr", "Manage OTR settings", COMPL_OTR, COMPL_JID, &do_otr, NULL);
  cmd_add("otrpolicy", "Manage OTR policies", COMPL_JID, COMPL_OTRPOLICY,
          &do_otrpolicy, NULL);
  cmd_add("pgp", "Manage PGP settings", COMPL_PGP, COMPL_JID, &do_pgp, NULL);
  cmd_add("quit", "Exit the software", 0, 0, NULL, NULL);
  cmd_add("rawxml", "Send a raw XML string", 0, 0, &do_rawxml, NULL);
  cmd_add("rename", "Rename the current buddy", 0, 0, &do_rename, NULL);
  cmd_add("request", "Send a Jabber IQ request", COMPL_REQUEST, COMPL_JID,
          &do_request, NULL);
  cmd_add("room", "MUC actions command", COMPL_ROOM, 0, &do_room, NULL);
  cmd_add("roster", "Manipulate the roster/buddylist", COMPL_ROSTER, 0,
          &do_roster, NULL);
  cmd_add("say", "Say something to the selected buddy", 0, 0, &do_say, NULL);
  cmd_add("say_to", "Say something to a specific buddy", COMPL_JID, 0,
          &do_say_to, NULL);
  cmd_add("screen_refresh", "Redraw mcabber screen", 0, 0, &do_screen_refresh,
          NULL);
  cmd_add("set", "Set/query an option value", 0, 0, &do_set, NULL);
  cmd_add("source", "Read a configuration file", 0, 0, &do_source, NULL);
  cmd_add("status", "Show or set your status", COMPL_STATUS, 0, &do_status,
          NULL);
  cmd_add("status_to", "Show or set your status for one recipient",
          COMPL_JID, COMPL_STATUS, &do_status_to, NULL);
  cmd_add("version", "Show mcabber version", 0, 0, &do_version, NULL);

  // Status category
  compl_add_category_word(COMPL_STATUS, "online");
  compl_add_category_word(COMPL_STATUS, "avail");
  compl_add_category_word(COMPL_STATUS, "invisible");
  compl_add_category_word(COMPL_STATUS, "free");
  compl_add_category_word(COMPL_STATUS, "dnd");
  compl_add_category_word(COMPL_STATUS, "notavail");
  compl_add_category_word(COMPL_STATUS, "away");
  compl_add_category_word(COMPL_STATUS, "offline");
  compl_add_category_word(COMPL_STATUS, "message");

  // Roster category
  compl_add_category_word(COMPL_ROSTER, "bottom");
  compl_add_category_word(COMPL_ROSTER, "top");
  compl_add_category_word(COMPL_ROSTER, "up");
  compl_add_category_word(COMPL_ROSTER, "down");
  compl_add_category_word(COMPL_ROSTER, "group_prev");
  compl_add_category_word(COMPL_ROSTER, "group_next");
  compl_add_category_word(COMPL_ROSTER, "hide");
  compl_add_category_word(COMPL_ROSTER, "show");
  compl_add_category_word(COMPL_ROSTER, "toggle");
  compl_add_category_word(COMPL_ROSTER, "display");
  compl_add_category_word(COMPL_ROSTER, "hide_offline");
  compl_add_category_word(COMPL_ROSTER, "show_offline");
  compl_add_category_word(COMPL_ROSTER, "toggle_offline");
  compl_add_category_word(COMPL_ROSTER, "item_lock");
  compl_add_category_word(COMPL_ROSTER, "item_unlock");
  compl_add_category_word(COMPL_ROSTER, "item_toggle_lock");
  compl_add_category_word(COMPL_ROSTER, "alternate");
  compl_add_category_word(COMPL_ROSTER, "search");
  compl_add_category_word(COMPL_ROSTER, "unread_first");
  compl_add_category_word(COMPL_ROSTER, "unread_next");
  compl_add_category_word(COMPL_ROSTER, "note");

  // Buffer category
  compl_add_category_word(COMPL_BUFFER, "clear");
  compl_add_category_word(COMPL_BUFFER, "bottom");
  compl_add_category_word(COMPL_BUFFER, "top");
  compl_add_category_word(COMPL_BUFFER, "up");
  compl_add_category_word(COMPL_BUFFER, "down");
  compl_add_category_word(COMPL_BUFFER, "search_backward");
  compl_add_category_word(COMPL_BUFFER, "search_forward");
  compl_add_category_word(COMPL_BUFFER, "date");
  compl_add_category_word(COMPL_BUFFER, "%");
  compl_add_category_word(COMPL_BUFFER, "purge");
  compl_add_category_word(COMPL_BUFFER, "close");
  compl_add_category_word(COMPL_BUFFER, "close_all");
  compl_add_category_word(COMPL_BUFFER, "scroll_lock");
  compl_add_category_word(COMPL_BUFFER, "scroll_unlock");
  compl_add_category_word(COMPL_BUFFER, "scroll_toggle");
  compl_add_category_word(COMPL_BUFFER, "list");
  compl_add_category_word(COMPL_BUFFER, "save");

  // Group category
  compl_add_category_word(COMPL_GROUP, "fold");
  compl_add_category_word(COMPL_GROUP, "unfold");
  compl_add_category_word(COMPL_GROUP, "toggle");

  // Multi-line (msay) category
  compl_add_category_word(COMPL_MULTILINE, "abort");
  compl_add_category_word(COMPL_MULTILINE, "begin");
  compl_add_category_word(COMPL_MULTILINE, "send");
  compl_add_category_word(COMPL_MULTILINE, "send_to");
  compl_add_category_word(COMPL_MULTILINE, "toggle");
  compl_add_category_word(COMPL_MULTILINE, "toggle_verbatim");
  compl_add_category_word(COMPL_MULTILINE, "verbatim");

  // Room category
  compl_add_category_word(COMPL_ROOM, "affil");
  compl_add_category_word(COMPL_ROOM, "ban");
  compl_add_category_word(COMPL_ROOM, "bookmark");
  compl_add_category_word(COMPL_ROOM, "destroy");
  compl_add_category_word(COMPL_ROOM, "invite");
  compl_add_category_word(COMPL_ROOM, "join");
  compl_add_category_word(COMPL_ROOM, "kick");
  compl_add_category_word(COMPL_ROOM, "leave");
  compl_add_category_word(COMPL_ROOM, "names");
  compl_add_category_word(COMPL_ROOM, "nick");
  compl_add_category_word(COMPL_ROOM, "privmsg");
  compl_add_category_word(COMPL_ROOM, "remove");
  compl_add_category_word(COMPL_ROOM, "role");
  compl_add_category_word(COMPL_ROOM, "setopt");
  compl_add_category_word(COMPL_ROOM, "topic");
  compl_add_category_word(COMPL_ROOM, "unban");
  compl_add_category_word(COMPL_ROOM, "unlock");
  compl_add_category_word(COMPL_ROOM, "whois");

  // Authorization category
  compl_add_category_word(COMPL_AUTH, "allow");
  compl_add_category_word(COMPL_AUTH, "cancel");
  compl_add_category_word(COMPL_AUTH, "request");
  compl_add_category_word(COMPL_AUTH, "request_unsubscribe");

  // Request (query) category
  compl_add_category_word(COMPL_REQUEST, "last");
  compl_add_category_word(COMPL_REQUEST, "ping");
  compl_add_category_word(COMPL_REQUEST, "time");
  compl_add_category_word(COMPL_REQUEST, "vcard");
  compl_add_category_word(COMPL_REQUEST, "version");

  // Events category
  compl_add_category_word(COMPL_EVENTS, "accept");
  compl_add_category_word(COMPL_EVENTS, "ignore");
  compl_add_category_word(COMPL_EVENTS, "reject");

  // PGP category
  compl_add_category_word(COMPL_PGP, "disable");
  compl_add_category_word(COMPL_PGP, "enable");
  compl_add_category_word(COMPL_PGP, "force");
  compl_add_category_word(COMPL_PGP, "info");
  compl_add_category_word(COMPL_PGP, "setkey");

  // OTR category
  compl_add_category_word(COMPL_OTR, "start");
  compl_add_category_word(COMPL_OTR, "stop");
  compl_add_category_word(COMPL_OTR, "fingerprint");
  compl_add_category_word(COMPL_OTR, "smpq");
  compl_add_category_word(COMPL_OTR, "smpr");
  compl_add_category_word(COMPL_OTR, "smpa");
  compl_add_category_word(COMPL_OTR, "info");
  compl_add_category_word(COMPL_OTR, "key");

  // OTR Policy category
  compl_add_category_word(COMPL_OTRPOLICY, "plain");
  compl_add_category_word(COMPL_OTRPOLICY, "manual");
  compl_add_category_word(COMPL_OTRPOLICY, "opportunistic");
  compl_add_category_word(COMPL_OTRPOLICY, "always");

  // Color category
  compl_add_category_word(COMPL_COLOR, "roster");
  compl_add_category_word(COMPL_COLOR, "muc");
  compl_add_category_word(COMPL_COLOR, "mucnick");

#ifdef MODULES_ENABLE
  // Module category
  compl_add_category_word(COMPL_MODULE, "info");
  compl_add_category_word(COMPL_MODULE, "list");
  compl_add_category_word(COMPL_MODULE, "load");
  compl_add_category_word(COMPL_MODULE, "unload");
#endif
}

//  expandalias(line)
// If there is one, expand the alias in line and returns a new allocated line
// If no alias is found, returns line
// Note: if the returned pointer is different from line, the caller should
//       g_free() the pointer after use
char *expandalias(const char *line)
{
  const char *p1, *p2;
  char *word;
  const gchar *value;
  char *newline = (char*)line;

  // Ignore leading COMMAND_CHAR
  for (p1 = line ; *p1 == COMMAND_CHAR ; p1++)
    ;
  // Locate the end of the word
  for (p2 = p1 ; *p2 && (*p2 != ' ') ; p2++)
    ;
  // Extract the word and look for an alias in the list
  word = g_strndup(p1, p2-p1);
  value = settings_get(SETTINGS_TYPE_ALIAS, (const char*)word);
  g_free(word);

  if (value)
    newline = g_strdup_printf("%c%s%s", COMMAND_CHAR, value, p2);

  return newline;
}

//  cmd_get
// Finds command in the command list structure.
// Returns a pointer to the cmd entry, or NULL if command not found.
cmd *cmd_get(const char *command)
{
  const char *p1, *p2;
  char *com;
  GSList *sl_com;

  // Ignore leading COMMAND_CHAR
  for (p1 = command ; *p1 == COMMAND_CHAR ; p1++)
    ;
  // Locate the end of the command
  for (p2 = p1 ; *p2 && (*p2 != ' ') ; p2++)
    ;
  // Copy the clean command
  com = g_strndup(p1, p2-p1);

  // Look for command in the list
  for (sl_com=Commands; sl_com; sl_com = g_slist_next(sl_com)) {
    if (!strcasecmp(com, ((cmd*)sl_com->data)->name))
      break;
  }
  g_free(com);

  if (sl_com)       // Command has been found.
    return (cmd*)sl_com->data;
  return NULL;
}

//  process_command(line, iscmd)
// Process a command line.
// If iscmd is TRUE, process the command even if verbatim mmode is set;
// it is intended to be used for key bindings.
// Return 255 if this is the /quit command, and 0 for the other commands.
int process_command(const char *line, guint iscmd)
{
  char *p;
  char *xpline;
  cmd *curcmd;

  if (!line)
    return 0;

  // We do alias expansion here
  if (iscmd || scr_get_multimode() != 2)
    xpline = expandalias(line);
  else
    xpline = (char*)line; // No expansion in verbatim multi-line mode

  // We want to use a copy
  if (xpline == line)
    xpline = g_strdup(line);

  // Remove trailing spaces:
  for (p=xpline ; *p ; p++)
    ;
  for (p-- ; p>xpline && (*p == ' ') ; p--)
    *p = 0;

  // Command "quit"?
  if ((iscmd || scr_get_multimode() != 2)
      && (!strncasecmp(xpline, mkcmdstr("quit"), strlen(mkcmdstr("quit"))))) {
    if (!xpline[5] || xpline[5] == ' ') {
      g_free(xpline);
      return 255;
    }
  } else if (iscmd && !strncasecmp(xpline, "quit", 4) &&
             (!xpline[4] || xpline[4] == ' ')) {
    // If iscmd is true we can have the command without the command prefix
    // character (usually '/').
    g_free(xpline);
    return 255;
  }

  // If verbatim multi-line mode, we check if another /msay command is typed
  if (!iscmd && scr_get_multimode() == 2
      && (strncasecmp(xpline, mkcmdstr("msay "), strlen(mkcmdstr("msay "))))) {
    // It isn't an /msay command
    scr_append_multiline(xpline);
    g_free(xpline);
    return 0;
  }

  // Commands handling
  curcmd = cmd_get(xpline);

  if (!curcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized command.  "
                 "Please see the manual for a list of known commands.");
    g_free(xpline);
    return 0;
  }
  if (!curcmd->func) {
    scr_LogPrint(LPRINT_NORMAL,
                 "This functionality is not yet implemented, sorry.");
    g_free(xpline);
    return 0;
  }
  // Lets go to the command parameters
  for (p = xpline+1; *p && (*p != ' ') ; p++)
    ;
  // Skip spaces
  while (*p && (*p == ' '))
    p++;
  // Call command-specific function
  retval_for_cmds = 0;
#ifdef MODULES_ENABLE
  if (curcmd->userdata)
    (*(void (*)(char *p, gpointer u))curcmd->func)(p, curcmd->userdata);
  else
    (*curcmd->func)(p);
#else
  (*curcmd->func)(p);
#endif
  g_free(xpline);
  return retval_for_cmds;
}

//  process_line(line)
// Process a command/message line.
// If this isn't a command, this is a message and it is sent to the
// currently selected buddy.
// Return 255 if the line is the /quit command, or 0.
int process_line(const char *line)
{
  if (!*line) { // User only pressed enter
    if (scr_get_multimode()) {
      scr_append_multiline("");
      return 0;
    }
    if (current_buddy) {
      if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_GROUP)
        do_group("toggle");
      else {
        // Enter chat mode
        scr_set_chatmode(TRUE);
        scr_show_buddy_window();
      }
    }
    return 0;
  }

  if (*line != COMMAND_CHAR) {
    // This isn't a command
    if (scr_get_multimode())
      scr_append_multiline(line);
    else
      say_cmd((char*)line, 0);
    return 0;
  }

  /* It is _probably_ a command -- except for verbatim multi-line mode */
  return process_command(line, FALSE);
}

// Helper routine for buffer item_{lock,unlock,toggle_lock}
// "lock" values: 1=lock 0=unlock -1=invert
static void roster_buddylock(char *bjid, int lock)
{
  gpointer bud = NULL;
  bool may_need_refresh = FALSE;

  // Allow special jid "" or "." (current buddy)
  if (bjid && (!*bjid || !strcmp(bjid, ".")))
    bjid = NULL;

  if (bjid) {
    // The JID has been specified.  Quick check...
    if (check_jid_syntax(bjid)) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "<%s> is not a valid Jabber ID.", bjid);
    } else {
      // Find the buddy
      GSList *roster_elt;
      roster_elt = roster_find(bjid, jidsearch,
                               ROSTER_TYPE_USER|ROSTER_TYPE_ROOM);
      if (roster_elt)
        bud = roster_elt->data;
      else
        scr_LogPrint(LPRINT_NORMAL, "This jid isn't in the roster.");
      may_need_refresh = TRUE;
    }
  } else {
    // Use the current buddy
    if (current_buddy)
      bud = BUDDATA(current_buddy);
  }

  // Update the ROSTER_FLAG_USRLOCK flag
  if (bud) {
    if (lock == -1)
      lock = !(buddy_getflags(bud) & ROSTER_FLAG_USRLOCK);
    buddy_setflags(bud, ROSTER_FLAG_USRLOCK, lock);
    if (may_need_refresh) {
      buddylist_build();
      update_roster = TRUE;
    }
  }
}

//  display_and_free_note(note, winId)
// Display the note information in the winId buffer, and free note
// (winId is a bare jid or NULL for the status window, in which case we
// display the note jid too)
static void display_and_free_note(struct annotation *note, const char *winId)
{
  gchar tbuf[128];
  GString *sbuf;
  guint msg_flag = HBB_PREFIX_INFO;
  /* We use the flag prefix_info for the first line, and prefix_cont
     for the other lines, for better readability */

  if (!note)
    return;

  sbuf = g_string_new("");

  if (!winId) {
    // We're writing to the status window, so let's show the jid too.
    g_string_printf(sbuf, "Annotation on <%s>", note->jid);
    scr_WriteIncomingMessage(winId, sbuf->str, 0, msg_flag, 0);
    msg_flag = HBB_PREFIX_INFO | HBB_PREFIX_CONT;
  }

  // If we have the creation date, display it
  if (note->cdate) {
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S",
             localtime(&note->cdate));
    g_string_printf(sbuf, "Note created  %s", tbuf);
    scr_WriteIncomingMessage(winId, sbuf->str, 0, msg_flag, 0);
    msg_flag = HBB_PREFIX_INFO | HBB_PREFIX_CONT;
  }
  // If we have the modification date, display it
  // unless it's the same as the creation date
  if (note->mdate && note->mdate != note->cdate) {
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S",
             localtime(&note->mdate));
    g_string_printf(sbuf, "Note modified %s", tbuf);
    scr_WriteIncomingMessage(winId, sbuf->str, 0, msg_flag, 0);
    msg_flag = HBB_PREFIX_INFO | HBB_PREFIX_CONT;
  }
  // Note text
  g_string_printf(sbuf, "Note: %s", note->text);
  scr_WriteIncomingMessage(winId, sbuf->str, 0, msg_flag, 0);

  g_string_free(sbuf, TRUE);
  g_free(note->text);
  g_free(note->jid);
  g_free(note);
}

static void display_all_annotations(void)
{
  GSList *notes;
  notes = xmpp_get_all_storage_rosternotes();

  if (!notes)
    return;

  // Call display_and_free_note() for each note,
  // with winId = NULL (special window)
  g_slist_foreach(notes, (GFunc)&display_and_free_note, NULL);
  scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
  scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                 ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
  g_slist_free(notes);
}

static void roster_note(char *arg)
{
  const char *bjid;
  guint type;

  if (!current_buddy)
    return;

  bjid = buddy_getjid(BUDDATA(current_buddy));
  type = buddy_gettype(BUDDATA(current_buddy));

  if (!bjid && type == ROSTER_TYPE_SPECIAL && !arg) {
    // We're in the status window (the only special buffer currently)
    // Let's display all server notes
    display_all_annotations();
    return;
  }

  if (!bjid || (type != ROSTER_TYPE_USER &&
               type != ROSTER_TYPE_ROOM &&
               type != ROSTER_TYPE_AGENT)) {
    scr_LogPrint(LPRINT_NORMAL, "This item can't have a note.");
    return;
  }

  if (arg && *arg) {  // Set a note
    gchar *msg, *notetxt;
    msg = to_utf8(arg);
    if (!strcmp(msg, "-"))
      notetxt = NULL; // delete note
    else
      notetxt = msg;
    xmpp_set_storage_rosternotes(bjid, notetxt);
    g_free(msg);
  } else {      // Display a note
    struct annotation *note = xmpp_get_storage_rosternotes(bjid, FALSE);
    if (note) {
      display_and_free_note(note, bjid);
    } else {
      scr_WriteIncomingMessage(bjid, "This item doesn't have a note.", 0,
                               HBB_PREFIX_INFO, 0);
    }
  }
}

//  roster_updown(updown, nitems)
// updown: -1=up, +1=down
inline static void roster_updown(int updown, char *nitems)
{
  int nbitems;

  if (!nitems || !*nitems)
    nbitems = 1;
  else
    nbitems = strtol(nitems, NULL, 10);

  if (nbitems > 0)
    scr_roster_up_down(updown, nbitems);
}

/* Commands callback functions */
/* All these do_*() functions will be called with a "arg" parameter */
/* (with arg not null)                                              */

static void do_roster(char *arg)
{
  char **paramlst;
  char *subcmd;

  paramlst = split_arg(arg, 2, 1); // subcmd, arg
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(subcmd, "top")) {
    scr_roster_top();
    update_roster = TRUE;
  } else if (!strcasecmp(subcmd, "bottom")) {
    scr_roster_bottom();
    update_roster = TRUE;
  } else if (!strcasecmp(subcmd, "hide")) {
    scr_roster_visibility(0);
  } else if (!strcasecmp(subcmd, "show")) {
    scr_roster_visibility(1);
  } else if (!strcasecmp(subcmd, "toggle")) {
    scr_roster_visibility(-1);
  } else if (!strcasecmp(subcmd, "hide_offline")) {
    buddylist_set_hide_offline_buddies(TRUE);
    if (current_buddy)
      buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(subcmd, "show_offline")) {
    buddylist_set_hide_offline_buddies(FALSE);
    buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(subcmd, "toggle_offline")) {
    buddylist_set_hide_offline_buddies(-1);
    buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(subcmd, "display")) {
    scr_roster_display(arg);
  } else if (!strcasecmp(subcmd, "item_lock")) {
    roster_buddylock(arg, 1);
  } else if (!strcasecmp(subcmd, "item_unlock")) {
    roster_buddylock(arg, 0);
  } else if (!strcasecmp(subcmd, "item_toggle_lock")) {
    roster_buddylock(arg, -1);
  } else if (!strcasecmp(subcmd, "unread_first")) {
    scr_roster_unread_message(0);
  } else if (!strcasecmp(subcmd, "unread_next")) {
    scr_roster_unread_message(1);
  } else if (!strcasecmp(subcmd, "alternate")) {
    scr_roster_jump_alternate();
  } else if (!strncasecmp(subcmd, "search", 6)) {
    strip_arg_special_chars(arg);
    if (!arg || !*arg) {
      scr_LogPrint(LPRINT_NORMAL, "What name or JID are you looking for?");
      free_arg_lst(paramlst);
      return;
    }
    scr_roster_search(arg);
    update_roster = TRUE;
  } else if (!strcasecmp(subcmd, "up")) {
    roster_updown(-1, arg);
  } else if (!strcasecmp(subcmd, "down")) {
    roster_updown(1, arg);
  } else if (!strcasecmp(subcmd, "group_prev")) {
    scr_roster_prev_group();
  } else if (!strcasecmp(subcmd, "group_next")) {
    scr_roster_next_group();
  } else if (!strcasecmp(subcmd, "note")) {
    roster_note(arg);
  } else
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  free_arg_lst(paramlst);
}

void do_color(char *arg)
{
  char **paramlst;
  char *subcmd;

  paramlst = split_arg(arg, 2, 1); // subcmd, arg
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(subcmd, "roster")) {
    char *status, *wildcard, *color;
    char **arglist = split_arg(arg, 3, 0);

    status = *arglist;
    wildcard = to_utf8(arglist[1]);
    color = arglist[2];

    if (status && !strcmp(status, "clear")) { // Not a color command, clear all
      scr_roster_clear_color();
      update_roster = TRUE;
    } else {
      if (!status || !*status || !wildcard || !*wildcard || !color || !*color) {
        scr_LogPrint(LPRINT_NORMAL, "Missing argument");
      } else {
        update_roster = scr_roster_color(status, wildcard, color) ||
                        update_roster;
      }
    }
    free_arg_lst(arglist);
    g_free(wildcard);
  } else if (!strcasecmp(subcmd, "muc")) {
    char **arglist = split_arg(arg, 2, 0);
    char *free_muc = to_utf8(*arglist);
    const char *muc = free_muc, *mode = arglist[1];
    if (!muc || !*muc)
      scr_LogPrint(LPRINT_NORMAL, "What MUC?");
    else {
      if (!strcmp(muc, "."))
        if (!(muc = CURRENT_JID))
          scr_LogPrint(LPRINT_NORMAL, "No JID selected");
      if (muc) {
        if (check_jid_syntax(muc) && strcmp(muc, "*"))
          scr_LogPrint(LPRINT_NORMAL, "Not a JID");
        else {
          if (!mode || !*mode || !strcasecmp(mode, "on"))
            scr_muc_color(muc, MC_ALL);
          else if (!strcasecmp(mode, "preset"))
            scr_muc_color(muc, MC_PRESET);
          else if (!strcasecmp(mode, "off"))
            scr_muc_color(muc, MC_OFF);
          else if (!strcmp(mode, "-"))
            scr_muc_color(muc, MC_REMOVE);
          else
            scr_LogPrint(LPRINT_NORMAL, "Unknown coloring mode");
        }
      }
    }
    free_arg_lst(arglist);
    g_free(free_muc);
  } else if (!strcasecmp(subcmd, "mucnick")) {
    char **arglist = split_arg(arg, 2, 0);
    const char *nick = *arglist, *color = arglist[1];
    if (!nick || !*nick || !color || !*color)
      scr_LogPrint(LPRINT_NORMAL, "Missing argument");
    else
      scr_muc_nick_color(nick, color);
    free_arg_lst(arglist);
  } else
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  free_arg_lst(paramlst);
}

//  cmd_setstatus(recipient, arg)
// Set your Jabber status.
// - if recipient is not NULL, the status is sent to this contact only
// - arg must be "status message" (message is optional)
void cmd_setstatus(const char *recipient, const char *arg)
{
  char **paramlst;
  char *status;
  char *msg;
  enum imstatus st;

  if (!xmpp_is_online())
    scr_LogPrint(LPRINT_NORMAL, "You are currently not connected...");
  // We do not return now, so that the status is memorized and used later...

  // It makes sense to reset autoaway before changing the status
  // (esp. for FIFO or remote commands) or the behaviour could be
  // unexpected...
  if (!recipient)
    scr_check_auto_away(TRUE);

  paramlst = split_arg(arg, 2, 1); // status, message
  status = *paramlst;
  msg = *(paramlst+1);

  if (!status) {
    free_arg_lst(paramlst);
    return;
  }

  if      (!strcasecmp(status, IMSTATUS_OFFLINE))       st = offline;
  else if (!strcasecmp(status, IMSTATUS_ONLINE))        st = available;
  else if (!strcasecmp(status, IMSTATUS_AVAILABLE))     st = available;
  else if (!strcasecmp(status, IMSTATUS_AWAY))          st = away;
  else if (!strcasecmp(status, IMSTATUS_INVISIBLE))     st = invisible;
  else if (!strcasecmp(status, IMSTATUS_DONOTDISTURB))  st = dontdisturb;
  else if (!strcasecmp(status, IMSTATUS_NOTAVAILABLE))  st = notavail;
  else if (!strcasecmp(status, IMSTATUS_FREE4CHAT))     st = freeforchat;
  else if (!strcasecmp(status, "message")) {
    if (!msg || !*msg) {
      // We want a message.  If there's none, we give up.
      scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
      free_arg_lst(paramlst);
      return;
    }
    st = xmpp_getstatus();  // Preserve current status
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized status!");
    free_arg_lst(paramlst);
    return;
  }

  // Use provided message
  if (msg && !*msg) {
    msg = NULL;
  }

  // If a recipient is specified, let's don't use default status messages
  if (recipient && !msg)
    msg = "";

  xmpp_setstatus(st, recipient, msg, FALSE);

  free_arg_lst(paramlst);
}

static void do_status(char *arg)
{
  if (!*arg) {
    const char *sm = xmpp_getstatusmsg();
    scr_LogPrint(LPRINT_NORMAL, "Your status is: [%c] %s",
                 imstatus2char[xmpp_getstatus()],
                 (sm ? sm : ""));
    return;
  }
  arg = to_utf8(arg);
  cmd_setstatus(NULL, arg);
  g_free(arg);
}

static void do_status_to(char *arg)
{
  char **paramlst;
  char *fjid, *st, *msg;
  char *jid_utf8 = NULL;

  paramlst = split_arg(arg, 3, 1); // jid, status, [message]
  fjid = *paramlst;
  st = *(paramlst+1);
  msg = *(paramlst+2);

  if (!fjid || !st) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Please specify both a Jabber ID and a status.");
    free_arg_lst(paramlst);
    return;
  }

  // Allow things like /status_to "" away
  if (!*fjid || !strcmp(fjid, "."))
    fjid = NULL;

  if (fjid) {
    // The JID has been specified.  Quick check...
    if (check_jid_syntax(fjid)) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "<%s> is not a valid Jabber ID.", fjid);
      fjid = NULL;
    } else {
      // Convert jid to lowercase
      char *p = fjid;
      for ( ; *p && *p != JID_RESOURCE_SEPARATOR; p++)
        *p = tolower(*p);
      fjid = jid_utf8 = to_utf8(fjid);
    }
  } else {
    // Add the current buddy
    if (current_buddy)
      fjid = (char*)buddy_getjid(BUDDATA(current_buddy));
    if (!fjid)
      scr_LogPrint(LPRINT_NORMAL, "Please specify a Jabber ID.");
  }

  if (fjid) {
    char *cmdline;
    if (!msg)
      msg = "";
    msg = to_utf8(msg);
    cmdline = g_strdup_printf("%s %s", st, msg);
    scr_LogPrint(LPRINT_LOGNORM, "Sending to <%s> /status %s", fjid, cmdline);
    cmd_setstatus(fjid, cmdline);
    g_free(msg);
    g_free(cmdline);
    g_free(jid_utf8);
  }
  free_arg_lst(paramlst);
}

static void do_add(char *arg)
{
  char **paramlst;
  char *id, *nick;
  char *jid_utf8 = NULL;

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    return;
  }

  paramlst = split_arg(arg, 2, 0); // jid, [nickname]
  id = *paramlst;
  nick = *(paramlst+1);

  if (!id)
    nick = NULL; // Allow things like: /add "" nick
  else if (!*id || !strcmp(id, "."))
    id = NULL;

  if (id) {
    // The JID has been specified.  Quick check...
    if (check_jid_syntax(id)) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "<%s> is not a valid Jabber ID.", id);
      id = NULL;
    } else {
      mc_strtolower(id);
      id = jid_utf8 = to_utf8(id);
    }
  } else {
    // Add the current buddy
    if (current_buddy)
      id = (char*)buddy_getjid(BUDDATA(current_buddy));
    if (!id)
      scr_LogPrint(LPRINT_NORMAL, "Please specify a Jabber ID.");
  }

  if (nick)
    nick = to_utf8(nick);

  if (id) {
    // 2nd parameter = optional nickname
    xmpp_addbuddy(id, nick, NULL);
    scr_LogPrint(LPRINT_LOGNORM, "Sent presence notification request to <%s>.",
                 id);
  }

  g_free(jid_utf8);
  g_free(nick);
  free_arg_lst(paramlst);
}

static void do_del(char *arg)
{
  const char *bjid;

  if (*arg) {
    scr_LogPrint(LPRINT_NORMAL, "This action does not require a parameter; "
                 "the currently-selected buddy will be deleted.");
    return;
  }

  if (!current_buddy)
    return;
  bjid = buddy_getjid(BUDDATA(current_buddy));
  if (!bjid)
    return;

  if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_ROOM) {
    // This is a chatroom
    if (buddy_getinsideroom(BUDDATA(current_buddy))) {
      scr_LogPrint(LPRINT_NORMAL, "You haven't left this room!");
      return;
    }
  }

  // Close the buffer
  scr_buffer_purge(1, NULL);

  scr_LogPrint(LPRINT_LOGNORM, "Removing <%s>...", bjid);
  xmpp_delbuddy(bjid);
  scr_update_buddy_window();
}

static void do_group(char *arg)
{
  gpointer group = NULL;
  guint leave_buddywindow;
  char **paramlst;
  char *subcmd;
  enum { group_toggle = -1, group_unfold = 0, group_fold = 1 } group_state = 0;

  if (!*arg) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    return;
  }

  if (!current_buddy)
    return;

  paramlst = split_arg(arg, 2, 0); // subcmd, [arg]
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd)
    goto do_group_return;   // Should not happen anyway

  if (arg && *arg) {
    GSList *roster_elt;
    char *group_utf8 = to_utf8(arg);
    roster_elt = roster_find(group_utf8, namesearch, ROSTER_TYPE_GROUP);
    g_free(group_utf8);
    if (roster_elt)
      group = buddy_getgroup(roster_elt->data);
  } else {
    group = buddy_getgroup(BUDDATA(current_buddy));
  }
  if (!group)
    goto do_group_return;

  // We'll have to redraw the chat window if we're not currently on the group
  // entry itself, because it means we'll have to leave the current buddy
  // chat window.
  leave_buddywindow = (group != BUDDATA(current_buddy) &&
                       group == buddy_getgroup(BUDDATA(current_buddy)));


  if (!(buddy_gettype(group) & ROSTER_TYPE_GROUP)) {
    scr_LogPrint(LPRINT_NORMAL, "You need to select a group.");
    goto do_group_return;
  }

  if (!strcasecmp(subcmd, "expand") || !strcasecmp(subcmd, "unfold"))
    group_state = group_unfold;
  else if (!strcasecmp(subcmd, "shrink") || !strcasecmp(subcmd, "fold"))
    group_state = group_fold;
  else if (!strcasecmp(subcmd, "toggle"))
    group_state = group_toggle;
  else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
    goto do_group_return;
  }

  if (group_state != group_unfold && leave_buddywindow)
    scr_roster_prev_group();

  buddy_hide_group(group, group_state);

  buddylist_build();
  update_roster = TRUE;

do_group_return:
  free_arg_lst(paramlst);
}

static int send_message_to(const char *fjid, const char *msg, const char *subj,
                           LmMessageSubType type_overwrite, bool quiet)
{
  char *bare_jid, *rp;
  char *hmsg;
  gint crypted;
  gint retval = 0;
  int isroom;
  gpointer xep184 = NULL;

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    return 1;
  }
  if (!fjid || !*fjid) {
    scr_LogPrint(LPRINT_NORMAL, "You must specify a Jabber ID.");
    return 1;
  }
  if (!msg || !*msg) {
    scr_LogPrint(LPRINT_NORMAL, "You must specify a message.");
    return 1;
  }
  if (check_jid_syntax((char*)fjid)) {
    scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                 "<%s> is not a valid Jabber ID.", fjid);
    return 1;
  }

  // We must use the bare jid in hk_message_out()
  rp = strchr(fjid, JID_RESOURCE_SEPARATOR);
  if (rp)
    bare_jid = g_strndup(fjid, rp - fjid);
  else
    bare_jid = (char*)fjid;

  if (!quiet) {
    // Jump to window, create one if needed
    scr_roster_jump_jid(bare_jid);
  }

  // Check if we're sending a message to a conference room
  // If not, we must make sure rp is NULL, for hk_message_out()
  isroom = !!roster_find(bare_jid, jidsearch, ROSTER_TYPE_ROOM);
  if (rp) {
    if (isroom) rp++;
    else rp = NULL;
  }
  isroom = isroom && (!rp || !*rp);

  // local part (UI, logging, etc.)
  if (subj)
    hmsg = g_strdup_printf("[%s]\n%s", subj, msg);
  else
    hmsg = (char*)msg;

  // Network part
  xmpp_send_msg(fjid, msg, (isroom ? ROSTER_TYPE_ROOM : ROSTER_TYPE_USER),
                subj, FALSE, &crypted, type_overwrite, &xep184);

  if (crypted == -1) {
    scr_LogPrint(LPRINT_LOGNORM, "Encryption error.  Message was not sent.");
    retval = 1;
    goto send_message_to_return;
  }

  // Hook
  if (!isroom)
    hk_message_out(bare_jid, rp, 0, hmsg, crypted, xep184);

send_message_to_return:
  if (hmsg != msg) g_free(hmsg);
  if (rp) g_free(bare_jid);
  return retval;
}

//  send_message(msg, subj, type_overwrite)
// Write the message in the buddy's window and send the message on
// the network.
static void send_message(const char *msg, const char *subj,
                         LmMessageSubType type_overwrite)
{
  const char *bjid;

  if (!current_buddy) {
    scr_LogPrint(LPRINT_NORMAL, "No buddy is currently selected.");
    return;
  }

  bjid = CURRENT_JID;
  if (!bjid) {
    scr_LogPrint(LPRINT_NORMAL, "No buddy is currently selected.");
    return;
  }

  send_message_to(bjid, msg, subj, type_overwrite, FALSE);
}

static LmMessageSubType scan_mtype(char **arg)
{
  // Try splitting it
  char **parlist = split_arg(*arg, 2, 1);
  LmMessageSubType result = LM_MESSAGE_SUB_TYPE_NOT_SET;
  // Is it a good parameter?
  if (parlist && *parlist) {
    if (!strcmp("-n", *parlist)) {
      result = LM_MESSAGE_SUB_TYPE_NORMAL;
    } else if (!strcmp("-h", *parlist)) {
      result = LM_MESSAGE_SUB_TYPE_HEADLINE;
    }
    if (result != LM_MESSAGE_SUB_TYPE_NOT_SET || (!strcmp("--", *parlist)))
      *arg += strlen(*arg) - (parlist[1] ? strlen(parlist[1]) : 0);
  }
  // Anything found? -> skip it
  free_arg_lst(parlist);
  return result;
}

void say_cmd(char *arg, int parse_flags)
{
  gpointer bud;
  LmMessageSubType msgtype = LM_MESSAGE_SUB_TYPE_NOT_SET;

  scr_set_chatmode(TRUE);
  scr_show_buddy_window();

  if (!current_buddy) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Whom are you talking to?  Please select a buddy.");
    return;
  }

  bud = BUDDATA(current_buddy);
  if (!(buddy_gettype(bud) &
        (ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM))) {
    scr_LogPrint(LPRINT_NORMAL, "This is not a user.");
    return;
  }

  buddy_setflags(bud, ROSTER_FLAG_LOCK, TRUE);
  if (parse_flags)
    msgtype = scan_mtype(&arg);
  arg = to_utf8(arg);
  send_message(arg, NULL, msgtype);
  g_free(arg);
}

static void do_say(char *arg) {
  say_cmd(arg, 1);
}

static void do_msay(char *arg)
{
  /* Parameters: begin verbatim abort send send_to */
  char **paramlst;
  char *subcmd;

  paramlst = split_arg(arg, 2, 1); // subcmd, arg
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    scr_LogPrint(LPRINT_NORMAL, "Please read the manual before using "
                 "the /msay command.");
    scr_LogPrint(LPRINT_NORMAL, "(Use \"%s begin\" to enter "
                 "multi-line mode...)", mkcmdstr("msay"));
    goto do_msay_return;
  }

  if (!strcasecmp(subcmd, "toggle")) {
    if (scr_get_multimode())
      subcmd = "send";
    else
      subcmd = "begin";
  } else if (!strcasecmp(subcmd, "toggle_verbatim")) {
    if (scr_get_multimode())
      subcmd = "send";
    else
      subcmd = "verbatim";
  }

  if (!strcasecmp(subcmd, "abort")) {
    if (scr_get_multimode())
      scr_LogPrint(LPRINT_NORMAL, "Leaving multi-line message mode.");
    scr_set_multimode(FALSE, NULL);
    goto do_msay_return;
  } else if ((!strcasecmp(subcmd, "begin")) ||
             (!strcasecmp(subcmd, "verbatim"))) {
    bool verbat;
    gchar *subj_utf8 = to_utf8(arg);
    if (!strcasecmp(subcmd, "verbatim")) {
      scr_set_multimode(2, subj_utf8);
      verbat = TRUE;
    } else {
      scr_set_multimode(1, subj_utf8);
      verbat = FALSE;
    }

    scr_LogPrint(LPRINT_NORMAL, "Entered %smulti-line message mode.",
                 verbat ? "VERBATIM " : "");
    scr_LogPrint(LPRINT_NORMAL, "Select a buddy and use \"%s send\" "
                 "when your message is ready.", mkcmdstr("msay"));
    if (verbat)
      scr_LogPrint(LPRINT_NORMAL, "Use \"%s abort\" to abort this mode.",
                   mkcmdstr("msay"));
    g_free(subj_utf8);
    goto do_msay_return;
  } else if (strcasecmp(subcmd, "send") && strcasecmp(subcmd, "send_to")) {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
    goto do_msay_return;
  }

  /* send/send_to command */

  if (!scr_get_multimode()) {
    scr_LogPrint(LPRINT_NORMAL, "No message to send.  "
                 "Use \"%s begin\" first.", mkcmdstr("msay"));
    goto do_msay_return;
  }

  scr_set_chatmode(TRUE);
  scr_show_buddy_window();

  if (!strcasecmp(subcmd, "send_to")) {
    int err = FALSE;
    gchar *msg_utf8;
    LmMessageSubType msg_type = scan_mtype(&arg);
    // Let's send to the specified JID.  We leave now if there
    // has been an error (so we don't leave multi-line mode).
    arg = to_utf8(arg);
    msg_utf8 = to_utf8(scr_get_multiline());
    if (msg_utf8) {
      err = send_message_to(arg, msg_utf8, scr_get_multimode_subj(), msg_type,
                            FALSE);
      g_free(msg_utf8);
    }
    g_free(arg);
    if (err)
      goto do_msay_return;
  } else { // Send to currently selected buddy
    gpointer bud;
    gchar *msg_utf8;

    if (!current_buddy) {
      scr_LogPrint(LPRINT_NORMAL, "Whom are you talking to?");
      goto do_msay_return;
    }

    bud = BUDDATA(current_buddy);
    if (!(buddy_gettype(bud) &
          (ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM))) {
      scr_LogPrint(LPRINT_NORMAL, "This is not a user.");
      goto do_msay_return;
    }

    buddy_setflags(bud, ROSTER_FLAG_LOCK, TRUE);
    msg_utf8 = to_utf8(scr_get_multiline());
    if (msg_utf8) {
      send_message(msg_utf8, scr_get_multimode_subj(), scan_mtype(&arg));
      g_free(msg_utf8);
    }
  }
  scr_set_multimode(FALSE, NULL);
  scr_LogPrint(LPRINT_NORMAL, "You have left multi-line message mode.");
do_msay_return:
  free_arg_lst(paramlst);
}

//  load_message_from_file(filename)
// Read the whole content of a file.
// The data are converted to UTF8, they should be freed by the caller after
// use.
char *load_message_from_file(const char *filename)
{
  FILE *fd;
  struct stat buf;
  char *msgbuf, *msgbuf_utf8;
  char *p;
  char *next_utf8_char;
  size_t len;

  fd = fopen(filename, "r");

  if (!fd || fstat(fileno(fd), &buf)) {
    scr_LogPrint(LPRINT_LOGNORM, "Cannot open message file (%s)", filename);
    return NULL;
  }
  if (!buf.st_size || buf.st_size >= HBB_BLOCKSIZE) {
    if (!buf.st_size)
      scr_LogPrint(LPRINT_LOGNORM, "Message file is empty (%s)", filename);
    else
      scr_LogPrint(LPRINT_LOGNORM, "Message file is too big (%s)", filename);
    fclose(fd);
    return NULL;
  }

  msgbuf = g_new0(char, HBB_BLOCKSIZE);
  len = fread(msgbuf, 1, HBB_BLOCKSIZE-1, fd);
  fclose(fd);

  next_utf8_char = msgbuf;

  // Check there is no binary data.  It must be a *message* file!
  for (p = msgbuf ; *p ; p++) {
    if (utf8_mode) {
      if (p == next_utf8_char) {
        if (!iswprint(get_char(p)) && *p != '\n' && *p != '\t')
          break;
        next_utf8_char = next_char(p);
      }
    } else {
      unsigned char sc = *p;
      if (!iswprint(sc) && sc != '\n' && sc != '\t')
        break;
    }
  }

  if (*p || (size_t)(p-msgbuf) != len) { // We're not at the End Of Line...
    scr_LogPrint(LPRINT_LOGNORM, "Message file contains "
                 "invalid characters (%s)", filename);
    g_free(msgbuf);
    return NULL;
  }

  // p is now at the EOL
  // Let's strip trailing newlines
  if (p > msgbuf)
    p--;
  while (p > msgbuf && *p == '\n')
    *p-- = 0;

  // It could be empty, once the trailing newlines are gone
  if (p == msgbuf && *p == '\n') {
    scr_LogPrint(LPRINT_LOGNORM, "Message file is empty (%s)", filename);
    g_free(msgbuf);
    return NULL;
  }

  msgbuf_utf8 = to_utf8(msgbuf);

  if (!msgbuf_utf8 && msgbuf)
    scr_LogPrint(LPRINT_LOGNORM, "Message file charset conversion error (%s)",
                 filename);
  g_free(msgbuf);
  return msgbuf_utf8;
}

static void do_say_to(char *arg)
{
  char **paramlst;
  char *fjid, *msg;
  char *file = NULL;
  LmMessageSubType msg_type = LM_MESSAGE_SUB_TYPE_NOT_SET;
  bool quiet = FALSE;

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    return;
  }

  msg_type = scan_mtype(&arg);
  paramlst = split_arg(arg, 2, 1); // jid, message (or option, jid, message)

  if (!*paramlst) {  // No parameter?
    scr_LogPrint(LPRINT_NORMAL, "Please specify a Jabber ID.");
    free_arg_lst(paramlst);
    return;
  }

  // Check for an option parameter
  while (*paramlst) {
    if (!strcmp(*paramlst, "-q")) {
      char **oldparamlst = paramlst;
      paramlst = split_arg(*(oldparamlst+1), 2, 1); // jid, message
      free_arg_lst(oldparamlst);
      quiet = TRUE;
    } else if (!strcmp(*paramlst, "-f")) {
      char **oldparamlst = paramlst;
      paramlst = split_arg(*(oldparamlst+1), 2, 1); // filename, jid
      free_arg_lst(oldparamlst);
      if (!*paramlst) {
        scr_LogPrint(LPRINT_NORMAL, "Wrong usage.");
        free_arg_lst(paramlst);
        return;
      }
      file = g_strdup(*paramlst);
      // One more parameter shift...
      oldparamlst = paramlst;
      paramlst = split_arg(*(oldparamlst+1), 2, 1); // jid, nothing
      free_arg_lst(oldparamlst);
    } else
      break;
  }

  if (!*paramlst) {
    scr_LogPrint(LPRINT_NORMAL, "Wrong usage.");
    free_arg_lst(paramlst);
    return;
  }

  fjid = *paramlst;
  msg = *(paramlst+1);

  if (fjid[0] == '.') {
    const gchar *cjid = (current_buddy ? CURRENT_JID : NULL);
    if (fjid[1] == '\0') {
      fjid = g_strdup(cjid);
    } else if (fjid[1] == JID_RESOURCE_SEPARATOR) {
      char *res_utf8 = to_utf8(fjid+2);
      fjid = g_strdup_printf("%s%c%s", cjid, JID_RESOURCE_SEPARATOR, res_utf8);
      g_free(res_utf8);
    } else
      fjid = to_utf8(fjid);
  } else
    fjid = to_utf8(fjid);

  if (check_jid_syntax(fjid)) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a valid Jabber ID.");
    free_arg_lst(paramlst);
    g_free(fjid);
    return;
  }

  if (!file) {
    msg = to_utf8(msg);
  } else {
    char *filename_xp;
    if (msg)
      scr_LogPrint(LPRINT_NORMAL, "say_to: extra parameter ignored.");
    filename_xp = expand_filename(file);
    msg = load_message_from_file(filename_xp);
    g_free(filename_xp);
    g_free(file);
  }

  send_message_to(fjid, msg, NULL, msg_type, quiet);

  g_free(fjid);
  g_free(msg);
  free_arg_lst(paramlst);
}

//  buffer_updown(updown, nblines)
// updown: -1=up, +1=down
inline static void buffer_updown(int updown, char *nlines)
{
  int nblines;

  if (!nlines || !*nlines)
    nblines = 0;
  else
    nblines = strtol(nlines, NULL, 10);

  if (nblines >= 0)
    scr_buffer_scroll_up_down(updown, nblines);
}

static void buffer_search(int direction, char *arg)
{
  if (!arg || !*arg) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    return;
  }

  scr_buffer_search(direction, arg);
}

static void buffer_date(char *date)
{
  time_t t;

  if (!date || !*date) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    return;
  }

  strip_arg_special_chars(date);

  t = from_iso8601(date, 0);
  if (t)
    scr_buffer_date(t);
  else
    scr_LogPrint(LPRINT_NORMAL, "The date you specified is "
                 "not correctly formatted or invalid.");
}

static void buffer_percent(char *arg1, char *arg2)
{
  // Basically, user has typed "%arg1 arg2"
  // "%50"  -> arg1 = 50, arg2 null pointer
  // "% 50" -> arg1 = \0, arg2 = 50

  if (!*arg1 && (!arg2 || !*arg2)) { // No value
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    return;
  }

  if (*arg1 && arg2 && *arg2) {     // Two values
    scr_LogPrint(LPRINT_NORMAL, "Wrong parameters.");
    return;
  }

  scr_buffer_percent(atoi((*arg1 ? arg1 : arg2)));
}

static void do_buffer(char *arg)
{
  char **paramlst;
  char *subcmd;

  if (!current_buddy)
    return;

  paramlst = split_arg(arg, 2, 1); // subcmd, arg
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    free_arg_lst(paramlst);
    return;
  }

  if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_GROUP &&
      strcasecmp(subcmd, "close_all")) {
    scr_LogPrint(LPRINT_NORMAL, "Groups have no buffer.");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(subcmd, "top")) {
    scr_buffer_top_bottom(-1);
  } else if (!strcasecmp(subcmd, "bottom")) {
    scr_buffer_top_bottom(1);
  } else if (!strcasecmp(subcmd, "clear")) {
    scr_buffer_clear();
  } else if (!strcasecmp(subcmd, "close")) {
    scr_buffer_purge(1, arg);
  } else if (!strcasecmp(subcmd, "close_all")) {
    scr_buffer_purge_all(1);
  } else if (!strcasecmp(subcmd, "purge")) {
    scr_buffer_purge(0, arg);
  } else if (!strcasecmp(subcmd, "scroll_lock")) {
    scr_buffer_scroll_lock(1);
  } else if (!strcasecmp(subcmd, "scroll_unlock")) {
    scr_buffer_scroll_lock(0);
  } else if (!strcasecmp(subcmd, "scroll_toggle")) {
    scr_buffer_scroll_lock(-1);
  } else if (!strcasecmp(subcmd, "up")) {
    buffer_updown(-1, arg);
  } else if (!strcasecmp(subcmd, "down")) {
    buffer_updown(1, arg);
  } else if (!strcasecmp(subcmd, "search_backward")) {
    strip_arg_special_chars(arg);
    buffer_search(-1, arg);
  } else if (!strcasecmp(subcmd, "search_forward")) {
    strip_arg_special_chars(arg);
    buffer_search(1, arg);
  } else if (!strcasecmp(subcmd, "date")) {
    buffer_date(arg);
  } else if (*subcmd == '%') {
    buffer_percent(subcmd+1, arg);
  } else if (!strcasecmp(subcmd, "save")) {
    scr_buffer_dump(arg);
  } else if (!strcasecmp(subcmd, "list")) {
    scr_buffer_list();
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  }

  free_arg_lst(paramlst);
}

static void do_clear(char *arg)    // Alias for "buffer clear"
{
  do_buffer("clear");
}

static void do_info(char *arg)
{
  gpointer bud;
  const char *bjid, *name;
  guint type, on_srv;
  char *buffer;
  enum subscr esub;

  if (!current_buddy)
    return;
  bud = BUDDATA(current_buddy);

  bjid   = buddy_getjid(bud);
  name   = buddy_getname(bud);
  type   = buddy_gettype(bud);
  esub   = buddy_getsubscription(bud);
  on_srv = buddy_getonserverflag(bud);

  buffer = g_new(char, 4096);

  if (bjid) {
    GSList *resources, *p_res;
    char *bstr = "unknown";

    // Enter chat mode
    scr_set_chatmode(TRUE);
    scr_show_buddy_window();

    snprintf(buffer, 4095, "jid:  <%s>", bjid);
    scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
    if (name) {
      snprintf(buffer, 4095, "Name: %s", name);
      scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
    }

    if (type == ROSTER_TYPE_USER)       bstr = "user";
    else if (type == ROSTER_TYPE_ROOM)  bstr = "chatroom";
    else if (type == ROSTER_TYPE_AGENT) bstr = "agent";
    snprintf(buffer, 127, "Type: %s", bstr);
    scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);

    if (!on_srv) {
      scr_WriteIncomingMessage(bjid, "(Local item, not on the server)",
                               0, HBB_PREFIX_INFO, 0);
    }

    if (esub == sub_both)     bstr = "both";
    else if (esub & sub_from) bstr = "from";
    else if (esub & sub_to)   bstr = "to";
    else bstr = "none";
    snprintf(buffer, 64, "Subscription: %s", bstr);
    if (esub & sub_pending)
      strcat(buffer, " (pending)");
    scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);

    resources = buddy_getresources(bud);
    if (!resources && type == ROSTER_TYPE_USER) {
      // No resource; display last status message, if any.
      const char *rst_msg = buddy_getstatusmsg(bud, "");
      if (rst_msg) {
        snprintf(buffer, 4095, "Last status message: %s", rst_msg);
        scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
      }
    }
    for (p_res = resources ; p_res ; p_res = g_slist_next(p_res)) {
      gchar rprio;
      enum imstatus rstatus;
      const char *rst_msg;
      time_t rst_time;
      struct pgp_data *rpgp;

      rprio   = buddy_getresourceprio(bud, p_res->data);
      rstatus = buddy_getstatus(bud, p_res->data);
      rst_msg = buddy_getstatusmsg(bud, p_res->data);
      rst_time = buddy_getstatustime(bud, p_res->data);
      rpgp = buddy_resource_pgp(bud, p_res->data);

      snprintf(buffer, 4095, "Resource: [%c] (%d) %s", imstatus2char[rstatus],
               rprio, (char*)p_res->data);
      scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
      if (rst_msg) {
        snprintf(buffer, 4095, "Status message: %s", rst_msg);
        scr_WriteIncomingMessage(bjid, buffer,
                                 0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
      }
      if (rst_time) {
        char tbuf[128];

        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&rst_time));
        snprintf(buffer, 127, "Status timestamp: %s", tbuf);
        scr_WriteIncomingMessage(bjid, buffer,
                                 0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
      }
#ifdef HAVE_GPGME
      if (rpgp && rpgp->sign_keyid) {
        snprintf(buffer, 4095, "PGP key id: %s", rpgp->sign_keyid);
        scr_WriteIncomingMessage(bjid, buffer,
                                 0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
        if (rpgp->last_sigsum) {
          gpgme_sigsum_t ss = rpgp->last_sigsum;
          snprintf(buffer, 4095, "Last PGP signature: %s",
                  (ss & GPGME_SIGSUM_GREEN ? "good":
                   (ss & GPGME_SIGSUM_RED ? "bad" : "unknown")));
          scr_WriteIncomingMessage(bjid, buffer,
                                   0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
        }
      }
#endif
      g_free(p_res->data);
    }
    g_slist_free(resources);
  } else {  /* Item has no jid */
    if (name) scr_LogPrint(LPRINT_NORMAL, "Name: %s", name);
    scr_LogPrint(LPRINT_NORMAL, "Type: %s",
                 type == ROSTER_TYPE_GROUP ? "group" :
                 (type == ROSTER_TYPE_SPECIAL ? "special" : "unknown"));
  }
  g_free(buffer);

  // Tell the user if this item has an annotation.
  if (type == ROSTER_TYPE_USER ||
      type == ROSTER_TYPE_ROOM ||
      type == ROSTER_TYPE_AGENT) {
    struct annotation *note = xmpp_get_storage_rosternotes(bjid, TRUE);
    if (note) {
      // We do not display the note, we just tell the user.
      g_free(note->text);
      g_free(note->jid);
      g_free(note);
      scr_WriteIncomingMessage(bjid, "(This item has an annotation)", 0,
                               HBB_PREFIX_INFO, 0);
    }
  }
}

// room_names() is a variation of do_info(), for chatrooms only
static void room_names(gpointer bud, char *arg)
{
  const char *bjid;
  char *buffer;
  GSList *resources, *p_res;
  enum { style_normal = 0, style_detail, style_short,
         style_quiet, style_compact } style = 0;

  if (*arg) {
    if (!strcasecmp(arg, "--short"))
      style = style_short;
    else if (!strcasecmp(arg, "--quiet"))
      style = style_quiet;
    else if (!strcasecmp(arg, "--detail"))
      style = style_detail;
    else if (!strcasecmp(arg, "--compact"))
      style = style_compact;
    else {
      scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
      return;
    }
  }

  // Enter chat mode
  scr_set_chatmode(TRUE);
  scr_show_buddy_window();

  bjid = buddy_getjid(bud);

  buffer = g_new(char, 4096);
  strncpy(buffer, "Room members:", 127);
  scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);

  resources = buddy_getresources(bud);
  for (p_res = resources ; p_res ; p_res = g_slist_next(p_res)) {
    enum imstatus rstatus;
    const char *rst_msg;

    rstatus = buddy_getstatus(bud, p_res->data);
    rst_msg = buddy_getstatusmsg(bud, p_res->data);

    if (style == style_short) {
      snprintf(buffer, 4095, "[%c] %s%s%s", imstatus2char[rstatus],
               (char*)p_res->data,
               rst_msg ? " -- " : "", rst_msg ? rst_msg : "");
      scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
    } else if (style == style_compact) {
        enum imrole role = buddy_getrole(bud, p_res->data);
        enum imaffiliation affil = buddy_getaffil(bud, p_res->data);
        bool showaffil = (affil != affil_none);

        snprintf(buffer, 4095, "[%c] %s (%s%s%s)",
                 imstatus2char[rstatus], (char*)p_res->data,
                 showaffil ? straffil[affil] : "\0",
                 showaffil ? "/" : "\0",
                 strrole[role]);
        scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
      } else {
      // (Style "normal", "detail" or "quiet")
      snprintf(buffer, 4095, "[%c] %s", imstatus2char[rstatus],
               (char*)p_res->data);
      scr_WriteIncomingMessage(bjid, buffer, 0, HBB_PREFIX_INFO, 0);
      if (rst_msg && style != style_quiet) {
        snprintf(buffer, 4095, "Status message: %s", rst_msg);
        scr_WriteIncomingMessage(bjid, buffer,
                                 0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
      }
      if (style == style_detail) {
        enum imrole role = buddy_getrole(bud, p_res->data);
        enum imaffiliation affil = buddy_getaffil(bud, p_res->data);

        snprintf(buffer, 4095, "Role: %s", strrole[role]);
        scr_WriteIncomingMessage(bjid, buffer,
                                 0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
        if (affil != affil_none) {
          snprintf(buffer, 4095, "Affiliat.: %s", straffil[affil]);
          scr_WriteIncomingMessage(bjid, buffer,
                                   0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
        }
      }
    }
    g_free(p_res->data);
  }
  g_slist_free(resources);
  g_free(buffer);
}

static void move_group_member(gpointer bud, void *groupnamedata)
{
  const char *bjid, *name, *groupname;

  groupname = (char *)groupnamedata;

  bjid = buddy_getjid(bud);
  name = buddy_getname(bud);

  xmpp_updatebuddy(bjid, name, *groupname ? groupname : NULL);
}

static void do_rename(char *arg)
{
  gpointer bud;
  const char *bjid, *group;
  guint type, on_srv;
  char *newname, *p;
  char *name_utf8;

  if (!current_buddy)
    return;
  bud = BUDDATA(current_buddy);

  bjid   = buddy_getjid(bud);
  group  = buddy_getgroupname(bud);
  type   = buddy_gettype(bud);
  on_srv = buddy_getonserverflag(bud);

  if (type & ROSTER_TYPE_SPECIAL) {
    scr_LogPrint(LPRINT_NORMAL, "You can't rename this item.");
    return;
  }

  if (!*arg && !(type & ROSTER_TYPE_GROUP)) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a new name.");
    return;
  }

  if (!(type & ROSTER_TYPE_GROUP) && !on_srv) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Note: this item will be added to your server roster.");
    // If this is a MUC room w/o bookmark, let's give a small hint...
    if ((type & ROSTER_TYPE_ROOM) && !xmpp_is_bookmarked(bjid)) {
      scr_LogPrint(LPRINT_NORMAL,
                   "You should add a room bookmark or it will not be "
                   "recognized as a MUC room next time you run mcabber.");
    }
  }

  newname = g_strdup(arg);
  // Remove trailing space
  for (p = newname; *p; p++) ;
  while (p > newname && *p == ' ') *p = 0;

  strip_arg_special_chars(newname);

  name_utf8 = to_utf8(newname);

  if (type & ROSTER_TYPE_GROUP) {
    // Rename a whole group
    foreach_group_member(bud, &move_group_member, name_utf8);
    // Let's jump to the previous buddy, because this group name should
    // disappear when we receive the server answer.
    scr_roster_up_down(-1, 1);
  } else {
    // Rename a single buddy
    guint del_name = 0;
    if (!*newname || !strcmp(arg, "-"))
      del_name = TRUE;
    /* We do not rename the buddy right now because the server could reject
     * the request.  Let's wait for the server answer.
     * buddy_setname(bud, (del_name ? (char*)bjid : name_utf8));
     */
    xmpp_updatebuddy(bjid, (del_name ? NULL : name_utf8), group);
  }

  g_free(name_utf8);
  g_free(newname);
  update_roster = TRUE;
}

static void do_move(char *arg)
{
  gpointer bud;
  const char *bjid, *name, *oldgroupname;
  guint type;
  char *newgroupname, *p;
  char *group_utf8;

  if (!current_buddy)
    return;
  bud = BUDDATA(current_buddy);

  bjid = buddy_getjid(bud);
  name = buddy_getname(bud);
  type = buddy_gettype(bud);

  oldgroupname = buddy_getgroupname(bud);

  if (type & ROSTER_TYPE_GROUP) {
    scr_LogPrint(LPRINT_NORMAL, "You can't move groups!");
    return;
  }
  if (type & ROSTER_TYPE_SPECIAL) {
    scr_LogPrint(LPRINT_NORMAL, "You can't move this item.");
    return;
  }

  newgroupname = g_strdup(arg);
  // Remove trailing space
  for (p = newgroupname; *p; p++) ;
  while (p > newgroupname && *p == ' ') *p-- = 0;

  strip_arg_special_chars(newgroupname);

  group_utf8 = to_utf8(newgroupname);
  if (strcmp(oldgroupname, group_utf8)) {
    /* guint msgflag; */

    xmpp_updatebuddy(bjid, name, *group_utf8 ? group_utf8 : NULL);
    scr_roster_up_down(-1, 1);

    /* We do not move the buddy right now because the server could reject
     * the request.  Let's wait for the server answer.

    // If the buddy has a pending message flag,
    // we remove it temporarily in order to reset the global group
    // flag.  We set it back once the buddy is in the new group,
    // which will update the new group's flag.
    msgflag = buddy_getflags(bud) & ROSTER_FLAG_MSG;
    if (msgflag)
      roster_msg_setflag(bjid, FALSE, FALSE);
    buddy_setgroup(bud, group_utf8);
    if (msgflag)
      roster_msg_setflag(bjid, FALSE, TRUE);
    */
  }

  g_free(group_utf8);
  g_free(newgroupname);
  update_roster = TRUE;
}

static void list_option_cb(char *k, char *v, void *f)
{
  if (strcmp(k, "password")) {
    GSList **list = f;
    *list = g_slist_insert_sorted(*list, k, (GCompareFunc)strcmp);
  }
}

static void do_set(char *arg)
{
  guint assign;
  gchar *option, *value;
  gchar *option_utf8;

  if (!*arg) {
    // list all set options
    GSList *list = NULL;
    // Get sorted list of keys
    settings_foreach(SETTINGS_TYPE_OPTION, list_option_cb, &list);
    if (list) {
      gsize max = 0;
      gsize maxmax = scr_gettextwidth() / 3;
      GSList *lel;
      gchar *format;
      // Find out maximum key length
      for (lel = list; lel; lel = lel->next) {
        const gchar *key = lel->data;
        gsize len = strlen(key);
        if (len > max) {
          max = len;
          if (max > maxmax) {
            max = maxmax;
            break;
          }
        }
      }
      // Print out list of options
      format = g_strdup_printf("%%-%us = [%%s]", (unsigned)max);
      for (lel = list; lel; lel = lel->next) {
        const gchar *key = lel->data;
        scr_LogPrint(LPRINT_NORMAL, format, key, settings_opt_get(key));
      }
      g_free(format);
      scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
      scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                 ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
    } else
      scr_LogPrint(LPRINT_NORMAL, "No options found.");
    return;
  }

  assign = parse_assigment(arg, &option, &value);
  if (!option) {
    scr_LogPrint(LPRINT_NORMAL, "Set what option?");
    return;
  }
  option_utf8 = to_utf8(option);
  g_free(option);
  if (!assign) {  // This is a query
    const char *val = settings_opt_get(option_utf8);
    if (val)
      scr_LogPrint(LPRINT_NORMAL, "%s = [%s]", option_utf8, val);
    else
      scr_LogPrint(LPRINT_NORMAL, "Option %s is not set", option_utf8);
    g_free(option_utf8);
    return;
  }
  // Update the option
  // Maybe some options should be protected when user is connected (server,
  // username, etc.).  And we should catch some options here, too
  // (hide_offline_buddies for ex.)
  if (!value) {
    settings_del(SETTINGS_TYPE_OPTION, option_utf8);
  } else {
    gchar *value_utf8 = to_utf8(value);
    settings_set(SETTINGS_TYPE_OPTION, option_utf8, value_utf8);
    g_free(value_utf8);
    g_free(value);
  }
  g_free(option_utf8);
}

static void dump_alias(char *k, char *v, void *param)
{
  scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8, "Alias %s = %s", k, v);
  scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
  scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                 ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
}

static void do_alias(char *arg)
{
  guint assign;
  gchar *alias, *value;

  assign = parse_assigment(arg, &alias, &value);
  if (!alias) {
    settings_foreach(SETTINGS_TYPE_ALIAS, &dump_alias, NULL);
    update_roster = TRUE;
    return;
  }
  if (!assign) {  // This is a query
    const char *val = settings_get(SETTINGS_TYPE_ALIAS, alias);
    // NOTE: LPRINT_NOTUTF8 here, see below why it isn't encoded...
    if (val)
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8, "%s = %s", alias, val);
    else
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "Alias '%s' does not exist", alias);
    goto do_alias_return;
  }
  // Check the alias does not conflict with a registered command
  if (cmd_get(alias)) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "'%s' is a reserved word!", alias);
      goto do_alias_return;
  }
  // Update the alias
  if (!value) {
    if (settings_get(SETTINGS_TYPE_ALIAS, alias)) {
      settings_del(SETTINGS_TYPE_ALIAS, alias);
      // Remove alias from the completion list
      compl_del_category_word(COMPL_CMD, alias);
    }
  } else {
    /* Add alias to the completion list, if not already in.
       NOTE: We're not UTF8-encoding "alias" and "value" here because UTF-8 is
       not yet supported in the UI... (and we use the values in the completion
       system)
    */
    if (!settings_get(SETTINGS_TYPE_ALIAS, alias))
      compl_add_category_word(COMPL_CMD, alias);
    settings_set(SETTINGS_TYPE_ALIAS, alias, value);
    g_free(value);
  }
do_alias_return:
  g_free(alias);
}

static void dump_bind(char *k, char *v, void *param)
{
  scr_LogPrint(LPRINT_NORMAL, "Key %4s is bound to: %s", k, v);
}

static void do_bind(char *arg)
{
  guint assign;
  gchar *k_code, *value;

  assign = parse_assigment(arg, &k_code, &value);
  if (!k_code) {
    settings_foreach(SETTINGS_TYPE_BINDING, &dump_bind, NULL);
    scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
    scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                   ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
    return;
  }
  if (!assign) {  // This is a query
    const char *val = settings_get(SETTINGS_TYPE_BINDING, k_code);
    if (val)
      scr_LogPrint(LPRINT_NORMAL, "Key %s is bound to: %s", k_code, val);
    else
      scr_LogPrint(LPRINT_NORMAL, "Key %s is not bound.", k_code);
    g_free(k_code);
    return;
  }
  // Update the key binding
  if (!value) {
    settings_del(SETTINGS_TYPE_BINDING, k_code);
  } else {
    gchar *value_utf8 = to_utf8(value);
    settings_set(SETTINGS_TYPE_BINDING, k_code, value_utf8);
    g_free(value_utf8);
    g_free(value);
  }
  g_free(k_code);
}

static void do_rawxml(char *arg)
{
  char **paramlst;
  char *subcmd;

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    return;
  }

  paramlst = split_arg(arg, 2, 1); // subcmd, arg
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Please read the manual page"
                 " before using /rawxml :-)");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(subcmd, "send"))  {
    gchar *buffer;

    if (!subcmd || !*subcmd) {
      scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
      free_arg_lst(paramlst);
      return;
    }

    // We don't strip_arg_special_chars() here, because it would be a pain for
    // the user to escape quotes in a XML stream...

    buffer = to_utf8(arg);
    if (buffer) {
      scr_LogPrint(LPRINT_NORMAL, "Sending XML string");
      lm_connection_send_raw(lconnection, buffer, NULL);
      g_free(buffer);
    } else {
      scr_LogPrint(LPRINT_NORMAL, "Conversion error in XML string.");
    }
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  }

  free_arg_lst(paramlst);
}

//  check_room_subcommand(arg, param_needed, buddy_must_be_a_room)
// - Check if this is a room, if buddy_must_be_a_room is not null
// - Check there is at least 1 parameter, if param_needed is true
// - Return null if one of the checks fails, or a pointer to the first
//   non-space character.
static char *check_room_subcommand(char *arg, bool param_needed,
                                   gpointer buddy_must_be_a_room)
{
  if (buddy_must_be_a_room &&
      !(buddy_gettype(buddy_must_be_a_room) & ROSTER_TYPE_ROOM)) {
    scr_LogPrint(LPRINT_NORMAL, "This isn't a conference room.");
    return NULL;
  }

  if (param_needed) {
    if (!arg) {
      scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
      return NULL;
    }
  }

  if (arg)
    return arg;
  else
    return "";
}

static void room_join(gpointer bud, char *arg)
{
  char **paramlst;
  char *roomname, *nick, *pass;
  char *roomname_tmp = NULL;
  char *pass_utf8;

  paramlst = split_arg(arg, 3, 0); // roomid, nickname, password
  roomname = *paramlst;
  nick = *(paramlst+1);
  pass = *(paramlst+2);

  if (!roomname)
    nick = NULL;
  if (!nick)
    pass = NULL;

  if (!roomname || !strcmp(roomname, ".")) {
    // If the current_buddy is recognized as a room, the room name
    // can be omitted (or "." can be used).
    if (!bud || !(buddy_gettype(bud) & ROSTER_TYPE_ROOM)) {
      scr_LogPrint(LPRINT_NORMAL, "Please specify a room name.");
      free_arg_lst(paramlst);
      return;
    }
    roomname = (char*)buddy_getjid(bud);
  } else if (strchr(roomname, '/')) {
    scr_LogPrint(LPRINT_NORMAL, "Invalid room name.");
    free_arg_lst(paramlst);
    return;
  } else {
    // The room id has been specified.  Let's convert it and use it.
    mc_strtolower(roomname);
    roomname = roomname_tmp = to_utf8(roomname);
  }

  // If no nickname is provided with the /join command,
  // we try to get a default nickname.
  if (!nick || !*nick)
    nick = default_muc_nickname(roomname);
  else
    nick = to_utf8(nick);
  // If we still have no nickname, give up
  if (!nick || !*nick) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a nickname.");
    g_free(nick);
    free_arg_lst(paramlst);
    return;
  }

  pass_utf8 = to_utf8(pass);

  xmpp_room_join(roomname, nick, pass_utf8);

  scr_LogPrint(LPRINT_LOGNORM, "Sent a join request to <%s>...", roomname);

  g_free(roomname_tmp);
  g_free(nick);
  g_free(pass_utf8);
  buddylist_build();
  update_roster = TRUE;
  free_arg_lst(paramlst);
}

static void room_invite(gpointer bud, char *arg)
{
  char **paramlst;
  const gchar *roomname;
  char* fjid;
  gchar *reason_utf8;

  paramlst = split_arg(arg, 2, 1); // jid, [reason]
  fjid = *paramlst;
  arg = *(paramlst+1);
  // An empty reason is no reason...
  if (arg && !*arg)
    arg = NULL;

  if (!fjid || !*fjid) {
    scr_LogPrint(LPRINT_NORMAL, "Missing or incorrect Jabber ID.");
    free_arg_lst(paramlst);
    return;
  }

  roomname = buddy_getjid(bud);
  reason_utf8 = to_utf8(arg);
  xmpp_room_invite(roomname, fjid, reason_utf8);
  scr_LogPrint(LPRINT_LOGNORM, "Invitation sent to <%s>.", fjid);
  g_free(reason_utf8);
  free_arg_lst(paramlst);
}

static void room_affil(gpointer bud, char *arg)
{
  char **paramlst;
  gchar *fjid, *rolename;
  struct role_affil ra;
  const char *roomid = buddy_getjid(bud);

  paramlst = split_arg(arg, 3, 1); // jid, new_affil, [reason]
  fjid = *paramlst;
  rolename = *(paramlst+1);
  arg = *(paramlst+2);

  if (!fjid || !*fjid || !rolename || !*rolename) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify both a Jabber ID and a role.");
    free_arg_lst(paramlst);
    return;
  }

  ra.type = type_affil;
  ra.val.affil = affil_none;
  for (; ra.val.affil < imaffiliation_size; ra.val.affil++)
    if (!strcasecmp(rolename, straffil[ra.val.affil]))
      break;

  if (ra.val.affil < imaffiliation_size) {
    gchar *jid_utf8, *reason_utf8;
    jid_utf8 = to_utf8(fjid);
    reason_utf8 = to_utf8(arg);
    xmpp_room_setattrib(roomid, jid_utf8, NULL, ra, reason_utf8);
    g_free(jid_utf8);
    g_free(reason_utf8);
  } else
    scr_LogPrint(LPRINT_NORMAL, "Wrong affiliation parameter.");

  free_arg_lst(paramlst);
}

static void room_role(gpointer bud, char *arg)
{
  char **paramlst;
  gchar *fjid, *rolename;
  struct role_affil ra;
  const char *roomid = buddy_getjid(bud);

  paramlst = split_arg(arg, 3, 1); // jid, new_role, [reason]
  fjid = *paramlst;
  rolename = *(paramlst+1);
  arg = *(paramlst+2);

  if (!fjid || !*fjid || !rolename || !*rolename) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify both a Jabber ID and a role.");
    free_arg_lst(paramlst);
    return;
  }

  ra.type = type_role;
  ra.val.role = role_none;
  for (; ra.val.role < imrole_size; ra.val.role++)
    if (!strcasecmp(rolename, strrole[ra.val.role]))
      break;

  if (ra.val.role < imrole_size) {
    gchar *jid_utf8, *reason_utf8;
    jid_utf8 = to_utf8(fjid);
    reason_utf8 = to_utf8(arg);
    xmpp_room_setattrib(roomid, jid_utf8, NULL, ra, reason_utf8);
    g_free(jid_utf8);
    g_free(reason_utf8);
  } else
    scr_LogPrint(LPRINT_NORMAL, "Wrong role parameter.");

  free_arg_lst(paramlst);
}


// The expected argument is a Jabber id
static void room_ban(gpointer bud, char *arg)
{
  char **paramlst;
  gchar *fjid, *bjid;
  const gchar *banjid;
  gchar *jid_utf8, *reason_utf8;
  struct role_affil ra;
  const char *roomid = buddy_getjid(bud);

  paramlst = split_arg(arg, 2, 1); // jid, [reason]
  fjid = *paramlst;
  arg = *(paramlst+1);

  if (!fjid || !*fjid) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a Jabber ID.");
    free_arg_lst(paramlst);
    return;
  }

  ra.type = type_affil;
  ra.val.affil = affil_outcast;

  bjid = jidtodisp(fjid);
  jid_utf8 = to_utf8(bjid);

  // If the argument doesn't look like a jid, we'll try to find a matching
  // nickname.
  if (!strchr(bjid, JID_DOMAIN_SEPARATOR) || check_jid_syntax(bjid)) {
    const gchar *tmp;
    // We want the initial argument, so the fjid variable, because
    // we don't want to strip a resource-like string from the nickname!
    g_free(jid_utf8);
    jid_utf8 = to_utf8(fjid);
    tmp = buddy_getrjid(bud, jid_utf8);
    if (!tmp) {
      scr_LogPrint(LPRINT_NORMAL, "Wrong JID or nickname");
      goto room_ban_return;
    }
    banjid = jidtodisp(tmp);
  } else
    banjid = jid_utf8;

  scr_LogPrint(LPRINT_NORMAL, "Requesting a ban for %s", banjid);

  reason_utf8 = to_utf8(arg);
  xmpp_room_setattrib(roomid, banjid, NULL, ra, reason_utf8);
  g_free(reason_utf8);

room_ban_return:
  g_free(bjid);
  g_free(jid_utf8);
  free_arg_lst(paramlst);
}

// The expected argument is a Jabber id
static void room_unban(gpointer bud, char *arg)
{
  gchar *fjid = arg;
  gchar *jid_utf8;
  struct role_affil ra;
  const char *roomid = buddy_getjid(bud);

  if (!fjid || !*fjid) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a Jabber ID.");
    return;
  }

  ra.type = type_affil;
  ra.val.affil = affil_none;

  jid_utf8 = to_utf8(fjid);
  xmpp_room_setattrib(roomid, jid_utf8, NULL, ra, NULL);
  g_free(jid_utf8);
}

// The expected argument is a nickname
static void room_kick(gpointer bud, char *arg)
{
  char **paramlst;
  gchar *nick;
  gchar *nick_utf8, *reason_utf8;
  struct role_affil ra;
  const char *roomid = buddy_getjid(bud);

  paramlst = split_arg(arg, 2, 1); // nickname, [reason]
  nick = *paramlst;
  arg = *(paramlst+1);

  if (!nick || !*nick) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a nickname.");
    free_arg_lst(paramlst);
    return;
  }

  ra.type = type_role;
  ra.val.affil = role_none;

  nick_utf8 = to_utf8(nick);
  reason_utf8 = to_utf8(arg);
  xmpp_room_setattrib(roomid, NULL, nick_utf8, ra, reason_utf8);
  g_free(nick_utf8);
  g_free(reason_utf8);

  free_arg_lst(paramlst);
}

void cmd_room_leave(gpointer bud, char *arg)
{
  gchar *roomid, *desc;
  const char *nickname;

  nickname = buddy_getnickname(bud);
  if (!nickname) {
    scr_LogPrint(LPRINT_NORMAL, "You are not in this room.");
    return;
  }

  roomid = g_strdup_printf("%s/%s", buddy_getjid(bud), nickname);
  desc = to_utf8(arg);
  xmpp_setstatus(offline, roomid, desc, TRUE);
  g_free(desc);
  g_free(roomid);
}

static void room_nick(gpointer bud, char *arg)
{
  if (!buddy_getinsideroom(bud)) {
    scr_LogPrint(LPRINT_NORMAL, "You are not in this room.");
    return;
  }

  if (!arg || !*arg) {
    const char *nick = buddy_getnickname(bud);
    if (nick)
      scr_LogPrint(LPRINT_NORMAL, "Your nickname is: %s", nick);
    else
      scr_LogPrint(LPRINT_NORMAL, "You have no nickname in this room.");
  } else {
    gchar *nick = to_utf8(arg);
    strip_arg_special_chars(nick);
    xmpp_room_join(buddy_getjid(bud), nick, NULL);
    g_free(nick);
  }
}

static void room_privmsg(gpointer bud, char *arg)
{
  char **paramlst;
  gchar *fjid_utf8, *nick, *nick_utf8, *msg;

  paramlst = split_arg(arg, 2, 1); // nickname, message
  nick = *paramlst;
  arg = *(paramlst+1);

  if (!nick || !*nick || !arg || !*arg) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Please specify both a Jabber ID and a message.");
    free_arg_lst(paramlst);
    return;
  }

  nick_utf8 = to_utf8(nick);
  fjid_utf8 = g_strdup_printf("%s/%s", buddy_getjid(bud), nick_utf8);
  g_free (nick_utf8);
  msg = to_utf8(arg);
  send_message_to(fjid_utf8, msg, NULL, LM_MESSAGE_SUB_TYPE_NOT_SET, FALSE);
  g_free(fjid_utf8);
  g_free(msg);
  free_arg_lst(paramlst);
}

static void room_remove(gpointer bud, char *arg)
{
  if (*arg) {
    scr_LogPrint(LPRINT_NORMAL, "This action does not require a parameter; "
                 "the currently-selected room will be removed.");
    return;
  }

  // Quick check: if there are resources, we haven't left
  if (buddy_getinsideroom(bud)) {
    scr_LogPrint(LPRINT_NORMAL, "You haven't left this room!");
    return;
  }
  // Delete the room
  roster_del_user(buddy_getjid(bud));
  scr_update_buddy_window();
  buddylist_build();
  update_roster = TRUE;
}

static void room_topic(gpointer bud, char *arg)
{
  if (!buddy_getinsideroom(bud)) {
    scr_LogPrint(LPRINT_NORMAL, "You are not in this room.");
    return;
  }

  // If no parameter is given, display the current topic
  if (!*arg) {
    const char *topic = buddy_gettopic(bud);
    if (topic)
      scr_LogPrint(LPRINT_NORMAL, "Topic: %s", topic);
    else
      scr_LogPrint(LPRINT_NORMAL, "No topic has been set.");
    return;
  }

  // If arg is "-", let's clear the topic
  if (!strcmp(arg, "-"))
    arg = NULL;

  arg = to_utf8(arg);
  // Set the topic
  xmpp_send_msg(buddy_getjid(bud), NULL, ROSTER_TYPE_ROOM, arg ? arg : "",
                FALSE, NULL, LM_MESSAGE_SUB_TYPE_NOT_SET, NULL);
  g_free(arg);
}

static void room_destroy(gpointer bud, char *arg)
{
  gchar *msg;

  if (arg && *arg)
    msg = to_utf8(arg);
  else
    msg = NULL;

  xmpp_room_destroy(buddy_getjid(bud), NULL, msg);
  g_free(msg);
}

static void room_unlock(gpointer bud, char *arg)
{
  if (*arg) {
    scr_LogPrint(LPRINT_NORMAL, "Unknown parameter.");
    return;
  }

  xmpp_room_unlock(buddy_getjid(bud));
}

static void room_setopt(gpointer bud, char *arg)
{
  char **paramlst;
  char *param, *value;
  enum { opt_none = 0, opt_printstatus, opt_autowhois } option = 0;

  paramlst = split_arg(arg, 2, 1); // param, value
  param = *paramlst;
  value = *(paramlst+1);
  if (!param) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a room option.");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(param, "print_status"))
    option = opt_printstatus;
  else if (!strcasecmp(param, "auto_whois"))
    option = opt_autowhois;
  else {
    scr_LogPrint(LPRINT_NORMAL, "Wrong option!");
    free_arg_lst(paramlst);
    return;
  }

  // If no value is given, display the current value
  if (!value) {
    const char *strval;
    if (option == opt_printstatus)
      strval = strprintstatus[buddy_getprintstatus(bud)];
    else
      strval = strautowhois[buddy_getautowhois(bud)];
    scr_LogPrint(LPRINT_NORMAL, "%s is set to: %s", param, strval);
    free_arg_lst(paramlst);
    return;
  }

  if (option == opt_printstatus) {
    enum room_printstatus eval;
    if (!strcasecmp(value, "none"))
      eval = status_none;
    else if (!strcasecmp(value, "in_and_out"))
      eval = status_in_and_out;
    else if (!strcasecmp(value, "all"))
      eval = status_all;
    else {
      eval = status_default;
      if (strcasecmp(value, "default") != 0)
        scr_LogPrint(LPRINT_NORMAL, "Unrecognized value, assuming default...");
    }
    buddy_setprintstatus(bud, eval);
  } else if (option == opt_autowhois) {
    enum room_autowhois eval;
    if (!strcasecmp(value, "on"))
      eval = autowhois_on;
    else if (!strcasecmp(value, "off"))
      eval = autowhois_off;
    else {
      eval = autowhois_default;
      if (strcasecmp(value, "default") != 0)
        scr_LogPrint(LPRINT_NORMAL, "Unrecognized value, assuming default...");
    }
    buddy_setautowhois(bud, eval);
  }

  free_arg_lst(paramlst);
}

//  cmd_room_whois(..)
// If interactive is TRUE, chatmode can be enabled.
// Please note that usernick is expected in UTF-8 locale iff interactive is
// FALSE (in order to work correctly with auto_whois).
void cmd_room_whois(gpointer bud, const char *usernick, guint interactive)
{
  char **paramlst = NULL;
  gchar *nick, *buffer;
  const char *bjid, *realjid;
  const char *rst_msg;
  gchar rprio;
  enum imstatus rstatus;
  enum imrole role;
  enum imaffiliation affil;
  time_t rst_time;
  guint msg_flag = HBB_PREFIX_INFO;

  if (interactive) {
    paramlst = split_arg(usernick, 1, 0); // nickname
    nick = to_utf8(*paramlst);
  } else {
    nick = g_strdup(usernick);
  }

  if (!nick || !*nick) {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a nickname.");
    if (paramlst)
      free_arg_lst(paramlst);
    return;
  }

  if (interactive) {
    // Enter chat mode
    scr_set_chatmode(TRUE);
    scr_show_buddy_window();
  } else
    msg_flag |= HBB_PREFIX_NOFLAG;

  bjid = buddy_getjid(bud);
  rstatus = buddy_getstatus(bud, nick);

  if (rstatus == offline) {
    scr_LogPrint(LPRINT_NORMAL, "No such member: %s", nick);
    if (paramlst)
      free_arg_lst(paramlst);
    g_free(nick);
    return;
  }

  rst_time = buddy_getstatustime(bud, nick);
  rprio   = buddy_getresourceprio(bud, nick);
  rst_msg = buddy_getstatusmsg(bud, nick);
  if (!rst_msg) rst_msg = "";

  role = buddy_getrole(bud, nick);
  affil = buddy_getaffil(bud, nick);
  realjid = buddy_getrjid(bud, nick);

  buffer = g_new(char, 4096);

  snprintf(buffer, 4095, "Whois [%s]", nick);
  scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag, 0);
  snprintf(buffer, 4095, "Status   : [%c] %s", imstatus2char[rstatus],
           rst_msg);
  scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag | HBB_PREFIX_CONT, 0);

  if (rst_time) {
    char tbuf[128];

    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&rst_time));
    snprintf(buffer, 127, "Timestamp: %s", tbuf);
    scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag | HBB_PREFIX_CONT, 0);
  }

  if (realjid) {
    snprintf(buffer, 4095, "JID      : <%s>", realjid);
    scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag | HBB_PREFIX_CONT, 0);
  }

  snprintf(buffer, 4095, "Role     : %s", strrole[role]);
  scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag | HBB_PREFIX_CONT, 0);
  snprintf(buffer, 4095, "Affiliat.: %s", straffil[affil]);
  scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag | HBB_PREFIX_CONT, 0);
  snprintf(buffer, 4095, "Priority : %d", rprio);
  scr_WriteIncomingMessage(bjid, buffer, 0, msg_flag | HBB_PREFIX_CONT, 0);

  scr_WriteIncomingMessage(bjid, "End of WHOIS", 0, msg_flag, 0);

  g_free(buffer);
  g_free(nick);
  if (paramlst)
    free_arg_lst(paramlst);
}

static void room_bookmark(gpointer bud, char *arg)
{
  const char *roomid;
  const char *name = NULL, *nick = NULL;
  char *tmpnick = NULL;
  enum room_autowhois autowhois = 0;
  enum room_printstatus printstatus = 0;
  enum { bm_add = 0, bm_del = 1 } action = 0;
  int autojoin = 0;
  int nick_set = 0;

  if (arg && *arg) {
    // /room bookmark [add|del] [[+|-]autojoin] [-|nick]
    char **paramlst;
    char **pp;

    paramlst = split_arg(arg, 3, 0); // At most 3 parameters
    for (pp = paramlst; *pp; pp++) {
      if (!strcasecmp(*pp, "add"))
        action = bm_add;
      else if (!strcasecmp(*pp, "del"))
        action = bm_del;
      else if (!strcasecmp(*pp, "-autojoin"))
        autojoin = 0;
      else if (!strcasecmp(*pp, "+autojoin") || !strcasecmp(*pp, "autojoin"))
        autojoin = 1;
      else if (!strcmp(*pp, "-"))
        nick_set = 1;
      else {
        nick_set = 1;
        nick = tmpnick = to_utf8 (*pp);
      }
    }
    free_arg_lst(paramlst);
  }

  roomid = buddy_getjid(bud);

  if (action == bm_add) {
    name = buddy_getname(bud);
    if (!nick_set)
      nick = buddy_getnickname(bud);
    printstatus = buddy_getprintstatus(bud);
    autowhois   = buddy_getautowhois(bud);
  }

  xmpp_set_storage_bookmark(roomid, name, nick, NULL, autojoin,
                            printstatus, autowhois);
  g_free (tmpnick);
}

static void display_all_bookmarks(void)
{
  GSList *bm, *bmp;
  GString *sbuf;
  struct bookmark *bm_elt;

  bm = xmpp_get_all_storage_bookmarks();

  if (!bm)
    return;

  sbuf = g_string_new("");

  scr_WriteIncomingMessage(NULL, "List of MUC bookmarks:",
                           0, HBB_PREFIX_INFO, 0);

  for (bmp = bm; bmp; bmp = g_slist_next(bmp)) {
    bm_elt = bmp->data;
    g_string_printf(sbuf, "%c <%s>",
                    (bm_elt->autojoin ? '*' : ' '), bm_elt->roomjid);
    if (bm_elt->nick)
      g_string_append_printf(sbuf, " (%s)", bm_elt->nick);
    if (bm_elt->name)
      g_string_append_printf(sbuf, " %s", bm_elt->name);
    g_free(bm_elt->roomjid);
    g_free(bm_elt->name);
    g_free(bm_elt->nick);
    g_free(bm_elt);
    scr_WriteIncomingMessage(NULL, sbuf->str,
                             0, HBB_PREFIX_INFO | HBB_PREFIX_CONT, 0);
  }

  scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
  scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                 ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
  g_string_free(sbuf, TRUE);
  g_slist_free(bm);
}

static void do_module(char *arg)
{
#ifdef MODULES_ENABLE
  gboolean force = FALSE;
  char **args;

  args = split_arg(arg, 2, 0);
  if (!args[0] || !strcmp(args[0], "list")) {
    module_list_print();
  } else {
    const gchar *error = NULL;
    const gchar *name = args[1];

    if (name && name[0] == '-' && name[1] == 'f') {
      force = TRUE;
      name +=2;
      while (*name && *name == ' ')
        ++name;
    }

    if (!strcmp(args[0], "load"))
      error = module_load(name, TRUE, force);
    else if (!strcmp(args[0], "unload"))
      error = module_unload(name, TRUE, force);
    else if (!strcmp(args[0], "info"))
      module_info_print(name);
    else
      error = "Unknown subcommand";
    if (error)
      scr_LogPrint(LPRINT_LOGNORM, "Error: %s.",  error);
  }
  free_arg_lst(args);
#else
  scr_log_print(LPRINT_NORMAL,
                "Please recompile mcabber with modules enabled.");
#endif
}

static void do_room(char *arg)
{
  char **paramlst;
  char *subcmd;
  gpointer bud;

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    return;
  }

  paramlst = split_arg(arg, 2, 1); // subcmd, arg
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    free_arg_lst(paramlst);
    return;
  }

  if (current_buddy) {
    bud = BUDDATA(current_buddy);
  } else {
    if (strcasecmp(subcmd, "join")) {
      free_arg_lst(paramlst);
      return;
    }
    // "room join" is a special case, we don't need to have a valid
    // current_buddy.
    bud = NULL;
  }

  if (!strcasecmp(subcmd, "join"))  {
    if ((arg = check_room_subcommand(arg, FALSE, NULL)) != NULL)
      room_join(bud, arg);
  } else if (!strcasecmp(subcmd, "invite"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_invite(bud, arg);
  } else if (!strcasecmp(subcmd, "affil"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_affil(bud, arg);
  } else if (!strcasecmp(subcmd, "role"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_role(bud, arg);
  } else if (!strcasecmp(subcmd, "ban"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_ban(bud, arg);
  } else if (!strcasecmp(subcmd, "unban"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_unban(bud, arg);
  } else if (!strcasecmp(subcmd, "kick"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_kick(bud, arg);
  } else if (!strcasecmp(subcmd, "leave"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      cmd_room_leave(bud, arg);
  } else if (!strcasecmp(subcmd, "names"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_names(bud, arg);
  } else if (!strcasecmp(subcmd, "nick"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_nick(bud, arg);
  } else if (!strcasecmp(subcmd, "privmsg"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      room_privmsg(bud, arg);
  } else if (!strcasecmp(subcmd, "remove"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_remove(bud, arg);
  } else if (!strcasecmp(subcmd, "destroy"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_destroy(bud, arg);
  } else if (!strcasecmp(subcmd, "unlock"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_unlock(bud, arg);
  } else if (!strcasecmp(subcmd, "setopt"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_setopt(bud, arg);
  } else if (!strcasecmp(subcmd, "topic"))  {
    if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_topic(bud, arg);
  } else if (!strcasecmp(subcmd, "whois"))  {
    if ((arg = check_room_subcommand(arg, TRUE, bud)) != NULL)
      cmd_room_whois(bud, arg, TRUE);
  } else if (!strcasecmp(subcmd, "bookmark"))  {
    if (!arg && !buddy_getjid(BUDDATA(current_buddy)) &&
        buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_SPECIAL)
      display_all_bookmarks();
    else if ((arg = check_room_subcommand(arg, FALSE, bud)) != NULL)
      room_bookmark(bud, arg);
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  }

  free_arg_lst(paramlst);
}

static void do_authorization(char *arg)
{
  char **paramlst;
  char *subcmd;
  char *jid_utf8;

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    return;
  }

  paramlst = split_arg(arg, 2, 0); // subcmd, [jid]
  subcmd = *paramlst;
  arg = *(paramlst+1);

  if (!subcmd || !*subcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter.");
    goto do_authorization_return;
  }

  // Use the provided jid, if it looks valid
  if (arg) {
    if (!*arg) {
      // If no jid is provided, we use the current selected buddy
      arg = NULL;
    } else {
      if (check_jid_syntax(arg)) {
        scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                     "<%s> is not a valid Jabber ID.", arg);
        goto do_authorization_return;
      }
    }
  }

  if (!arg) {       // Use the current selected buddy's jid
    gpointer bud;
    guint type;

    if (!current_buddy)
      goto do_authorization_return;
    bud = BUDDATA(current_buddy);

    jid_utf8 = arg  = (char*)buddy_getjid(bud);
    type = buddy_gettype(bud);

    if (!(type & (ROSTER_TYPE_USER|ROSTER_TYPE_AGENT))) {
      scr_LogPrint(LPRINT_NORMAL, "Invalid buddy.");
      goto do_authorization_return;
    }
  } else {
    jid_utf8 = to_utf8(arg);
  }

  if (!strcasecmp(subcmd, "allow"))  {
    xmpp_send_s10n(jid_utf8, LM_MESSAGE_SUB_TYPE_SUBSCRIBED);
    scr_LogPrint(LPRINT_LOGNORM,
                 "Sent presence subscription approval to <%s>.",
                 jid_utf8);
  } else if (!strcasecmp(subcmd, "cancel"))  {
    xmpp_send_s10n(jid_utf8, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);
    scr_LogPrint(LPRINT_LOGNORM,
                 "<%s> will no longer receive your presence updates.",
                 jid_utf8);
  } else if (!strcasecmp(subcmd, "request"))  {
    xmpp_send_s10n(jid_utf8, LM_MESSAGE_SUB_TYPE_SUBSCRIBE);
    scr_LogPrint(LPRINT_LOGNORM,
                 "Sent presence notification request to <%s>.", jid_utf8);
  } else if (!strcasecmp(subcmd, "request_unsubscribe"))  {
    xmpp_send_s10n(jid_utf8, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE);
    scr_LogPrint(LPRINT_LOGNORM,
                 "Sent presence notification unsubscription request to <%s>.",
                 jid_utf8);
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  }

  // Only free jid_utf8 if it has been allocated, i.e. if != arg.
  if (jid_utf8 && jid_utf8 != arg)
    g_free(jid_utf8);
do_authorization_return:
  free_arg_lst(paramlst);
}

static void do_version(char *arg)
{
  gchar *ver = mcabber_version();
  scr_LogPrint(LPRINT_NORMAL, "This is mcabber version %s.", ver);
  g_free(ver);
#ifdef MODULES_ENABLE
  scr_LogPrint(LPRINT_NORMAL, "Compiled with modules support (API %s:%d-%d).",
         MCABBER_BRANCH, MCABBER_API_MIN, MCABBER_API_VERSION);
# ifdef PKGLIB_DIR
  scr_LogPrint(LPRINT_NORMAL, " Modules directory: " PKGLIB_DIR);
# endif
#endif
}

static void do_request(char *arg)
{
  char **paramlst;
  char *fjid, *type;
  enum iqreq_type numtype = iqreq_none;
  char *jid_utf8 = NULL;

  paramlst = split_arg(arg, 2, 0); // type, jid
  type = *paramlst;
  fjid = *(paramlst+1);

  if (type) {
    // Quick check...
    if (!strcasecmp(type, "version"))
      numtype = iqreq_version;
    else if (!strcasecmp(type, "time"))
      numtype = iqreq_time;
    else if (!strcasecmp(type, "last"))
      numtype = iqreq_last;
    else if (!strcasecmp(type, "ping"))
      numtype = iqreq_ping;
    else if (!strcasecmp(type, "vcard"))
      numtype = iqreq_vcard;
  }

  if (!type || !numtype) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Please specify a query type (version, time...).");
    free_arg_lst(paramlst);
    return;
  }

  if (!xmpp_is_online()) {
    scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    free_arg_lst(paramlst);
    return;
  }

  // Allow special jid "" or "." (current buddy)
  if (fjid && (!*fjid || !strcmp(fjid, ".")))
    fjid = NULL;

  if (fjid) {
    // The JID has been specified.  Quick check...
    if (check_jid_syntax(fjid)) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "<%s> is not a valid Jabber ID.", fjid);
      fjid = NULL;
    } else {
      // Convert jid to lowercase
      char *p;
      for (p = fjid; *p && *p != JID_RESOURCE_SEPARATOR; p++)
        *p = tolower(*p);
      fjid = jid_utf8 = to_utf8(fjid);
    }
  } else {
    // Add the current buddy
    if (current_buddy)
      fjid = (char*)buddy_getjid(BUDDATA(current_buddy));
    if (!fjid)
      scr_LogPrint(LPRINT_NORMAL, "Please specify a Jabber ID.");
  }

  if (fjid) {
    switch (numtype) {
      case iqreq_vcard:
          { // vCards requests are sent to the bare jid, except in MUC rooms
            gchar *tmp = strchr(fjid, JID_RESOURCE_SEPARATOR);
            if (tmp) {
              gchar *bjid = jidtodisp(fjid);
              if (!roster_find(bjid, jidsearch, ROSTER_TYPE_ROOM))
                *tmp = '\0';
              g_free(bjid);
            }
          }
      case iqreq_version:
      case iqreq_time:
      case iqreq_last:
      case iqreq_ping:
          xmpp_request(fjid, numtype);
          break;
      default:
          break;
    }
  }
  g_free(jid_utf8);
  free_arg_lst(paramlst);
}

static void do_event(char *arg)
{
  char **paramlst;
  char *evid, *subcmd;
  int action = -1;

  paramlst = split_arg(arg, 3, 1); // id, subcmd, optional arg
  evid = *paramlst;
  subcmd = *(paramlst+1);

  if (!evid || !subcmd) {
    // Special case: /event list
    if (evid && !strcasecmp(evid, "list"))
      evs_display_list();
    else
      scr_LogPrint(LPRINT_NORMAL,
                   "Missing parameter.  Usage: /event num action "
                   "[event-specific args]");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(subcmd, "reject"))
    action = EVS_CONTEXT_REJECT;
  else if (!strcasecmp(subcmd, "accept"))
    action = EVS_CONTEXT_ACCEPT;
  else if (!strcasecmp(subcmd, "ignore"))
    action = EVS_CONTEXT_CANCEL;

  if (action == -1) {
    scr_LogPrint(LPRINT_NORMAL, "Wrong action parameter.");
  } else {
    GSList *p;
    GSList *evidlst;

    if (!strcmp(evid, "*")) {
      // Use completion list
      evidlst = evs_geteventslist();
    } else {
      // Let's create a slist with the provided event id
      evidlst = g_slist_append(NULL, evid);
    }
    for (p = evidlst; p; p = g_slist_next(p)) {
      if (evs_callback(p->data, action,
                       (const char*)(paramlst+2)) == -1) {
        scr_LogPrint(LPRINT_NORMAL, "Event %s not found.",
                     (const char *)p->data);
      }
    }
    g_slist_free(evidlst);
  }

  free_arg_lst(paramlst);
}

static void do_pgp(char *arg)
{
  char **paramlst;
  char *fjid, *subcmd, *keyid;
  enum {
    pgp_none,
    pgp_enable,
    pgp_disable,
    pgp_setkey,
    pgp_force,
    pgp_info
  } op = 0;
  int force = FALSE;

  paramlst = split_arg(arg, 3, 0); // subcmd, jid, [key]
  subcmd = *paramlst;
  fjid = *(paramlst+1);
  keyid = *(paramlst+2);

  if (!subcmd)
    fjid = NULL;
  if (!fjid)
    keyid = NULL;

  if (subcmd) {
    if (!strcasecmp(subcmd, "enable"))
      op = pgp_enable;
    else if (!strcasecmp(subcmd, "disable"))
      op = pgp_disable;
    else if (!strcasecmp(subcmd, "setkey"))
      op = pgp_setkey;
    else if ((!strcasecmp(subcmd, "force")) ||
             (!strcasecmp(subcmd, "+force"))) {
      op = pgp_force;
      force = TRUE;
    } else if (!strcasecmp(subcmd, "-force"))
      op = pgp_force;
    else if (!strcasecmp(subcmd, "info"))
      op = pgp_info;
  }

  if (!op) {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized or missing parameter!");
    free_arg_lst(paramlst);
    return;
  }

  // Allow special jid "" or "." (current buddy)
  if (fjid && (!*fjid || !strcmp(fjid, ".")))
    fjid = NULL;

  if (fjid) {
    // The JID has been specified.  Quick check...
    if (check_jid_syntax(fjid) || !strchr(fjid, '@')) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "<%s> is not a valid Jabber ID.", fjid);
      fjid = NULL;
    } else {
      // Convert jid to lowercase and strip resource
      char *p;
      for (p = fjid; *p && *p != JID_RESOURCE_SEPARATOR; p++)
        *p = tolower(*p);
      if (*p == JID_RESOURCE_SEPARATOR)
        *p = '\0';
    }
  } else {
    gpointer bud = NULL;
    if (current_buddy)
      bud = BUDDATA(current_buddy);
    if (bud) {
      guint type = buddy_gettype(bud);
      if (type & ROSTER_TYPE_USER)  // Is it a user?
        fjid = (char*)buddy_getjid(bud);
      else
        scr_LogPrint(LPRINT_NORMAL, "The selected item should be a user.");
    }
  }

  if (fjid) { // fjid is actually a bare jid...
    guint disabled;
    GString *sbuf;
    switch (op) {
      case pgp_enable:
      case pgp_disable:
          settings_pgp_setdisabled(fjid, (op == pgp_disable ? TRUE : FALSE));
          break;
      case pgp_force:
          settings_pgp_setforce(fjid, force);
          break;
      case pgp_setkey:
          settings_pgp_setkeyid(fjid, keyid);
          break;
      case pgp_info:
          sbuf = g_string_new("");
          if (settings_pgp_getkeyid(fjid)) {
            g_string_printf(sbuf, "PGP Encryption key id: %s",
                            settings_pgp_getkeyid(fjid));
            scr_WriteIncomingMessage(fjid, sbuf->str, 0, HBB_PREFIX_INFO, 0);
          }
          disabled = settings_pgp_getdisabled(fjid);
          g_string_printf(sbuf, "PGP encryption is %s",
                          (disabled ?  "disabled" : "enabled"));
          scr_WriteIncomingMessage(fjid, sbuf->str, 0, HBB_PREFIX_INFO, 0);
          if (!disabled && settings_pgp_getforce(fjid)) {
            scr_WriteIncomingMessage(fjid,
                                     "Encryption enforced (no negotiation)",
                                     0, HBB_PREFIX_INFO, 0);
          }
          g_string_free(sbuf, TRUE);
          break;
      default:
          break;
    }
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Please specify a valid Jabber ID.");
  }

  free_arg_lst(paramlst);
}

static void do_otr(char *arg)
{
#ifdef HAVE_LIBOTR
  char **paramlst;
  char *fjid, *subcmd, *keyid;
  enum {
    otr_none,
    otr_start,
    otr_stop,
    otr_fpr,
    otr_smpq,
    otr_smpr,
    otr_smpa,
    otr_k,
    otr_info
  } op = 0;

  if (!otr_enabled()) {
    scr_LogPrint(LPRINT_LOGNORM,
                 "Warning: OTR hasn't been enabled -- command ignored.");
    return;
  }

  paramlst = split_arg(arg, 3, 0); // subcmd, jid, [key]
  subcmd = *paramlst;
  fjid = *(paramlst+1);
  keyid = *(paramlst+2);

  if (!subcmd)
    fjid = NULL;
  if (!fjid)
    keyid = NULL;

  if (subcmd) {
    if (!strcasecmp(subcmd, "start"))
      op = otr_start;
    else if (!strcasecmp(subcmd, "stop"))
      op = otr_stop;
    else if (!strcasecmp(subcmd, "fingerprint"))
      op = otr_fpr;
    else if (!strcasecmp(subcmd, "smpq"))
      op = otr_smpq;
    else if (!strcasecmp(subcmd, "smpr"))
      op = otr_smpr;
    else if (!strcasecmp(subcmd, "smpa"))
      op = otr_smpa;
    else if (!strcasecmp(subcmd, "key"))
      op = otr_k;
    else if (!strcasecmp(subcmd, "info"))
      op = otr_info;
  }

  if (!op) {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized or missing parameter!");
    free_arg_lst(paramlst);
    return;
  }

  if (op == otr_k)
    otr_key();
  else {
    // Allow special jid "" or "." (current buddy)
    if (fjid && (!*fjid || !strcmp(fjid, ".")))
      fjid = NULL;

    if (fjid) {
      // The JID has been specified.  Quick check...
      if (check_jid_syntax(fjid) || !strchr(fjid, '@')) {
        scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                     "<%s> is not a valid Jabber ID.", fjid);
        fjid = NULL;
      } else {
        // Convert jid to lowercase and strip resource
        char *p;
        for (p = fjid; *p && *p != JID_RESOURCE_SEPARATOR; p++)
          *p = tolower(*p);
        if (*p == JID_RESOURCE_SEPARATOR)
          *p = '\0';
      }
    } else {
      gpointer bud = NULL;
      if (current_buddy)
        bud = BUDDATA(current_buddy);
      if (bud) {
        guint type = buddy_gettype(bud);
        if (type & ROSTER_TYPE_USER)  // Is it a user?
          fjid = (char*)buddy_getjid(bud);
        else
          scr_LogPrint(LPRINT_NORMAL, "The selected item should be a user.");
      }
    }

    if (fjid) { // fjid is actually a bare jid...
      switch (op) {
        case otr_start:
          otr_establish(fjid);          break;
        case otr_stop:
          otr_disconnect(fjid);         break;
        case otr_fpr:
          otr_fingerprint(fjid, keyid); break;
        case otr_smpq:
          otr_smp_query(fjid, keyid);   break;
        case otr_smpr:
          otr_smp_respond(fjid, keyid); break;
        case otr_smpa:
          otr_smp_abort(fjid);          break;
        case otr_info:
          otr_print_info(fjid);         break;
        default:
          break;
      }
    } else
      scr_LogPrint(LPRINT_NORMAL, "Please specify a valid Jabber ID.");
  }
  free_arg_lst(paramlst);

#else
  scr_LogPrint(LPRINT_NORMAL, "Please recompile mcabber with libotr enabled.");
#endif /* HAVE_LIBOTR */
}

#ifdef HAVE_LIBOTR
static char *string_for_otrpolicy(enum otr_policy p)
{
  switch (p) {
    case plain:         return "plain";
    case opportunistic: return "opportunistic";
    case manual:        return "manual";
    case always:        return "always";
    default:            return "unknown";
  }
}

static void dump_otrpolicy(char *k, char *v, void *nothing)
{
  scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8, "otrpolicy for %s: %s", k,
               string_for_otrpolicy(*(enum otr_policy*)v));
}
#endif

static void do_otrpolicy(char *arg)
{
#ifdef HAVE_LIBOTR
  char **paramlst;
  char *fjid, *policy;
  enum otr_policy p;

  paramlst = split_arg(arg, 2, 0); // [jid|default] policy
  fjid = *paramlst;
  policy = *(paramlst+1);

  if (!fjid && !policy) {
    scr_LogPrint(LPRINT_NORMAL, "default otrpolicy: %s",
                 string_for_otrpolicy(settings_otr_getpolicy(NULL)));
    settings_foreach(SETTINGS_TYPE_OTR, &dump_otrpolicy, NULL);
    free_arg_lst(paramlst);
    return;
  }

  if (!policy) {
    scr_LogPrint(LPRINT_NORMAL,
                 "Please call otrpolicy correctly: /otrpolicy (default|jid) "
                 "(plain|manual|opportunistic|always)");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(policy, "plain"))
    p = plain;
  else if (!strcasecmp(policy, "manual"))
    p = manual;
  else if (!strcasecmp(policy, "opportunistic"))
    p = opportunistic;
  else if (!strcasecmp(policy, "always"))
    p = always;
  else {
    /* Fail, we don't know _this_ policy*/
    scr_LogPrint(LPRINT_NORMAL, "mcabber doesn't support _this_ policy!");
    free_arg_lst(paramlst);
    return;
  }

  if (!strcasecmp(fjid, "default") || !strcasecmp(fjid, "*")) {
    /*set default policy*/
    settings_otr_setpolicy(NULL, p);
    free_arg_lst(paramlst);
    return;
  }
  // Allow special jid "" or "." (current buddy)
  if (fjid && (!*fjid || !strcmp(fjid, ".")))
    fjid = NULL;

  if (fjid) {
    // The JID has been specified.  Quick check...
    if (check_jid_syntax(fjid) || !strchr(fjid, '@')) {
      scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
                   "<%s> is not a valid Jabber ID.", fjid);
      fjid = NULL;
    } else {
      // Convert jid to lowercase and strip resource
      char *p;
      for (p = fjid; *p && *p != JID_RESOURCE_SEPARATOR; p++)
        *p = tolower(*p);
      if (*p == JID_RESOURCE_SEPARATOR)
        *p = '\0';
    }
  } else {
    gpointer bud = NULL;
    if (current_buddy)
      bud = BUDDATA(current_buddy);
    if (bud) {
      guint type = buddy_gettype(bud);
      if (type & ROSTER_TYPE_USER)  // Is it a user?
        fjid = (char*)buddy_getjid(bud);
      else
        scr_LogPrint(LPRINT_NORMAL, "The selected item should be a user.");
    }
  }

  if (fjid)
    settings_otr_setpolicy(fjid, p);
  else
    scr_LogPrint(LPRINT_NORMAL, "Please specify a valid Jabber ID.");

  free_arg_lst(paramlst);
#else
  scr_LogPrint(LPRINT_NORMAL, "Please recompile mcabber with libotr enabled.");
#endif /* HAVE_LIBOTR */
}

/* !!!
  After changing the /iline arguments names here, you must change ones
  in init_bindings().
*/
static void do_iline(char *arg)
{
  if (!strcasecmp(arg, "fword")) {
    readline_forward_word();
  } else if (!strcasecmp(arg, "bword")) {
    readline_backward_word();
  } else if (!strcasecmp(arg, "word_fdel")) {
    readline_forward_kill_word();
  } else if (!strcasecmp(arg, "word_bdel")) {
    readline_backward_kill_word();
  } else if (!strcasecmp(arg, "word_upcase")) {
    readline_updowncase_word(1);
  } else if (!strcasecmp(arg, "word_downcase")) {
    readline_updowncase_word(0);
  } else if (!strcasecmp(arg, "word_capit")) {
    readline_capitalize_word();
  } else if (!strcasecmp(arg, "fchar")) {
    readline_forward_char();
  } else if (!strcasecmp(arg, "bchar")) {
    readline_backward_char();
  } else if (!strcasecmp(arg, "char_fdel")) {
    readline_forward_kill_char();
  } else if (!strcasecmp(arg, "char_bdel")) {
    readline_backward_kill_char();
  } else if (!strcasecmp(arg, "char_swap")) {
    readline_transpose_chars();
  } else if (!strcasecmp(arg, "hist_beginning_search_bwd")) {
    readline_hist_beginning_search_bwd();
  } else if (!strcasecmp(arg, "hist_beginning_search_fwd")) {
    readline_hist_beginning_search_fwd();
  } else if (!strcasecmp(arg, "hist_prev")) {
    readline_hist_prev();
  } else if (!strcasecmp(arg, "hist_next")) {
    readline_hist_next();
  } else if (!strcasecmp(arg, "iline_start")) {
    readline_iline_start();
  } else if (!strcasecmp(arg, "iline_end")) {
    readline_iline_end();
  } else if (!strcasecmp(arg, "iline_fdel")) {
    readline_forward_kill_iline();
  } else if (!strcasecmp(arg, "iline_bdel")) {
    readline_backward_kill_iline();
  } else if (!strcasecmp(arg, "send_multiline")) {
    readline_send_multiline();
  } else if (!strcasecmp(arg, "iline_accept")) {
    retval_for_cmds = readline_accept_line(FALSE);
  } else if (!strcasecmp(arg, "iline_accept_down_hist")) {
    retval_for_cmds = readline_accept_line(TRUE);
  } else if (!strcasecmp(arg, "compl_cancel")) {
    readline_cancel_completion();
  } else if (!strcasecmp(arg, "compl_do")) {
    readline_do_completion();
  }
}

static void do_screen_refresh(char *arg)
{
  readline_refresh_screen();
}

static void do_chat_disable(char *arg)
{
  guint show_roster;

  if (arg && !strcasecmp(arg, "--show-roster"))
    show_roster = 1;
  else
    show_roster = 0;

  readline_disable_chat_mode(show_roster);
}

static int source_print_error(const char *path, int eerrno)
{
  scr_LogPrint(LPRINT_DEBUG, "Source: glob (%s) error: %s.",
               path, strerror(eerrno));
  return 0;
}

static void do_source(char *arg)
{
  static int recur_level;
  gchar *filename, *expfname;
  glob_t flist;
  if (!*arg) {
    scr_LogPrint(LPRINT_NORMAL, "Missing filename.");
    return;
  }
  if (recur_level > 20) {
    scr_LogPrint(LPRINT_LOGNORM, "** Too many source commands!");
    return;
  }
  filename = g_strdup(arg);
  strip_arg_special_chars(filename);
  expfname = expand_filename(filename);
  g_free(filename);
  // match
  flist.gl_offs = 0;
  if (glob(expfname, 0, source_print_error, &flist)) {
    scr_LogPrint(LPRINT_LOGNORM, "Source: error: %s.", strerror (errno));
  } else {
    unsigned int i;
    // sort list
    for (i = 1; i < flist.gl_pathc; ++i) {
      int j;
      for (j = i-1; j > 0; --j) {
        char *a = flist.gl_pathv[j+1];
        char *b = flist.gl_pathv[j];
        if (strcmp(a, b) < 0) {
          flist.gl_pathv[j]   = a;
          flist.gl_pathv[j+1] = b;
        } else
          break;
      }
    }
    // source files in list
    for (i=0; i < flist.gl_pathc; ++i) {
      recur_level++;
      cfg_read_file(flist.gl_pathv[i], FALSE);
      recur_level--;
    }
    // free
    globfree(&flist);
  }
  g_free(expfname);
}

static void do_connect(char *arg)
{
  xmpp_connect();
}

static void do_disconnect(char *arg)
{
  xmpp_disconnect();
}

static void do_help(char *arg)
{
  help_process(arg);
}

static void do_echo(char *arg)
{
  if (arg)
    scr_print_logwindow(arg);
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
