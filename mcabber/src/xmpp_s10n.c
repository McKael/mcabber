/* See xmpp.c file for copyright and license details. */

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

