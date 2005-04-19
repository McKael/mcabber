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

#include "commands.h"
#include "jabglue.h"
#include "roster.h"
#include "screen.h"
#include "utf8.h"
#include "utils.h"


// Command structure
typedef struct {
  char name[32];
  char *help;       // ?
  guint completion_flags;
  void *(*func)();
} cmd;


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
  if (*line != '/') {
    send_message(line);
    return 0;
  }
  if (!strcasecmp(line, "/quit")) {
    return 255;
  }
  // Commands handling
  // TODO
  // say, send_raw...

  scr_LogPrint("Unrecognised command, sorry.");
  return 0;
}

