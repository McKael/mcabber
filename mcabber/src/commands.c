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
#include "utils.h"
#include "settings.h"

// Commands callbacks
static void do_roster(char *arg);
static void do_status(char *arg);
static void do_status_to(char *arg);
static void do_add(char *arg);
static void do_del(char *arg);
static void do_group(char *arg);
static void do_say(char *arg);
static void do_msay(char *arg);
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
  cmd_add("alias", "Add an alias", 0, 0, &do_alias);
  cmd_add("bind", "Add an key binding", 0, 0, &do_bind);
  cmd_add("buffer", "Manipulate current buddy's buffer (chat window)",
          COMPL_BUFFER, 0, &do_buffer);
  cmd_add("clear", "Clear the dialog window", 0, 0, &do_clear);
  cmd_add("connect", "Connect to the server", 0, 0, &do_connect);
  cmd_add("del", "Delete the current buddy", 0, 0, &do_del);
  cmd_add("disconnect", "Disconnect from server", 0, 0, &do_disconnect);
  cmd_add("group", "Change group display settings", COMPL_GROUP, 0, &do_group);
  //cmd_add("help", "Display some help", COMPL_CMD, 0, NULL);
  cmd_add("info", "Show basic infos on current buddy", 0, 0, &do_info);
  cmd_add("move", "Move the current buddy to another group", COMPL_GROUPNAME,
          0, &do_move);
  cmd_add("msay", "Send a multi-lines message to the selected buddy",
          COMPL_MULTILINE, 0, &do_msay);
  cmd_add("room", "MUC actions command", COMPL_ROOM, 0, &do_room);
  cmd_add("quit", "Exit the software", 0, 0, NULL);
  cmd_add("rawxml", "Send a raw XML string", 0, 0, &do_rawxml);
  cmd_add("rename", "Rename the current buddy", 0, 0, &do_rename);
  //cmd_add("request_auth");
  cmd_add("roster", "Manipulate the roster/buddylist", COMPL_ROSTER, 0,
          &do_roster);
  cmd_add("say", "Say something to the selected buddy", 0, 0, &do_say);
  //cmd_add("search");
  //cmd_add("send_auth");
  cmd_add("set", "Set/query an option value", 0, 0, &do_set);
  cmd_add("status", "Show or set your status", COMPL_STATUS, 0, &do_status);
  cmd_add("status_to", "Show or set your status for one recipient",
          COMPL_JID, COMPL_STATUS, &do_status_to);

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
  compl_add_category_word(COMPL_ROSTER, "up");
  compl_add_category_word(COMPL_ROSTER, "down");
  compl_add_category_word(COMPL_ROSTER, "hide_offline");
  compl_add_category_word(COMPL_ROSTER, "show_offline");
  compl_add_category_word(COMPL_ROSTER, "toggle_offline");
  compl_add_category_word(COMPL_ROSTER, "alternate");
  compl_add_category_word(COMPL_ROSTER, "search");
  compl_add_category_word(COMPL_ROSTER, "unread_first");
  compl_add_category_word(COMPL_ROSTER, "unread_next");

  // Roster category
  compl_add_category_word(COMPL_BUFFER, "bottom");
  compl_add_category_word(COMPL_BUFFER, "clear");
  compl_add_category_word(COMPL_BUFFER, "top");
  compl_add_category_word(COMPL_BUFFER, "search_backward");
  compl_add_category_word(COMPL_BUFFER, "search_forward");

  // Group category
  compl_add_category_word(COMPL_GROUP, "fold");
  compl_add_category_word(COMPL_GROUP, "unfold");
  compl_add_category_word(COMPL_GROUP, "toggle");

  // Multi-line (msay) category
  compl_add_category_word(COMPL_MULTILINE, "abort");
  compl_add_category_word(COMPL_MULTILINE, "begin");
  compl_add_category_word(COMPL_MULTILINE, "send");
  compl_add_category_word(COMPL_MULTILINE, "verbatim");

  // Room category
  compl_add_category_word(COMPL_ROOM, "join");
  compl_add_category_word(COMPL_ROOM, "leave");
  compl_add_category_word(COMPL_ROOM, "names");
  compl_add_category_word(COMPL_ROOM, "remove");
  compl_add_category_word(COMPL_ROOM, "unlock");
}

