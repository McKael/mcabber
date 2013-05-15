/*
 * otr.c        -- Off-The-Record Messaging for mcabber
 *
 * Copyright (C) 2007-2009 Frank Zschockelt <mcabber_otr@freakysoft.de>
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

#include <config.h>
#include <glib.h>

#ifdef HAVE_LIBOTR

#include "hbuf.h"
#include "logprint.h"
#include "nohtml.h"
#include "otr.h"
#include "roster.h"
#include "screen.h"
#include "settings.h"
#include "utils.h"
#include "xmpp.h"

#define OTR_PROTOCOL_NAME "jabber"

static OtrlUserState userstate = NULL;
static char *account = NULL;
static char *keyfile = NULL;
static char *fprfile = NULL;

static int otr_is_enabled = FALSE;

static OtrlPolicy cb_policy             (void *opdata, ConnContext *ctx);
static void       cb_create_privkey     (void *opdata,
                                         const char *accountname,
                                         const char *protocol);
static int        cb_is_logged_in       (void *opdata,
                                         const char *accountname,
                                         const char *protocol,
                                         const char *recipient);
static void       cb_inject_message     (void *opdata,
                                         const char *accountname,
                                         const char *protocol,
                                         const char *recipient,
                                         const char *message);
static void       cb_update_context_list(void *opdata);
static void       cb_new_fingerprint    (void *opdata, OtrlUserState us,
                                         const char *accountname,
                                         const char *protocol,
                                         const char *username,
                                         unsigned char fingerprint[20]);
static void       cb_write_fingerprints (void *opdata);
static void       cb_gone_secure        (void *opdata, ConnContext *context);
static void       cb_gone_insecure      (void *opdata, ConnContext *context);
static void       cb_still_secure       (void *opdata, ConnContext *context,
                                         int is_reply);
static int        cb_max_message_size   (void *opdata, ConnContext *context);

#ifdef HAVE_LIBOTR3
static void       cb_notify             (void *opdata,
                                         OtrlNotifyLevel level,
                                         const char *accountname,
                                         const char *protocol,
                                         const char *username,
                                         const char *title,
                                         const char *primary,
                                         const char *secondary);
static int        cb_display_otr_message(void *opdata,
                                         const char *accountname,
                                         const char *protocol,
                                         const char *username,
                                         const char *msg);
static const char *cb_protocol_name     (void *opdata, const char *protocol);
static void       cb_protocol_name_free (void *opdata,
                                         const char *protocol_name);
static void       cb_log_message        (void *opdata, const char *message);

static void       otr_handle_smp_tlvs   (OtrlTLV *tlvs, ConnContext *ctx);
#else /* HAVE_LIBOTR3 */
static char *tagfile = NULL;
static guint otr_timer_source = 0;

static void       cb_handle_smp_event   (void *opdata, OtrlSMPEvent event,
                                         ConnContext *context, unsigned short percent,
                                         char *question);
static void       cb_handle_msg_event   (void *opdata, OtrlMessageEvent event,
                                         ConnContext *context, const char *message,
                                         gcry_error_t err);
static void       cb_create_instag      (void *opdata, const char *accountname,
                                         const char *protocol);
static void       cb_timer_control      (void *opdata, unsigned int interval);
#endif /* HAVE_LIBOTR3 */

static OtrlMessageAppOps ops =
{
  cb_policy,
  cb_create_privkey,
  cb_is_logged_in,
  cb_inject_message,
#ifdef HAVE_LIBOTR3
  cb_notify,
  cb_display_otr_message,
#endif
  cb_update_context_list,
#ifdef HAVE_LIBOTR3
  cb_protocol_name,
  cb_protocol_name_free,
#endif
  cb_new_fingerprint,
  cb_write_fingerprints,
  cb_gone_secure,
  cb_gone_insecure,
  cb_still_secure,
#ifdef HAVE_LIBOTR3
  cb_log_message,
#endif
  cb_max_message_size,
  NULL, /* account_name */
  NULL, /* account_name_free */
#ifndef HAVE_LIBOTR3
  NULL, /* received_symkey */
  NULL, /* otr_error_message */
  NULL, /* otr_error_message_free */
  NULL, /* resent_msg_prefix */
  NULL, /* resent_msg_prefix_free */
  cb_handle_smp_event,
  cb_handle_msg_event,
  cb_create_instag,
  NULL, /* convert_msg */
  NULL, /* convert_free */
  cb_timer_control,
#endif
};

