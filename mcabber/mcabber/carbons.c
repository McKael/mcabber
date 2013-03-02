#include "carbons.h"
#include "settings.h"
#include "xmpp_helper.h"
#include "xmpp_defines.h"
#include "logprint.h"
#include "xmpp.h"

static int _carbons_available = 0;

static LmHandlerResult cb_carbons_enable(LmMessageHandler *h, LmConnection *c,
                                         LmMessage *m, gpointer user_data);


void carbons_init()
{

}

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
  LmMessage *iq;
  LmMessageNode *enable;
  LmMessageHandler *handler;
  GError *error = NULL;

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  enable = lm_message_node_add_child(iq->node, "enable", NULL);

  lm_message_node_set_attribute(enable, "xmlns", NS_CARBONS_2);

  handler = lm_message_handler_new(cb_carbons_enable, NULL, NULL);

  lm_connection_send_with_reply(lconnection, iq, handler, &error);
  lm_message_handler_unref(handler);
  lm_message_unref(iq);

  if (error) {
    scr_LogPrint(LPRINT_LOGNORM, "Error sending IQ request: %s.", error->message);
    g_error_free(error);
  }
}

void carbons_disable()
{

}

static LmHandlerResult cb_carbons_enable(LmMessageHandler *h, LmConnection *c,
                                         LmMessage *m, gpointer user_data)
{
  scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, "We have a response! O Yeah!");
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}