//  expandalias(line)
// If there is one, expand the alias in line and returns a new allocated line
// If no alias is found, returns line
// Note : if the returned pointer is different from line, the caller should
//        g_free() the pointer after use
char *expandalias(char *line)
{
  const char *p1, *p2;
  char *word;
  const gchar *value;
  char *newline = line;

  // Ignore leading '/'
  for (p1 = line ; *p1 == '/' ; p1++)
    ;
  // Locate the end of the word
  for (p2 = p1 ; *p2 && (*p2 != ' ') ; p2++)
    ;
  // Extract the word
  word = g_strndup(p1, p2-p1);

  // Look for an alias in the list
  value = settings_get(SETTINGS_TYPE_ALIAS, (const char*)word);
  if (value) {
    // There is an alias to expand
    newline = g_new(char, strlen(value)+strlen(p2)+2);
    *newline = '/';
    strcpy(newline+1, value);
    strcat(newline, p2);
  }
  g_free(word);

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
    scr_LogPrint(LPRINT_NORMAL, "No buddy currently selected.");
    return;
  }

  jid = CURRENT_JID;
  if (!jid) {
    scr_LogPrint(LPRINT_NORMAL, "No buddy currently selected.");
    return;
  }

  if (buddy_gettype(BUDDATA(current_buddy)) != ROSTER_TYPE_ROOM) {
    // local part (UI, logging, etc.)
    hk_message_out(jid, 0, msg);
  }

  // Network part
  jb_send_msg(jid, msg, buddy_gettype(BUDDATA(current_buddy)));
}

//  process_command(line)
// Process a command line.
// Return 255 if this is the /quit command, and 0 for the other commands.
int process_command(char *line)
{
  char *p;
  char *xpline;
  cmd *curcmd;

  // Remove trailing spaces:
  for (p=line ; *p ; p++)
    ;
  for (p-- ; p>line && (*p == ' ') ; p--)
    *p = 0;

  // We do alias expansion here
  if (scr_get_multimode() != 2)
    xpline = expandalias(line);
  else
    xpline = line; // No expansion in verbatim multi-line mode

  // Command "quit"?
  if ((!strncasecmp(xpline, "/quit", 5)) && (scr_get_multimode() != 2) )
    if (!xpline[5] || xpline[5] == ' ')
      return 255;

  // If verbatim multi-line mode, we check if another /msay command is typed
  if ((scr_get_multimode() == 2) && (strncasecmp(xpline, "/msay ", 6))) {
    // It isn't an /msay command
    scr_append_multiline(xpline);
    return 0;
  }

  // Commands handling
  curcmd = cmd_get(xpline);

  if (!curcmd) {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized command, sorry.");
    if (xpline != line) g_free(xpline);
    return 0;
  }
  if (!curcmd->func) {
    scr_LogPrint(LPRINT_NORMAL, "Not yet implemented, sorry.");
    if (xpline != line) g_free(xpline);
    return 0;
  }
  // Lets go to the command parameters
  for (p = xpline+1; *p && (*p != ' ') ; p++)
    ;
  // Skip spaces
  while (*p && (*p == ' '))
    p++;
  // Call command-specific function
  (*curcmd->func)(p);
  if (xpline != line) g_free(xpline);
  return 0;
}

//  process_line(line)
// Process a command/message line.
// If this isn't a command, this is a message and it is sent to the
// currently selected buddy.
// Return 255 if the line is the /quit command, or 0.
int process_line(char *line)
{
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

  /* It is (probably) a command -- except for verbatim multi-line mode */
  return process_command(line);
}

/* Commands callback functions */

static void do_roster(char *arg)
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
  } else if (!strcasecmp(arg, "toggle_offline")) {
    buddylist_set_hide_offline_buddies(-1);
    buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "unread_first")) {
    scr_RosterUnreadMessage(0);
  } else if (!strcasecmp(arg, "unread_next")) {
    scr_RosterUnreadMessage(1);
  } else if (!strcasecmp(arg, "alternate")) {
    scr_RosterJumpAlternate();
  } else if (!strncasecmp(arg, "search", 6)) {
    char *string = arg+6;
    if (*string && (*string != ' ')) {
      scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
      return;
    }
    while (*string == ' ')
      string++;
    if (!*string) {
      scr_LogPrint(LPRINT_NORMAL, "What name or jid are you looking for?");
      return;
    }
    scr_RosterSearch(string);
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "up")) {
    scr_RosterUp();
  } else if (!strcasecmp(arg, "down")) {
    scr_RosterDown();
  } else
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
}

