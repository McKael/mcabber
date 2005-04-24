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

#include <screen.h>

#include "hooks.h"
#include "roster.h"
#include "histolog.h"


inline void hk_message_in(const char *jid, time_t timestamp, const char *msg)
{
  scr_WriteIncomingMessage(jid, msg);
  hlog_write_message(jid, timestamp, FALSE, msg);
}

inline void hk_message_out(const char *jid, time_t timestamp, const char *msg)
{
  scr_WriteOutgoingMessage(jid, msg);
  hlog_write_message(jid, timestamp, TRUE, msg);
}

inline void hk_statuschange(const char *jid, time_t timestamp, 
        enum imstatus status)
{
  scr_LogPrint("Buddy status has changed: [%c>%c] <%s>",
          imstatus2char[roster_getstatus(jid)], imstatus2char[status], jid);
  roster_setstatus(jid, status);
  hlog_write_status(jid, 0, status);
}

