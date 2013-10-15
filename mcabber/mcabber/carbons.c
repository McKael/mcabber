#include "carbons.h"
#include "settings.h"
#include "xmpp_helper.h"
#include "xmpp_defines.h"
#include "logprint.h"
#include "xmpp.h"

static int _carbons_available = 0;
static int _carbons_enabled = 0;

static LmHandlerResult cb_carbons(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer user_data);


void carbons_available()
{
  int enable = 0;
  _carbons_available = 1;

  enable = settings_opt_get_int("carbons");

  if (enable) {
    carbons_enable();
  }
}

void carbons_enable()
{
  //We cannot enable carbons if there is no carbons support
  if (_carbons_available == 0) {
    scr_LogPrint(LPRINT_NORMAL, "Carbons not available on this server!");
    return;
  }

  //We only have to enable carbons once
  if (_carbons_enabled == 1) {
    return;
  }

  LmMessage *iq;
  LmMessageNode *enable;
  LmMessageHandler *handler;
  GError *error = NULL;

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  enable = lm_message_node_add_child(iq->node, "enable", NULL);
  lm_message_node_set_attribute(enable, "xmlns", NS_CARBONS_2);
  handler = lm_message_handler_new(cb_carbons, NULL, NULL);

  if (!lm_connection_send_with_reply(lconnection, iq, handler, &error)) {
    scr_LogPrint(LPRINT_DEBUG, "Error sending IQ request: %s.", error->message);
    g_error_free(error);
  }

  lm_message_handler_unref(handler);
  lm_message_unref(iq);
}

void carbons_disable()
{
  //We cannot disable carbons if there is no carbon support on the server
  if (_carbons_available == 0) {
    scr_LogPrint(LPRINT_NORMAL, "Carbons not available on this server!");
    return;
  }

  //We can only disable carbons if they are disabled
  if (_carbons_enabled == 0) {
    return;
  } 

  LmMessage *iq;
  LmMessageNode *disable;
  LmMessageHandler *handler;
  GError *error = NULL;

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  disable = lm_message_node_add_child(iq->node, "disable", NULL);
  lm_message_node_set_attribute(disable, "xmlns", NS_CARBONS_2);
  handler = lm_message_handler_new(cb_carbons, NULL, NULL);

  if (!lm_connection_send_with_reply(lconnection, iq, handler, &error)) {
    scr_LogPrint(LPRINT_DEBUG, "Error sending IQ request: %s.", error->message);
    g_error_free(error);
  }

  lm_message_handler_unref(handler);
  lm_message_unref(iq);
 
}

void carbons_info()
{
  if (_carbons_enabled) {
    scr_LogPrint(LPRINT_NORMAL, "Carbons enabled.");
  } else {
    if (_carbons_available) {
      scr_LogPrint(LPRINT_NORMAL, "Carbons available, but not enabled.");
    } else {
      scr_LogPrint(LPRINT_NORMAL, "Carbons not available.");     
    }
  }
}

static LmHandlerResult cb_carbons(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer user_data)
{
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_RESULT) {
    _carbons_enabled = _carbons_enabled == 0 ? 1 : 0;
  } else {
    //Handle error cases
  }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