//  setstatus(recipient, arg)
// Set your Jabber status.
// - if recipient is not NULL, the status is sent to this contact only
// - arg must be "status message" (message is optional)
static void setstatus(const char *recipient, const char *arg)
{
  enum imstatus st;
  int len;
  char *msg;

  msg = strchr(arg, ' ');
  if (!msg)
    len = strlen(arg);
  else
    len = msg - arg;

  if      (!strncasecmp(arg, "offline",   len)) st = offline;
  else if (!strncasecmp(arg, "online",    len)) st = available;
  else if (!strncasecmp(arg, "avail",     len)) st = available;
  else if (!strncasecmp(arg, "away",      len)) st = away;
  else if (!strncasecmp(arg, "invisible", len)) st = invisible;
  else if (!strncasecmp(arg, "dnd",       len)) st = dontdisturb;
  else if (!strncasecmp(arg, "notavail",  len)) st = notavail;
  else if (!strncasecmp(arg, "free",      len)) st = freeforchat;
  else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized status!");
    return;
  }

  if (msg && st != invisible) {
    for (msg++ ; *msg && *msg == ' ' ; msg++) ;
    if (!*msg) msg = NULL;
  } else
    msg = NULL;

  jb_setstatus(st, recipient, msg);
}

static void do_status(char *arg)
{
  if (!arg || (!*arg)) {
    scr_LogPrint(LPRINT_NORMAL, "Your status is: %c",
                 imstatus2char[jb_getstatus()]);
    return;
  }
  setstatus(NULL, arg);
}

static void do_status_to(char *arg)
{
  char *id, *st;
  if (!arg || (*arg == 0)) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter");
    return;
  }

  // Split recipient jid, status
  id = g_strdup(arg);
  st = strchr(id, ' ');
  if (!st) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter");
    g_free(id);
    return;
  }

  *st++ = 0;
  while (*st && *st == ' ')
    st++;

  if (check_jid_syntax(id)) {
    scr_LogPrint(LPRINT_NORMAL, "<%s> is not a valid Jabber id", id);
  } else {
    mc_strtolower(id);
    scr_LogPrint(LPRINT_LOGNORM, "Sending to <%s> /status %s", id, st);
    setstatus(id, st);
  }
  g_free(id);
}

static void do_add(char *arg)
{
  char *id, *nick;
  if (!arg || (!*arg)) {
    scr_LogPrint(LPRINT_NORMAL, "Wrong usage");
    return;
  }

  id = g_strdup(arg);
  nick = strchr(id, ' ');
  if (nick) {
    *nick++ = 0;
    while (*nick && *nick == ' ')
      nick++;
  }

  if (check_jid_syntax(id)) {
    scr_LogPrint(LPRINT_NORMAL, "<%s> is not a valid Jabber id", id);
  } else {
    mc_strtolower(id);
    // 2nd parameter = optional nickname
    jb_addbuddy(id, nick, NULL);
    scr_LogPrint(LPRINT_LOGNORM, "Sent presence notification request to <%s>",
                 id);
  }
  g_free(id);
}

static void do_del(char *arg)
{
  const char *jid;

  if (arg && (*arg)) {
    scr_LogPrint(LPRINT_NORMAL, "Wrong usage");
    return;
  }

  if (!current_buddy) return;
  jid = buddy_getjid(BUDDATA(current_buddy));
  if (!jid) return;

  scr_LogPrint(LPRINT_LOGNORM, "Removing <%s>...", jid);
  jb_delbuddy(jid);
}

static void do_group(char *arg)
{
  gpointer group;
  guint leave_windowbuddy;

  if (!arg || (!*arg)) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter");
    return;
  }

  if (!current_buddy) return;

  group = buddy_getgroup(BUDDATA(current_buddy));
  // We'll have to redraw the chat window if we're not currently on the group
  // entry itself, because it means we'll have to leave the current buddy
  // chat window.
  leave_windowbuddy = (group != BUDDATA(current_buddy));

  if (!(buddy_gettype(group) & ROSTER_TYPE_GROUP)) {
    scr_LogPrint(LPRINT_NORMAL, "You need to select a group");
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
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
    return;
  }

  buddylist_build();
  update_roster = TRUE;
  if (leave_windowbuddy) scr_ShowBuddyWindow();
}

static void do_say(char *arg)
{
  gpointer bud;

  scr_set_chatmode(TRUE);

  if (!current_buddy) {
    scr_LogPrint(LPRINT_NORMAL, "Who are you talking to??");
    return;
  }

  bud = BUDDATA(current_buddy);
  if (!(buddy_gettype(bud) & (ROSTER_TYPE_USER|ROSTER_TYPE_ROOM))) {
    scr_LogPrint(LPRINT_NORMAL, "This is not a user");
    return;
  }

  buddy_setflags(bud, ROSTER_FLAG_LOCK, TRUE);
  send_message(arg);
}

