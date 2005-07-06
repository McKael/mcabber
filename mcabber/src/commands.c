/*
 * commands.c     -- user commands handling
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

#include "commands.h"
#include "jabglue.h"
#include "roster.h"
#include "screen.h"
#include "compl.h"
#include "hooks.h"
#include "hbuf.h"
#include "utf8.h"
#include "utils.h"
#include "settings.h"

// Commands callbacks
void do_roster(char *arg);
void do_status(char *arg);
void do_add(char *arg);
void do_del(char *arg);
void do_group(char *arg);
void do_say(char *arg);
void do_msay(char *arg);
void do_buffer(char *arg);
void do_clear(char *arg);
void do_info(char *arg);
void do_rename(char *arg);
void do_move(char *arg);
void do_set(char *arg);

// Global variable for the commands list
static GSList *Commands;


//  cmd_add()
// Adds a command to the commands list and to the CMD completion list
void cmd_add(const char *name, const char *help,
        guint flags_row1, guint flags_row2, void (*f)())
{
  cmd *n_cmd = g_new0(cmd, 1);
  strncpy(n_cmd->name, name, 32-1);
  n_cmd->help = help;
  n_cmd->completion_flags[0] = flags_row1;
  n_cmd->completion_flags[1] = flags_row2;
  n_cmd->func = f;
  Commands = g_slist_append(Commands, n_cmd);
  // Add to completion CMD category
  compl_add_category_word(COMPL_CMD, name);
}

//  cmd_init()
// ...
void cmd_init(void)
{
  cmd_add("add", "Add a jabber user", COMPL_JID, 0, &do_add);
  cmd_add("buffer", "Manipulate current buddy's buffer (chat window)",
          COMPL_BUFFER, 0, &do_buffer);
  cmd_add("clear", "Clear the dialog window", 0, 0, &do_clear);
  cmd_add("del", "Delete the current buddy", 0, 0, &do_del);
  cmd_add("group", "Change group display settings", COMPL_GROUP, 0, &do_group);
  //cmd_add("help", "Display some help", COMPL_CMD, 0, NULL);
  cmd_add("info", "Show basic infos on current buddy", 0, 0, &do_info);
  cmd_add("move", "Move the current buddy to another group", COMPL_GROUPNAME,
          0, &do_move);
  cmd_add("msay", "Send a multi-lines message to the selected buddy",
          COMPL_MULTILINE, 0, &do_msay);
  //cmd_add("nick");
  cmd_add("quit", "Exit the software", 0, 0, NULL);
  cmd_add("rename", "Rename the current buddy", 0, 0, &do_rename);
  //cmd_add("request_auth");
  cmd_add("roster", "Manipulate the roster/buddylist", COMPL_ROSTER, 0,
          &do_roster);
  cmd_add("say", "Say something to the selected buddy", 0, 0, &do_say);
  //cmd_add("search");
  //cmd_add("send_auth");
  cmd_add("set", "Set/query an option value", 0, 0, &do_set);
  cmd_add("status", "Show or set your status", COMPL_STATUS, 0, &do_status);

  // Status category
  compl_add_category_word(COMPL_STATUS, "online");
  compl_add_category_word(COMPL_STATUS, "avail");
  compl_add_category_word(COMPL_STATUS, "invisible");
  compl_add_category_word(COMPL_STATUS, "free");
  compl_add_category_word(COMPL_STATUS, "dnd");
  compl_add_category_word(COMPL_STATUS, "notavail");
  compl_add_category_word(COMPL_STATUS, "away");

  // Roster category
  compl_add_category_word(COMPL_ROSTER, "bottom");
  compl_add_category_word(COMPL_ROSTER, "top");
  compl_add_category_word(COMPL_ROSTER, "hide_offline");
  compl_add_category_word(COMPL_ROSTER, "show_offline");
  compl_add_category_word(COMPL_ROSTER, "search");
  compl_add_category_word(COMPL_ROSTER, "unread_first");
  compl_add_category_word(COMPL_ROSTER, "unread_next");

  // Roster category
  compl_add_category_word(COMPL_BUFFER, "bottom");
  compl_add_category_word(COMPL_BUFFER, "clear");
  compl_add_category_word(COMPL_BUFFER, "top");

  // Group category
  compl_add_category_word(COMPL_GROUP, "fold");
  compl_add_category_word(COMPL_GROUP, "unfold");
  compl_add_category_word(COMPL_GROUP, "toggle");

  // Multi-line (msay) category
  compl_add_category_word(COMPL_MULTILINE, "abort");
  compl_add_category_word(COMPL_MULTILINE, "begin");
  compl_add_category_word(COMPL_MULTILINE, "send");
  compl_add_category_word(COMPL_MULTILINE, "verbatim");
}

//  cmd_get
// Finds command in the command list structure.
// Returns a pointer to the cmd entry, or NULL if command not found.
cmd *cmd_get(const char *command)
{
  const char *p1, *p2;
  char *com;
  GSList *sl_com;
  // Ignore leading '/'
  for (p1 = command ; *p1 == '/' ; p1++)
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

//  send_message(msg)
// Write the message in the buddy's window and send the message on
// the network.
void send_message(const char *msg)
{
  const char *jid;
      
  if (!current_buddy) {
    scr_LogPrint("No buddy currently selected.");
    return;
  }

  jid = CURRENT_JID;
  if (!jid) {
    scr_LogPrint("No buddy currently selected.");
    return;
  }

  // local part (UI, logging, etc.)
  hk_message_out(jid, 0, msg);

  // Network part
  jb_send_msg(jid, msg);
}

//  process_line(line)
// Process a command/message line.
// If this isn't a command, this is a message and it is sent to the
// currently selected buddy.
int process_line(char *line)
{
  char *p;
  cmd *curcmd;

  if (!*line) { // User only pressed enter
    if (scr_get_multimode()) {
      scr_append_multiline("");
      return 0;
    }
    if (current_buddy) {
      scr_set_chatmode(TRUE);
      buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);
      scr_ShowBuddyWindow();
    }
    return 0;
  }

  if (*line != '/') {
    // This isn't a command
    if (scr_get_multimode())
      scr_append_multiline(line);
    else
      do_say(line);
    return 0;
  }

  /* It is a command */
  // Remove trailing spaces:
  for (p=line ; *p ; p++)
    ;
  for (p-- ; p>line && (*p == ' ') ; p--)
    *p = 0;

  // Command "quit"?
  if ((!strncasecmp(line, "/quit", 5)) && (scr_get_multimode() != 2) )
    if (!line[5] || line[5] == ' ')
      return 255;

  // If verbatim multi-line mode, we check if another /msay command is typed
  if ((scr_get_multimode() == 2) && (strncasecmp(line, "/msay ", 6))) {
    // It isn't an /msay command
    scr_append_multiline(line);
    return 0;
  }

  // Commands handling
  curcmd = cmd_get(line);

  if (!curcmd) {
    scr_LogPrint("Unrecognized command, sorry.");
    return 0;
  }
  if (!curcmd->func) {
    scr_LogPrint("Not yet implemented, sorry.");
    return 0;
  }
  // Lets go to the command parameters
  for (line++; *line && (*line != ' ') ; line++)
    ;
  // Skip spaces
  while (*line && (*line == ' '))
    line++;
  // Call command-specific function
  (*curcmd->func)(line);
  return 0;
}