static void otr_message_disconnect(ConnContext *ctx);
static ConnContext *otr_get_context(const char *buddy);
static void otr_startstop(const char *buddy, int start);

static char *otr_get_dir(void);

void otr_init(const char *fjid)
{
  char *root;

  if (userstate) // already initialised
    return;

  otr_is_enabled = !!settings_opt_get_int("otr");

  if (!otr_is_enabled)
    return;

  OTRL_INIT;

  userstate = otrl_userstate_create();

  root = otr_get_dir();
  account = jidtodisp(fjid);
  keyfile = g_strdup_printf("%s%s.key", root, account);
  fprfile = g_strdup_printf("%s%s.fpr", root, account);

  if (otrl_privkey_read(userstate, keyfile)){
    scr_LogPrint(LPRINT_LOGNORM, "Could not read OTR key from %s", keyfile);
    cb_create_privkey(NULL, account, OTR_PROTOCOL_NAME);
  }
  if (otrl_privkey_read_fingerprints(userstate, fprfile, NULL, NULL)){
    scr_LogPrint(LPRINT_LOGNORM, "Could not read OTR fingerprints from %s",
                 fprfile);
  }
#ifndef HAVE_LIBOTR3
  tagfile = g_strdup_printf("%s%s.tag", root, account);
  if (otrl_instag_read(userstate, tagfile)) {
    scr_LogPrint(LPRINT_LOGNORM, "Could not read OTR instance tag from %s", tagfile);
    cb_create_instag(NULL, account, OTR_PROTOCOL_NAME);
  }
#endif
  g_free(root);
}

void otr_terminate(void)
{
  ConnContext *ctx;

  if (!otr_is_enabled)
    return;

#ifndef HAVE_LIBOTR3
  if (otr_timer_source > 0) {
    g_source_remove (otr_timer_source);
    otr_timer_source = 0;
  }
#endif

  for (ctx = userstate->context_root; ctx; ctx = ctx->next)
    if (ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED)
      otr_message_disconnect(ctx);

  g_free(account);
  account = NULL;

  /* XXX This #ifdef is a quick workaround: when mcabber
   * is linked to both gnutls and libotr, libgcrypt will
   * segfault when we call otrl_userstate_free().
   * This is reported to be a bug in libgcrypt :-/
   * Mikael
   */
#if defined(HAVE_GNUTLS) && !defined(HAVE_OPENSSL) // TODO: broken now
  if (!settings_opt_get_int("ssl"))
#endif
  otrl_userstate_free(userstate);

  userstate = NULL;
  g_free(keyfile);
  keyfile = NULL;
  g_free(fprfile);
  fprfile = NULL;
#ifndef HAVE_LIBOTR3
  g_free(tagfile);
  tagfile = NULL;
#endif
}

static char *otr_get_dir(void)
{
  const char *configured_dir = settings_opt_get("otr_dir");

  if (configured_dir && *configured_dir) {
    char *xp_conf_dir;
    int l;
    xp_conf_dir = expand_filename(configured_dir);
    // The path must be slash-terminated
    l = strlen(xp_conf_dir);
    if (xp_conf_dir[l-1] != '/') {
      char *xp_conf_dir_tmp = xp_conf_dir;
      xp_conf_dir = g_strdup_printf("%s/", xp_conf_dir_tmp);
      g_free(xp_conf_dir_tmp);
    }
    return xp_conf_dir;
  } else {
    return expand_filename("~/.mcabber/otr/");
  }
}

static ConnContext *otr_get_context(const char *buddy)
{
  int null = 0;
  ConnContext *ctx;
  char *lowcasebuddy = g_strdup(buddy);

  mc_strtolower(lowcasebuddy);
  ctx = otrl_context_find(userstate, lowcasebuddy, account, OTR_PROTOCOL_NAME,
#ifdef HAVE_LIBOTR3
                          1, &null, NULL, NULL);
#else
                          // INSTAG XXX
                          OTRL_INSTAG_BEST, 1, &null, NULL, NULL);
#endif
  g_free(lowcasebuddy);
  return ctx;
}

static void otr_message_disconnect(ConnContext *ctx)
{
  if (ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED)
    cb_gone_insecure(NULL, ctx);
  otrl_message_disconnect(userstate, &ops, NULL, ctx->accountname,
#ifdef HAVE_LIBOTR3
                          ctx->protocol, ctx->username);
#else
                          // INSTAG XXX
                          ctx->protocol, ctx->username, OTRL_INSTAG_BEST);