static void do_msay(char *arg)
{
  /* Parameters: begin verbatim abort send */
  gpointer bud;

  if (!strcasecmp(arg, "abort")) {
    if (scr_get_multimode())
      scr_LogPrint(LPRINT_NORMAL, "Leaving multi-line message mode");
    scr_set_multimode(FALSE);
    return;
  } else if ((!strcasecmp(arg, "begin")) || (!strcasecmp(arg, "verbatim"))) {
    if (!strcasecmp(arg, "verbatim"))
      scr_set_multimode(2);
    else
      scr_set_multimode(1);

    scr_LogPrint(LPRINT_NORMAL, "Entered multi-line message mode.");
    scr_LogPrint(LPRINT_NORMAL, "Select a buddy and use \"/msay send\" "
                 "when your message is ready.");
    return;
  } else if (!*arg) {
    scr_LogPrint(LPRINT_NORMAL, "Please read the manual before using "
                 "the /msay command.");
    scr_LogPrint(LPRINT_NORMAL, "(Use \"/msay begin\" to enter "
                 "multi-line mode...)");
    return;
  } else if (strcasecmp(arg, "send")) {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
    return;
  }

  // send command

  if (!scr_get_multimode()) {
    scr_LogPrint(LPRINT_NORMAL, "No message to send.  "
                 "Use \"/msay begin\" first.");
    return;
  }

  scr_set_chatmode(TRUE);

  if (!current_buddy) {
    scr_LogPrint(LPRINT_NORMAL, "Who are you talking to??");
    return;
  }

  bud = BUDDATA(current_buddy);
  if (!(buddy_gettype(bud) & (ROSTER_TYPE_USER|ROSTER_TYPE_ROOM))) {
    scr_LogPrint(LPRINT_NORMAL, "This is not a user");
    return;
  }

  buddy_setflags(bud, ROSTER_FLAG_LOCK, TRUE);
  send_message(scr_get_multiline());
  scr_set_multimode(FALSE);
}

static void do_buffer(char *arg)
{
  int search_dir = 0;

  if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_GROUP) {
    scr_LogPrint(LPRINT_NORMAL, "Groups have no buffer");
    return;
  }

  if (!strcasecmp(arg, "top")) {
    scr_BufferTopBottom(-1);
  } else if (!strcasecmp(arg, "bottom")) {
    scr_BufferTopBottom(1);
  } else if (!strcasecmp(arg, "clear")) {
    scr_BufferClear();
  } else if (!strncasecmp(arg, "search_backward", 15)) {
    arg += 15;
    if (*arg++ == ' ')
      search_dir = -1;
    else
      scr_LogPrint(LPRINT_NORMAL, "Wrong or missing parameter");
  } else if (!strncasecmp(arg, "search_forward", 14)) {
    arg += 14;
    if (*arg++ == ' ')
      search_dir = 1;
    else
      scr_LogPrint(LPRINT_NORMAL, "Wrong or missing parameter");
  } else
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");

  if (search_dir) { // It is a string search command
    for ( ; *arg && *arg == ' ' ; arg++)
      ;
    scr_BufferSearch(search_dir, arg);
  }
}

static void do_clear(char *arg)    // Alias for "/buffer clear"
{
  do_buffer("clear");
}

