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
#include "utf8.h"
#include "utils.h"


static GSList *Commands;

//  cmd_add()
// Adds a command to the commands list and to the CMD completion list
void cmd_add(const char *name, const char *help,
        guint flags_row1, guint flags_row2, void *(*f)())
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
  //cmd_add("add");
  //cmd_add("clear");
  //cmd_add("del");
  //cmd_add("group");
  //cmd_add("info");
  //cmd_add("move");
  //cmd_add("nick");
  cmd_add("quit", "Exit the software", 0, 0, NULL);
  //cmd_add("rename");
  //cmd_add("request_auth");
  cmd_add("say", "Say something to the selected buddy", 0, 0, NULL);
  //cmd_add("search");
  //cmd_add("send_auth");
  cmd_add("status", "Show or set your status", COMPL_STATUS, 0, NULL);

  // Status category
  compl_add_category_word(COMPL_STATUS, "online");
  compl_add_category_word(COMPL_STATUS, "avail");
  compl_add_category_word(COMPL_STATUS, "invisible");
  compl_add_category_word(COMPL_STATUS, "free");
  compl_add_category_word(COMPL_STATUS, "dnd");
  compl_add_category_word(COMPL_STATUS, "busy");
  compl_add_category_word(COMPL_STATUS, "notavail");
  compl_add_category_word(COMPL_STATUS, "away");
}

//  cmd_get
// Finds command in the command list structure.
// Returns a pointer to the cmd entry, or NULL if command not found.
cmd *cmd_get(char *command)
{
  char *p1, *p2;
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
void send_message(char *msg)
{
  char *buffer;
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

  // UI part
  scr_WriteOutgoingMessage(jid, msg);

  // Network part
  buffer = utf8_encode(msg);
  jb_send_msg(jid, buffer);
  free(buffer);
}

//  process_line(line)
// Process a command/message line.
// If this isn't a command, this is a message and it is sent to the
// currently selected buddy.
int process_line(char *line)
{
  char *p;
  if (*line != '/') {
    send_message(line); // FIXME: are we talking to a _buddy_?
    return 0;
  }

  /* It is a command */
  // Remove trailing spaces:
  for (p=line ; *p ; p++)
    ;
  for (p-- ; p>line && (*p == ' ') ; p--)
    *p = 0;

  // Command "quit"?
  if (!strcasecmp(line, "/quit")) {
    return 255;
  }
  // Commands handling
  // TODO
  // say, send_raw...

  scr_LogPrint("Unrecognised command, sorry.");
  return 0;
}