#endif
}

static void otr_startstop(const char *buddy, int start)
{
  char *msg = NULL;
  ConnContext *ctx = otr_get_context(buddy);

  if (!userstate || !ctx)
    return;

  if (start && ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED)
    otr_message_disconnect(ctx);

  if (start) {
    OtrlPolicy policy = cb_policy(NULL, ctx);
    if (policy == plain) {
      scr_LogPrint(LPRINT_LOGNORM, "The OTR policy for this user is set to"
      " plain. You have to change it first.");
      return;
    }
    msg = otrl_proto_default_query_msg(ctx->accountname, policy);
    cb_inject_message(NULL, ctx->accountname, ctx->protocol, ctx->username,
                      msg);
    free (msg);
  }
  else
    otr_message_disconnect(ctx);
}

void otr_establish(const char *buddy)
{
  otr_startstop(buddy, 1);
}

void otr_disconnect(const char *buddy)
{
  otr_startstop(buddy, 0);
}

void otr_fingerprint(const char *buddy, const char *trust)
{
  char fpr[45], *tr;
  ConnContext *ctx = otr_get_context(buddy);
  if (!userstate || !ctx)
    return;

  if (!ctx->active_fingerprint || !ctx->active_fingerprint->fingerprint) {
    scr_LogPrint(LPRINT_LOGNORM,
                 "No active fingerprint - start OTR for this buddy first.");
    return;
  }

  otrl_privkey_hash_to_human(fpr, ctx->active_fingerprint->fingerprint);
  if (trust) {
    if (strcmp(fpr, trust) == 0)
      otrl_context_set_trust(ctx->active_fingerprint, "trust");
    else
      otrl_context_set_trust(ctx->active_fingerprint, NULL);
  }

  tr = ctx->active_fingerprint->trust;
  scr_LogPrint(LPRINT_LOGNORM, "%s [%44s]: %s", ctx->username, fpr,
               tr && *tr ?  "trusted" : "untrusted");
  cb_write_fingerprints(NULL);
}

#ifdef HAVE_LIBOTR3

static void otr_handle_smp_tlvs(OtrlTLV *tlvs, ConnContext *ctx)
{
  OtrlTLV *tlv = NULL;
  char *sbuf = NULL;
  NextExpectedSMP nextMsg = ctx->smstate->nextExpected;

  tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP1);
  if (tlv) {
    if (nextMsg != OTRL_SMP_EXPECT1)
      otr_smp_abort(ctx->username);
    else {
      sbuf = g_strdup_printf("OTR: Received SMP Initiation. "
                             "Answer with /otr smpr %s $secret",
                             ctx->username);
    }
  }
  tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP2);
  if (tlv) {
    if (nextMsg != OTRL_SMP_EXPECT2)
      otr_smp_abort(ctx->username);
    else {
      sbuf = g_strdup("OTR: Received SMP Response.");
      /* If we received TLV2, we will send TLV3 and expect TLV4 */
      ctx->smstate->nextExpected = OTRL_SMP_EXPECT4;
    }
  }
  tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP3);
  if (tlv) {
    if (nextMsg != OTRL_SMP_EXPECT3)
      otr_smp_abort(ctx->username);
    else {
      /* If we received TLV3, we will send TLV4
       * We will not expect more messages, so prepare for next SMP */
      ctx->smstate->nextExpected = OTRL_SMP_EXPECT1;
      /* Report result to user */
      if (ctx->active_fingerprint && ctx->active_fingerprint->trust &&
         *ctx->active_fingerprint->trust != '\0')
        sbuf = g_strdup("OTR: SMP succeeded");
      else
        sbuf = g_strdup("OTR: SMP failed");
    }
  }
  tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP4);
  if (tlv) {
    if (nextMsg != OTRL_SMP_EXPECT4)
      otr_smp_abort(ctx->username);
    else {
      /* We will not expect more messages, so prepare for next SMP */
      ctx->smstate->nextExpected = OTRL_SMP_EXPECT1;
      /* Report result to user */
      if (ctx->active_fingerprint && ctx->active_fingerprint->trust &&
         *ctx->active_fingerprint->trust != '\0')
        sbuf = g_strdup("OTR: SMP succeeded");
      else
        sbuf = g_strdup("OTR: SMP failed");
    }
  }
  tlv = otrl_tlv_find(tlvs, OTRL_TLV_SMP_ABORT);
  if (tlv) {
    /* The message we are waiting for will not arrive, so reset
     * and prepare for the next SMP */
    sbuf = g_strdup("OTR: SMP aborted by your buddy");
    ctx->smstate->nextExpected = OTRL_SMP_EXPECT1;
  }

  if (sbuf) {
    scr_WriteIncomingMessage(ctx->username, sbuf, 0, HBB_PREFIX_INFO, 0);
    g_free(sbuf);
  }
}

