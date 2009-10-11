/*
 * xmpp_s10n.c  -- Jabber presence subscription handling
 *
 * Copyright (C) 2008-2009 Frank Zschockelt <mcabber@freakysoft.de>
 * Copyright (C) 2005-2009 Mikael Berthe <mikael@lilotux.net>
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

//  xmpp_send_s10n(jid, subtype)
// Send a s10n message with the passed subtype
void xmpp_send_s10n(const char *bjid, LmMessageSubType type)
{
  LmMessage *x = lm_message_new_with_sub_type(bjid,
                                              LM_MESSAGE_TYPE_PRESENCE,
                                              type);
  lm_connection_send(lconnection, x, NULL);
  lm_message_unref(x);
}

static int evscallback_subscription(eviqs *evp, guint evcontext)
{
  char *barejid;
  char *buf;

  if (evcontext == EVS_CONTEXT_TIMEOUT) {
    scr_LogPrint(LPRINT_LOGNORM, "Event %s timed out, cancelled.",
                 evp->id);
    return 0;
  }
  if (evcontext == EVS_CONTEXT_CANCEL) {
    scr_LogPrint(LPRINT_LOGNORM, "Event %s cancelled.", evp->id);
    return 0;
  }
  if (!(evcontext & EVS_CONTEXT_USER))
    return 0;

  // Sanity check
  if (!evp->data) {
    // Shouldn't happen, data should be set to the barejid.
    scr_LogPrint(LPRINT_LOGNORM, "Error in evs callback.");
    return 0;
  }

  // Ok, let's work now.
  // evcontext: 0, 1 == reject, accept

  barejid = evp->data;

  if (evcontext & ~EVS_CONTEXT_USER) {
    // Accept subscription request
    xmpp_send_s10n(barejid, LM_MESSAGE_SUB_TYPE_SUBSCRIBED);
    buf = g_strdup_printf("<%s> is allowed to receive your presence updates",
                          barejid);
  } else {
    // Reject subscription request
    xmpp_send_s10n(barejid, LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED);
    buf = g_strdup_printf("<%s> won't receive your presence updates", barejid);
    if (settings_opt_get_int("delete_on_reject")) {
      // Remove the buddy from the roster if there is no current subscription
      if (roster_getsubscription(barejid) == sub_none)
        xmpp_delbuddy(barejid);
    }
  }
  scr_WriteIncomingMessage(barejid, buf, 0, HBB_PREFIX_INFO, 0);
  scr_LogPrint(LPRINT_LOGNORM, "%s", buf);
  g_free(buf);
  return 0;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