/* Commands callback functions */

void do_roster(char *arg)
{
  if (!strcasecmp(arg, "top")) {
    scr_RosterTop();
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "bottom")) {
    scr_RosterBottom();
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "hide_offline")) {
    buddylist_set_hide_offline_buddies(TRUE);
    if (current_buddy)
      buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "show_offline")) {
    buddylist_set_hide_offline_buddies(FALSE);
    buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "unread_first")) {
    scr_RosterUnreadMessage(0);
  } else if (!strcasecmp(arg, "unread_next")) {
    scr_RosterUnreadMessage(1);
  } else if (!strncasecmp(arg, "search", 6)) {
    char *string = arg+6;
    if (*string && (*string != ' ')) {
      scr_LogPrint("Unrecognized parameter!");
      return;
    }
    while (*string == ' ')
      string++;
    if (!*string) {
      scr_LogPrint("What name or jid are you looking for?");
      return;
    }
    scr_RosterSearch(string);
    update_roster = TRUE;
  } else
    scr_LogPrint("Unrecognized parameter!");
}

void do_status(char *arg)
{
  enum imstatus st;

  if (!arg || (*arg == 0)) {
    scr_LogPrint("Your status is: %c", imstatus2char[jb_getstatus()]);
    return;
  }

  if (!strcasecmp(arg, "offline"))        st = offline;
  else if (!strcasecmp(arg, "online"))    st = available;
  else if (!strcasecmp(arg, "avail"))     st = available;
  else if (!strcasecmp(arg, "away"))      st = away;
  else if (!strcasecmp(arg, "invisible")) st = invisible;
  else if (!strcasecmp(arg, "dnd"))       st = dontdisturb;
  else if (!strcasecmp(arg, "notavail"))  st = notavail;
  else if (!strcasecmp(arg, "free"))      st = freeforchat;
  else {
    scr_LogPrint("Unrecognized parameter!");
    return;
  }

  // XXX special case if offline??
  jb_setstatus(st, NULL);  // TODO handle message (instead of NULL)
}