#else /* HAVE_LIBOTR3 */

static void cb_handle_smp_event(void *opdata, OtrlSMPEvent event,
                                ConnContext *context, unsigned short percent,
                                char *question)
{
  const char *msg = NULL;
  char *freeme = NULL;
  switch (event) {
    case OTRL_SMPEVENT_ASK_FOR_SECRET:
      msg = freeme = g_strdup_printf("OTR: Socialist Millionaires' Protocol: "
                                     "Received SMP Initiation.\n"
                                     "Answer with /otr smpr %s $secret",
                                     context->username);
      break;
    case OTRL_SMPEVENT_ASK_FOR_ANSWER:
      msg = freeme = g_strdup_printf("OTR: Socialist Millionaires' Protocol: "
                                     "Received SMP Initiation.\n"
                                     "Answer with /otr smpr %s $secret\n"
                                     "Question: %s", context->username,
                                     question);
      break;
    case OTRL_SMPEVENT_CHEATED:
      msg = "OTR: Socialist Millionaires' Protocol: Correspondent cancelled negotiation!";
      otrl_message_abort_smp(userstate, &ops, opdata, context);
      break;
    case OTRL_SMPEVENT_IN_PROGRESS:
      scr_log_print(LPRINT_DEBUG, "OTR: Socialist Millionaires' Protocol: "
                                  "Negotiation is in pogress...");
      break;
    case OTRL_SMPEVENT_SUCCESS:
      msg = "OTR: Socialist Millionaires' Protocol: Success!";
      break;
    case OTRL_SMPEVENT_FAILURE:
      msg = "OTR: Socialist Millionaires' Protocol: Failure.";
      break;
    case OTRL_SMPEVENT_ABORT:
      msg = "OTR: Socialist Millionaires' Protocol: Aborted.";
      break;
    case OTRL_SMPEVENT_ERROR:
      msg = "OTR: Socialist Millionaires' Protocol: Error occured, aborting negotiations!";
      otrl_message_abort_smp(userstate, &ops, opdata, context);
      break;
    default:
      break;
  }

  if (msg) {
    scr_WriteIncomingMessage(context->username, msg, 0, HBB_PREFIX_INFO, 0);
    g_free(freeme);
  }
}

static void cb_handle_msg_event(void *opdata, OtrlMessageEvent event,
                                ConnContext *context, const char *message,
                                gcry_error_t err)
{
  const char *msg = NULL;
  char *freeme = NULL;
  switch (event) {
    case OTRL_MSGEVENT_ENCRYPTION_REQUIRED:
      msg = "OTR: Policy requires encryption on message!";
      break;
    case OTRL_MSGEVENT_ENCRYPTION_ERROR:
      msg = "OTR: Encryption error! Message not sent.";
      break;
    case OTRL_MSGEVENT_CONNECTION_ENDED:
      msg = "OTR: Connection closed by remote end, message lost. "
            "Close or refresh connection.";
      break;
    case OTRL_MSGEVENT_SETUP_ERROR:
      // FIXME
      msg = freeme = g_strdup_printf("OTR: Error setting up private conversation: %u",
                                     err);
      break;
    case OTRL_MSGEVENT_MSG_REFLECTED:
      msg = "OTR: Received own OTR message!";
      break;
    case OTRL_MSGEVENT_MSG_RESENT:
      msg = "OTR: Previous message was resent.";
      break;
    case OTRL_MSGEVENT_RCVDMSG_NOT_IN_PRIVATE:
      msg = "OTR: Received encrypted message, but connection is not established " \
            "yet! Message lost.";
      break;
    case OTRL_MSGEVENT_RCVDMSG_UNREADABLE:
      msg = "OTR: Unable to read incoming message!";
      break;
    case OTRL_MSGEVENT_RCVDMSG_MALFORMED:
      msg = "OTR: Malformed incoming message!";
      break;
    case OTRL_MSGEVENT_LOG_HEARTBEAT_RCVD:
      scr_log_print(LPRINT_DEBUG, "OTR: Received heartbeat.");
      break;
    case OTRL_MSGEVENT_LOG_HEARTBEAT_SENT:
      scr_log_print(LPRINT_DEBUG, "OTR: Sent heartbeat.");
      break;
    case OTRL_MSGEVENT_RCVDMSG_GENERAL_ERR:
      msg = freeme = g_strdup_printf("OTR: Received general otr error: %s",
                                     message);
      break;
    case OTRL_MSGEVENT_RCVDMSG_UNENCRYPTED:
      msg = freeme = g_strdup_printf("OTR: Received unencrypted message: %s",
                                     message);
      break;
    case OTRL_MSGEVENT_RCVDMSG_UNRECOGNIZED:
      msg = "OTR: Unable to determine type of received OTR message!";
      break;
    case OTRL_MSGEVENT_RCVDMSG_FOR_OTHER_INSTANCE:
      // XXX
      scr_log_print(LPRINT_DEBUG, "OTR: Received message for other instance.");
      break;
    default:
      break;
  }

  if (msg) {
    scr_WriteIncomingMessage(context->username, msg, 0, HBB_PREFIX_INFO, 0);
    g_free(freeme);
  }
}