static void do_info(char *arg)
{
  gpointer bud;
  const char *jid, *name;
  guint type;
  char *buffer;

  if (!current_buddy) return;
  bud = BUDDATA(current_buddy);

  jid    = buddy_getjid(bud);
  name   = buddy_getname(bud);
  type   = buddy_gettype(bud);

  buffer = g_new(char, 128);

  if (jid) {
    GSList *resources;
    char *typestr = "unknown";

    snprintf(buffer, 127, "jid:  <%s>", jid);
    scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
    if (name) {
      snprintf(buffer, 127, "Name: %s", name);
      scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
    }

    if (type == ROSTER_TYPE_USER)       typestr = "user";
    else if (type == ROSTER_TYPE_ROOM)  typestr = "chatroom";
    else if (type == ROSTER_TYPE_AGENT) typestr = "agent";
    snprintf(buffer, 127, "Type: %s", typestr);
    scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);

    resources = buddy_getresources(bud);
    for ( ; resources ; resources = g_slist_next(resources) ) {
      gchar rprio;
      enum imstatus rstatus;
      const char *rst_msg;

      rprio   = buddy_getresourceprio(bud, resources->data);
      rstatus = buddy_getstatus(bud, resources->data);
      rst_msg = buddy_getstatusmsg(bud, resources->data);

      snprintf(buffer, 127, "Resource: [%c] (%d) %s", imstatus2char[rstatus],
               rprio, (char*)resources->data);
      scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
      if (rst_msg) {
        snprintf(buffer, 127, "Status message: %s", rst_msg);
        scr_WriteIncomingMessage(jid, buffer, 0, HBB_PREFIX_INFO);
      }
    }
  } else {
    if (name) scr_LogPrint(LPRINT_NORMAL, "Name: %s", name);
    scr_LogPrint(LPRINT_NORMAL, "Type: %s",
                 ((type == ROSTER_TYPE_GROUP) ? "group" : "unknown"));
  }

  g_free(buffer);
}