void do_add(char *arg)
{
  char *id, *nick;
  if (!arg || (*arg == 0)) {
    scr_LogPrint("Wrong usage");
    return;
  }

  id = g_strdup(arg);
  nick = strchr(id, ' ');
  if (nick) {
    *nick++ = 0;
    while (*nick && *nick == ' ')
      nick++;
  }

  // FIXME check id =~ jabber id
  // 2nd parameter = optional nickname
  jb_addbuddy(id, nick, NULL);
  scr_LogPrint("Sent presence notfication request to <%s>", id);
  g_free(id);
}

void do_del(char *arg)
{
  const char *jid;

  if (arg && (*arg)) {
    scr_LogPrint("Wrong usage");
    return;
  }

  if (!current_buddy) return;
  jid = buddy_getjid(BUDDATA(current_buddy));
  if (!jid) return;

  scr_LogPrint("Removing <%s>...", jid);
  jb_delbuddy(jid);
}

void do_group(char *arg)
{
  gpointer group;
  guint leave_windowbuddy;

  if (!arg || (*arg == 0)) {
    scr_LogPrint("Missing parameter");
    return;
  }

  if (!current_buddy) return;

  group = buddy_getgroup(BUDDATA(current_buddy));
  // We'll have to redraw the chat window if we're not currently on the group
  // entry itself, because it means we'll have to leave the current buddy
  // chat window.
  leave_windowbuddy = (group != BUDDATA(current_buddy));

  if (!(buddy_gettype(group) & ROSTER_TYPE_GROUP)) {
    scr_LogPrint("You need to select a group");
    return;
  }

  if (!strcasecmp(arg, "expand") || !strcasecmp(arg, "unfold")) {
    buddy_setflags(group, ROSTER_FLAG_HIDE, FALSE);
  } else if (!strcasecmp(arg, "shrink") || !strcasecmp(arg, "fold")) {
    buddy_setflags(group, ROSTER_FLAG_HIDE, TRUE);
  } else if (!strcasecmp(arg, "toggle")) {
    buddy_setflags(group, ROSTER_FLAG_HIDE,
            !(buddy_getflags(group) & ROSTER_FLAG_HIDE));
  } else {
    scr_LogPrint("Unrecognized parameter!");
    return;
  }

  buddylist_build();
  update_roster = TRUE;
  if (leave_windowbuddy) scr_ShowBuddyWindow();
}

void do_say(char *arg)
{
  gpointer bud;

  scr_set_chatmode(TRUE);

  if (!current_buddy) {
    scr_LogPrint("Who are you talking to??");
    return;
  }

  bud = BUDDATA(current_buddy);
  if (!(buddy_gettype(bud) & ROSTER_TYPE_USER)) {
    scr_LogPrint("This is not a user");
    return;
  }

  buddy_setflags(bud, ROSTER_FLAG_LOCK, TRUE);
  send_message(arg);
}

void do_msay(char *arg)
{
  /* Parameters: begin verbatim abort send */
  gpointer bud;

  if (!strcasecmp(arg, "abort")) {
    scr_set_multimode(FALSE);
    return;
  } else if ((!strcasecmp(arg, "begin")) || (!strcasecmp(arg, "verbatim"))) {
    if (!strcasecmp(arg, "verbatim"))
      scr_set_multimode(2);
    else
      scr_set_multimode(1);

    scr_LogPrint("Entered multi-line message mode.");
    scr_LogPrint("Select a buddy and use \"/msay send\" "
                 "when your message is ready.");
    return;
  } else if (*arg == 0) {
    scr_LogPrint("Please read the manual before using the /msay command.");
    scr_LogPrint("(Use /msay begin to enter multi-line mode...)");
    return;
  } else if (strcasecmp(arg, "send")) {
    scr_LogPrint("Unrecognized parameter!");
    return;
  }

  // send command

  if (!scr_get_multimode()) {
    scr_LogPrint("No message to send.  Use \"/msay begin\" first.");
    return;
  }

  scr_set_chatmode(TRUE);

  if (!current_buddy) {
    scr_LogPrint("Who are you talking to??");
    return;
  }

  bud = BUDDATA(current_buddy);
  if (!(buddy_gettype(bud) & ROSTER_TYPE_USER)) {
    scr_LogPrint("This is not a user");
    return;
  }

  buddy_setflags(bud, ROSTER_FLAG_LOCK, TRUE);
  send_message(scr_get_multiline());
  scr_set_multimode(FALSE);
}