#endif /* HAVE_LIBOTR3 */

/*
 * returns whether a otr_message was received
 * sets *otr_data to NULL, when it was an internal otr message
 */
int otr_receive(char **otr_data, const char *buddy, int *free_msg)
{
  int ignore_message;
  char *newmessage = NULL;
#ifdef HAVE_LIBOTR3
  OtrlTLV *tlvs = NULL;
  OtrlTLV *tlv = NULL;
#endif
  ConnContext *ctx;

  ctx = otr_get_context(buddy);
  *free_msg = 0;
  ignore_message = otrl_message_receiving(userstate, &ops, NULL,
                                          ctx->accountname, ctx->protocol,
                                          ctx->username, *otr_data,
#ifdef HAVE_LIBOTR3
                                          &newmessage, &tlvs, NULL, NULL);

  tlv = otrl_tlv_find(tlvs, OTRL_TLV_DISCONNECTED);
  if (tlv) {
    /* Notify the user that the other side disconnected. */
    if (ctx) {
      cb_gone_insecure(NULL, ctx);
      otr_disconnect(ctx->username);
    }
  }

  otr_handle_smp_tlvs(tlvs, ctx);

  if (tlvs != NULL)
    otrl_tlv_free(tlvs);
#else
                                          &newmessage, NULL, NULL, NULL, NULL);
#endif

  if (ignore_message)
    *otr_data = NULL;

  if (!ignore_message && newmessage) {
    *free_msg = 1;
    *otr_data = html_strip(newmessage);
    otrl_message_free(newmessage);
    if (ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED)
      return 1;
  }
  return 0;
}

int otr_send(char **msg, const char *buddy)
{
  gcry_error_t err;
  char *newmessage = NULL;
  char *htmlmsg;
  ConnContext *ctx = otr_get_context(buddy);

  if (ctx->msgstate == OTRL_MSGSTATE_PLAINTEXT)
    err = otrl_message_sending(userstate, &ops, NULL, ctx->accountname,
#ifdef HAVE_LIBOTR3
                               ctx->protocol, ctx->username, *msg, NULL,
                               &newmessage, NULL, NULL);
#else
                               // INSTAG XXX
                               ctx->protocol, ctx->username, OTRL_INSTAG_BEST,
                               *msg, NULL, &newmessage, OTRL_FRAGMENT_SEND_SKIP,
                               NULL, NULL, NULL);
#endif
  else {
    htmlmsg = html_escape(*msg);
    err = otrl_message_sending(userstate, &ops, NULL, ctx->accountname,
#ifdef HAVE_LIBOTR3
                               ctx->protocol, ctx->username, htmlmsg, NULL,
                               &newmessage, NULL, NULL);
#else
                               // INSTAG XXX
                               ctx->protocol, ctx->username, OTRL_INSTAG_BEST,
                               htmlmsg, NULL, &newmessage, OTRL_FRAGMENT_SEND_SKIP,
                               NULL, NULL, NULL);