static void do_rename(char *arg)
{
  gpointer bud;
  const char *jid, *group;
  guint type;
  char *newname, *p;

  if (!arg || (!*arg)) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter");
    return;
  }

  if (!current_buddy) return;
  bud = BUDDATA(current_buddy);

  jid   = buddy_getjid(bud);
  group = buddy_getgroupname(bud);
  type  = buddy_gettype(bud);

  if (type & ROSTER_TYPE_GROUP) {
    scr_LogPrint(LPRINT_NORMAL, "You can't rename groups");
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

static void do_move(char *arg)
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
    scr_LogPrint(LPRINT_NORMAL, "You can't move groups!");
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

static void do_set(char *arg)
{
  guint assign;
  const gchar *option, *value;

  assign = parse_assigment(arg, &option, &value);
  if (!option) {
    scr_LogPrint(LPRINT_NORMAL, "Huh?");
    return;
  }
  if (!assign) {
    // This is a query
    value = settings_opt_get(option);
    if (value) {
      scr_LogPrint(LPRINT_NORMAL, "%s = [%s]", option, value);
    } else
      scr_LogPrint(LPRINT_NORMAL, "Option %s is not set", option);
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

static void do_alias(char *arg)
{
  guint assign;
  const gchar *alias, *value;

  assign = parse_assigment(arg, &alias, &value);
  if (!alias) {
    scr_LogPrint(LPRINT_NORMAL, "Huh?");
    return;
  }
  if (!assign) {
    // This is a query
    value = settings_get(SETTINGS_TYPE_ALIAS, alias);
    if (value) {
      scr_LogPrint(LPRINT_NORMAL, "%s = %s", alias, value);
    } else
      scr_LogPrint(LPRINT_NORMAL, "Alias '%s' does not exist", alias);
    return;
  }
  // Check the alias does not conflict with a registered command
  if (cmd_get(alias)) {
      scr_LogPrint(LPRINT_NORMAL, "'%s' is a reserved word!", alias);
      return;
  }
  // Update the alias
  if (!value) {
    if (settings_get(SETTINGS_TYPE_ALIAS, alias)) {
      settings_del(SETTINGS_TYPE_ALIAS, alias);
      // Remove alias from the completion list
      compl_del_category_word(COMPL_CMD, alias);
    }
  } else {
    // Add alias to the completion list, if not already in
    if (!settings_get(SETTINGS_TYPE_ALIAS, alias))
      compl_add_category_word(COMPL_CMD, alias);
    settings_set(SETTINGS_TYPE_ALIAS, alias, value);
  }
}

static void do_bind(char *arg)
{
  guint assign;
  const gchar *keycode, *value;

  assign = parse_assigment(arg, &keycode, &value);
  if (!keycode) {
    scr_LogPrint(LPRINT_NORMAL, "Huh?");
    return;
  }
  if (!assign) {
    // This is a query
    value = settings_get(SETTINGS_TYPE_BINDING, keycode);
    if (value) {
      scr_LogPrint(LPRINT_NORMAL, "Key %s is bound to: %s", keycode, value);
    } else
      scr_LogPrint(LPRINT_NORMAL, "Key %s is not bound", keycode);
    return;
  }
  // Update the key binding
  if (!value)
    settings_del(SETTINGS_TYPE_BINDING, keycode);
  else
    settings_set(SETTINGS_TYPE_BINDING, keycode, value);
}

static void do_rawxml(char *arg)
{
  if (!strncasecmp(arg, "send ", 5))  {
    gchar *buffer;
    for (arg += 5; *arg && *arg == ' '; arg++)
      ;
    buffer = g_locale_to_utf8(arg, -1, NULL, NULL, NULL);
    if (!buffer) {
      scr_LogPrint(LPRINT_NORMAL, "Conversion error in XML string");
      return;
    }
    scr_LogPrint(LPRINT_NORMAL, "Sending XML string");
    jb_send_raw(buffer);
    g_free(buffer);
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Please read the manual page"
                 " before using /rawxml :-)");
  }
}

static void do_room(char *arg)
{
  gpointer bud;

  if (!arg || (!*arg)) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter");
    return;
  }

  bud = BUDDATA(current_buddy);

  if (!strncasecmp(arg, "join", 4))  {
    GSList *roster_usr;
    char *roomname, *nick, *roomid;

    arg += 4;
    if (*arg++ != ' ') {
      scr_LogPrint(LPRINT_NORMAL, "Wrong or missing parameter");
      return;
    }
    for (; *arg && *arg == ' '; arg++)
      ;

    if (strchr(arg, '/')) {
      scr_LogPrint(LPRINT_NORMAL, "Invalid room name");
      return;
    }

    roomname = g_strdup(arg);
    nick = strchr(roomname, ' ');
    if (!nick) {
      scr_LogPrint(LPRINT_NORMAL, "Missing parameter (nickname)");
      g_free(roomname);
      return;
    }

    *nick++ = 0;
    while (*nick && *nick == ' ')
      nick++;
    if (!*nick) {
      scr_LogPrint(LPRINT_NORMAL, "Missing parameter (nickname)");
      g_free(roomname);
      return;
    }
    // room syntax: "room@server/nick"
    roomid = g_strdup_printf("%s/%s", roomname, nick);
    if (check_jid_syntax(roomid)) {
      scr_LogPrint(LPRINT_NORMAL, "<%s> is not a valid Jabber room", roomid);
      g_free(roomname);
      g_free(roomid);
      return;
    }

    mc_strtolower(roomid);
    jb_room_join(roomid);

    // We need to save the nickname for future use
    roster_usr = roster_add_user(roomname, NULL, NULL, ROSTER_TYPE_ROOM);
    if (roster_usr)
      buddy_setnickname(roster_usr->data, nick);

    g_free(roomname);
    g_free(roomid);
    buddylist_build();
    update_roster = TRUE;
  } else if (!strncasecmp(arg, "leave", 5))  {
    char *roomid;
    arg += 5;
    for (; *arg && *arg == ' '; arg++)
      ;
    if (!(buddy_gettype(bud) & ROSTER_TYPE_ROOM)) {
      scr_LogPrint(LPRINT_NORMAL, "This isn't a chatroom");
      return;
    }
    roomid = g_strdup_printf("%s/%s", buddy_getjid(bud),
                             buddy_getnickname(bud));
    jb_setstatus(offline, roomid, arg);
    g_free(roomid);
    buddy_setnickname(bud, NULL);
    buddy_del_all_resources(bud);
    scr_LogPrint(LPRINT_NORMAL, "You have left %s", buddy_getjid(bud));
  } else if (!strcasecmp(arg, "names"))  {
    if (!(buddy_gettype(bud) & ROSTER_TYPE_ROOM)) {
      scr_LogPrint(LPRINT_NORMAL, "This isn't a chatroom");
      return;
    }
    do_info(NULL);
  } else if (!strcasecmp(arg, "remove"))  {
    if (!(buddy_gettype(bud) & ROSTER_TYPE_ROOM)) {
      scr_LogPrint(LPRINT_NORMAL, "This isn't a chatroom");
      return;
    }
    // Quick check: if there are resources, we haven't left
    if (buddy_getresources(bud)) {
      scr_LogPrint(LPRINT_NORMAL, "You haven't left this room!");
      return;
    }
    // Delete the room
    roster_del_user(buddy_getjid(bud));
    buddylist_build();
    update_roster = TRUE;
  } else if (!strcasecmp(arg, "unlock"))  {
    if (!(buddy_gettype(bud) & ROSTER_TYPE_ROOM)) {
      scr_LogPrint(LPRINT_NORMAL, "This isn't a chatroom");
      return;
    }
    jb_room_unlock(buddy_getjid(bud));
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unrecognized parameter!");
  }
}

static void do_connect(char *arg)
{
  mcabber_connect();
}

static void do_disconnect(char *arg)
{
  jb_disconnect();
}