void do_buffer(char *arg)
{
  if (!strcasecmp(arg, "top")) {
    scr_BufferTop();
  } else if (!strcasecmp(arg, "bottom")) {
    scr_BufferBottom();
  } else if (!strcasecmp(arg, "clear")) {
    scr_Clear();
  } else
    scr_LogPrint("Unrecognized parameter!");
}

void do_clear(char *arg)    // Alias for "/buffer clear"
{
  do_buffer("clear");
}

void do_info(char *arg)
{
  gpointer bud;
  const char *jid, *name, *st_msg;
  guint type;
  enum imstatus status;
  char *buffer;

  if (!current_buddy) return;
  bud = BUDDATA(current_buddy);

  jid    = buddy_getjid(bud);
  name   = buddy_getname(bud);
  type   = buddy_gettype(bud);
  status = buddy_getstatus(bud);
  st_msg = buddy_getstatusmsg(bud);

  buffer = g_new(char, 128);

  if (jid) {
    char *typestr = "unknown";

    snprintf(buffer, 127, "jid:  <%s>", jid);
    scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
    if (name) {
      snprintf(buffer, 127, "Name: %s", name);
      scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
    }
    if (st_msg) {
      snprintf(buffer, 127, "Status message: %s", st_msg);
      scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
    }

    if (type == ROSTER_TYPE_USER) typestr = "user";
    else if (type == ROSTER_TYPE_AGENT) typestr = "agent";

    snprintf(buffer, 127, "Type: %s", typestr);
    scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
  } else {
    if (name) scr_LogPrint("Name: %s", name);
    scr_LogPrint("Type: %s",
            ((type == ROSTER_TYPE_GROUP) ? "group" : "unknown"));
  }

  g_free(buffer);
}

void do_rename(char *arg)
{
  gpointer bud;
  const char *jid, *group;
  guint type;
  char *newname, *p;

  if (!arg || (*arg == 0)) {
    scr_LogPrint("Missing parameter");
    return;
  }

  if (!current_buddy) return;
  bud = BUDDATA(current_buddy);

  jid   = buddy_getjid(bud);
  group = buddy_getgroupname(bud);
  type  = buddy_gettype(bud);

  if (type & ROSTER_TYPE_GROUP) {
    scr_LogPrint("You can't rename groups");
    return;
  }

  newname = g_strdup(arg);
  // Remove trailing space
  for (p = newname; *p; p++) ;
  while (p > newname && *p == ' ') *p = 0;

  buddy_setname(bud, newname);
  jb_updatebuddy(jid, newname, group);

  g_free(newname);
  update_roster = TRUE;
}

void do_move(char *arg)
{
  gpointer bud;
  const char *jid, *name;
  guint type;
  char *newgroupname, *p;

  if (!current_buddy) return;
  bud = BUDDATA(current_buddy);

  jid  = buddy_getjid(bud);
  name = buddy_getname(bud);
  type = buddy_gettype(bud);

  if (type & ROSTER_TYPE_GROUP) {
    scr_LogPrint("You can't move groups!");
    return;
  }

  newgroupname = g_strdup(arg);
  // Remove trailing space
  for (p = newgroupname; *p; p++) ;
  while (p > newgroupname && *p == ' ') *p = 0;

  // Call to buddy_setgroup() should be at the end, as current implementation
  // clones the buddy and deletes the old one (and thus, jid and name are
  // freed)
  jb_updatebuddy(jid, name, newgroupname);
  buddy_setgroup(bud, newgroupname);

  g_free(newgroupname);
  update_roster = TRUE;
}

void do_set(char *arg)
{
  guint assign;
  const gchar *option, *value;
  
  assign = parse_assigment(arg, &option, &value);
  if (!option) {
    scr_LogPrint("Huh?");
    return;
  }
  if (!assign) {
    // This is a query
    value = settings_opt_get(option);
    if (value) {
      scr_LogPrint("%s = [%s]", option, value);
    } else
      scr_LogPrint("Option %s is not set", option);
    return;
  }
  // Update the option
  // XXX Maybe some options should be protected when user is connected
  // (server, username, etc.).  And we should catch some options here, too
  // (hide_offline_buddies for ex.)
  if (!value) {
    settings_del(SETTINGS_TYPE_OPTION, option);
  } else {
    settings_set(SETTINGS_TYPE_OPTION, option, value);
  }
}