#endif
    g_free(htmlmsg);
  }

  if (err)
    *msg = NULL; /*something went wrong, don't send the plain-message! */

  if (!err && newmessage) {
    *msg = g_strdup(newmessage);
    otrl_message_free(newmessage);
    if (cb_policy(NULL, ctx) & OTRL_POLICY_REQUIRE_ENCRYPTION ||
        ctx->msgstate == OTRL_MSGSTATE_ENCRYPTED)
      return 1;
  }
  return 0;
}

/* Prints OTR connection state */
void otr_print_info(const char *buddy)
{
  const char *state, *auth, *policy;
  ConnContext *ctx = otr_get_context(buddy);
  OtrlPolicy p = cb_policy(ctx->app_data, ctx);

  if (!userstate || !ctx)
    return;

  switch (ctx->msgstate) {
    case OTRL_MSGSTATE_PLAINTEXT: state = "plaintext"; break;
    case OTRL_MSGSTATE_ENCRYPTED:
     switch (ctx->protocol_version) {
       case 1: state = "encrypted V1"; break;
       case 2: state = "encrypted V2"; break;
       default:state = "encrypted";
     };
     break;
    case OTRL_MSGSTATE_FINISHED:  state = "finished";  break;
    default:                      state = "unknown state";
  }
  switch (ctx->auth.authstate) {
    case OTRL_AUTHSTATE_NONE:
      switch (ctx->otr_offer) {
        case OFFER_NOT:      auth = "no offer sent";  break;
        case OFFER_SENT:     auth = "offer sent";     break;
        case OFFER_ACCEPTED: auth = "offer accepted"; break;
        case OFFER_REJECTED: auth = "offer rejected"; break;
        default:             auth = "unknown auth";
      }
      break;
    case OTRL_AUTHSTATE_AWAITING_DHKEY:
      auth = "awaiting D-H key";          break;
    case OTRL_AUTHSTATE_AWAITING_REVEALSIG:
      auth = "awaiting reveal signature"; break;
    case OTRL_AUTHSTATE_AWAITING_SIG:
      auth = "awaiting signature";        break;
    case OTRL_AUTHSTATE_V1_SETUP:
      auth = "v1 setup";                  break;
    default:
      auth = "unknown auth";
  }
  if (p == OTRL_POLICY_NEVER)
    policy = "plain";
  else if (p == (OTRL_POLICY_OPPORTUNISTIC & ~OTRL_POLICY_ALLOW_V1))
    policy = "opportunistic";
  else if (p == (OTRL_POLICY_MANUAL & ~OTRL_POLICY_ALLOW_V1))
    policy = "manual";
  else if (p == (OTRL_POLICY_ALWAYS & ~OTRL_POLICY_ALLOW_V1))
    policy = "always";
  else
    policy = "unknown";

  scr_LogPrint(LPRINT_LOGNORM, "%s: %s (%s) [%s]",
               ctx->username, state, auth, policy);
}

static ConnContext *otr_context_encrypted(const char *buddy)
{
  ConnContext *ctx = otr_get_context(buddy);

  if (!userstate || !ctx  || ctx->msgstate != OTRL_MSGSTATE_ENCRYPTED){
    scr_LogPrint(LPRINT_LOGNORM,
                 "You have to start an OTR channel with %s before you can "
                 "use SMP.", buddy);
    return NULL;
  }

  return ctx;
}

void otr_smp_query(const char *buddy, const char *secret)
{
  ConnContext *ctx = otr_context_encrypted(buddy);

  if (!secret) {
    scr_LogPrint(LPRINT_LOGNORM,
                 "Using SMP without a secret isn't a good idea.");
    return;
  }

  if (ctx) {
    otrl_message_initiate_smp(userstate, &ops, NULL, ctx,
                              (const unsigned char *)secret,
                              strlen(secret));
    scr_WriteIncomingMessage(ctx->username,
                             "OTR: Socialist Millionaires' Protocol "
                             "initiated.", 0, HBB_PREFIX_INFO, 0);
  }
}

void otr_smp_respond(const char *buddy, const char *secret)
{
  ConnContext *ctx = otr_context_encrypted(buddy);

  if (!secret) {
    scr_LogPrint(LPRINT_LOGNORM,
                 "Using SMP without a secret isn't a good idea.");
    return;
  }

  if (ctx) {
    if (!ctx->smstate->secret) {
      scr_LogPrint(LPRINT_LOGNORM,
                   "Don't call smpr until you have received an SMP "
                   "Initiation!");
      return;
    }
    otrl_message_respond_smp(userstate, &ops, NULL, ctx,
                             (const unsigned char *)secret,
                             strlen(secret));
    scr_WriteIncomingMessage(ctx->username,
                             "OTR: Socialist Millionaires' Protocol: "
                             "response sent", 0, HBB_PREFIX_INFO, 0);
  }
}

void otr_smp_abort(const char *buddy)
{
  ConnContext *ctx = otr_context_encrypted(buddy);

  if (ctx) {
    otrl_message_abort_smp(userstate, &ops, NULL, ctx);
    scr_WriteIncomingMessage(ctx->username,
                             "OTR: Socialist Millionaires' Protocol aborted.",
                             0, HBB_PREFIX_INFO, 0);
  }
}

void otr_key(void)
{
  OtrlPrivKey *key;
  char readable[45] = "";

  if(!userstate)
    return;
  for (key = userstate->privkey_root; key; key = key->next) {
    otrl_privkey_fingerprint(userstate, readable, key->accountname,
                             key->protocol);
    scr_LogPrint(LPRINT_LOGNORM, "%s: %s", key->accountname, readable);
  }
}

/* Return the OTR policy for the given context. */
static OtrlPolicy cb_policy(void *opdata, ConnContext *ctx)
{
  enum otr_policy p = settings_otr_getpolicy(NULL);

  if(ctx)
    if(settings_otr_getpolicy(ctx->username))
      p = settings_otr_getpolicy(ctx->username);

  switch (p) {
    case plain:
      return OTRL_POLICY_NEVER;
    case opportunistic:
      return OTRL_POLICY_OPPORTUNISTIC & ~OTRL_POLICY_ALLOW_V1;
    case manual:
      return OTRL_POLICY_MANUAL & ~OTRL_POLICY_ALLOW_V1;
    case always:
      return OTRL_POLICY_ALWAYS & ~OTRL_POLICY_ALLOW_V1;
  }

  return OTRL_POLICY_MANUAL & ~OTRL_POLICY_ALLOW_V1;
}

/* Create a private key for the given accountname/protocol if
 * desired. */
static void cb_create_privkey(void *opdata, const char *accountname,
                              const char *protocol)
{
  gcry_error_t e;
  char *root;

  scr_LogPrint(LPRINT_LOGNORM,
               "Generating new OTR key for %s. This may take a while...",
               accountname);
  scr_do_update();

  e = otrl_privkey_generate(userstate, keyfile, accountname, protocol);

  if (e) {
    root = otr_get_dir();
    scr_LogPrint(LPRINT_LOGNORM, "OTR key generation failed! Please mkdir "
                 "%s if you want to use otr encryption.", root);
    g_free(root);
  }
  else
    scr_LogPrint(LPRINT_LOGNORM, "OTR key generated.");
}

/* Report whether you think the given user is online.  Return 1 if
 * you think he is, 0 if you think he isn't, -1 if you're not sure.
 * If you return 1, messages such as heartbeats or other
 * notifications may be sent to the user, which could result in "not
 * logged in" errors if you're wrong. */
static int cb_is_logged_in(void *opdata, const char *accountname,
                           const char *protocol, const char *recipient)
{
  int ret = (roster_getstatus(recipient, NULL) != offline);
  return ret;
}

/* Send the given IM to the given recipient from the given
 * accountname/protocol. */
static void cb_inject_message(void *opdata, const char *accountname,
                              const char *protocol, const char *recipient,
                              const char *message)
{
  if (roster_gettype(recipient) == ROSTER_TYPE_USER)
    xmpp_send_msg(recipient, message, ROSTER_TYPE_USER, "", TRUE, NULL,
                  LM_MESSAGE_SUB_TYPE_NOT_SET, NULL);
}

/* When the list of ConnContexts changes (including a change in
 * state), this is called so the UI can be updated. */
static void cb_update_context_list(void *opdata)
{
  /*maybe introduce new status characters for mcabber,
   * then use this function (?!)*/
}

/* A new fingerprint for the given user has been received. */
static void cb_new_fingerprint(void *opdata, OtrlUserState us,
                               const char *accountname, const char *protocol,
                               const char *username,
                               unsigned char fingerprint[20])
{
  char *sbuf = NULL;
  char readable[45];

  otrl_privkey_hash_to_human(readable, fingerprint);
  sbuf = g_strdup_printf("OTR: new fingerprint: %s", readable);
  scr_WriteIncomingMessage(username, sbuf, 0, HBB_PREFIX_INFO, 0);
  g_free(sbuf);
}

/* The list of known fingerprints has changed.  Write them to disk. */
static void cb_write_fingerprints(void *opdata)
{
  otrl_privkey_write_fingerprints(userstate, fprfile);
}

/* A ConnContext has entered a secure state. */
static void cb_gone_secure(void *opdata, ConnContext *context)
{
  scr_WriteIncomingMessage(context->username, "OTR: channel established", 0,
                           HBB_PREFIX_INFO, 0);
}

/* A ConnContext has left a secure state. */
static void cb_gone_insecure(void *opdata, ConnContext *context)
{
  scr_WriteIncomingMessage(context->username, "OTR: channel closed", 0,
                           HBB_PREFIX_INFO, 0);
}

/* We have completed an authentication, using the D-H keys we
 * already knew.  is_reply indicates whether we initiated the AKE. */
static void cb_still_secure(void *opdata, ConnContext *context, int is_reply)
{
  scr_WriteIncomingMessage(context->username, "OTR: channel reestablished", 0,
                           HBB_PREFIX_INFO, 0);
}

#ifdef HAVE_LIBOTR3

/* Display a notification message for a particular
 * accountname / protocol / username conversation. */
static void cb_notify(void *opdata, OtrlNotifyLevel level,
                      const char *accountname, const char *protocol,
                      const char *username, const char *title,
                      const char *primary, const char *secondary)
{
  char *type;
  char *sbuf = NULL;
  switch (level) {
    case OTRL_NOTIFY_ERROR:   type = "error";   break;
    case OTRL_NOTIFY_WARNING: type = "warning"; break;
    case OTRL_NOTIFY_INFO:    type = "info";    break;
    default:                  type = "unknown";
  }
  sbuf = g_strdup_printf("OTR %s:%s\n%s\n%s",type,title, primary, secondary);
  scr_WriteIncomingMessage(username, sbuf, 0, HBB_PREFIX_INFO, 0);
  g_free(sbuf);
}

/* Display an OTR control message for a particular
 * accountname / protocol / username conversation.  Return 0 if you are able
 * to successfully display it.  If you return non-0 (or if this
 * function is NULL), the control message will be displayed inline,
 * as a received message, or else by using the above notify()
 * callback. */
static int cb_display_otr_message(void *opdata, const char *accountname,
                                  const char *protocol, const char *username,
                                  const char *msg)
{
  char *strippedmsg = html_strip(msg);
  scr_WriteIncomingMessage(username, strippedmsg, 0, HBB_PREFIX_INFO, 0);
  g_free(strippedmsg);
  return 0;
}

/* Return a newly allocated string containing a human-friendly name
 * for the given protocol id */
static const char *cb_protocol_name(void *opdata, const char *protocol)
{
  return protocol;
}

/* Deallocate a string allocated by protocol_name */
static void cb_protocol_name_free (void *opdata, const char *protocol_name)
{
  /* We didn't allocated memory, so we don't have to free anything :p */
}

/* Log a message.  The passed message will end in "\n". */
static void cb_log_message(void *opdata, const char *message)
{
  scr_LogPrint(LPRINT_DEBUG, "OTR: %s", message);
}

#else /* HAVE_LIBOTR3 */

/* Generate unique instance tag for account. */
static void cb_create_instag(void *opdata, const char *accountname,
                             const char *protocol)
{
  if (otrl_instag_generate(userstate, tagfile, accountname, protocol)) {
    scr_LogPrint(LPRINT_LOGNORM, "OTR instance tag generation failed!");
  }
}

static gboolean otr_timer_cb(gpointer userdata)
{
  otrl_message_poll(userstate, &ops, userdata);
  return TRUE;
}

static void cb_timer_control(void *opdata, unsigned int interval)
{
  if (otr_timer_source > 0) {
    g_source_remove(otr_timer_source);
    otr_timer_source = 0;
  }
  if (interval > 0)
    otr_timer_source = g_timeout_add_seconds(interval, otr_timer_cb, opdata);
}

#endif /* HAVE_LIBOTR3 */

/* Find the maximum message size supported by this protocol. */
static int cb_max_message_size(void *opdata, ConnContext *context)
{
  return 8192;
}

int otr_enabled(void)
{
  return otr_is_enabled;
}

#else /* !HAVE_LIBOTR */

int otr_enabled(void)
{
  return FALSE;
}

#endif /* HAVE_LIBOTR */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
