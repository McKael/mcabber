/*
 * carbons.c        -- Support for Message Carbons (XEP 0280)
 *
 * Copyright (C) 2013 Roeland Jago Douma <roeland@famdouma.nl>
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
  LmMessage *iq;
  LmMessageNode *enable;
  LmMessageHandler *handler;
  GError *error = NULL;

  //We cannot enable carbons if there is no carbons support
  if (_carbons_available == 0) {
    scr_log_print(LPRINT_NORMAL, "Carbons not available on this server!");
    return;
  }

  //We only have to enable carbons once
  if (_carbons_enabled == 1) {
    return;
  }

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  enable = lm_message_node_add_child(iq->node, "enable", NULL);
  lm_message_node_set_attribute(enable, "xmlns", NS_CARBONS_2);
  handler = lm_message_handler_new(cb_carbons, NULL, NULL);

  if (!lm_connection_send_with_reply(lconnection, iq, handler, &error)) {
    scr_log_print(LPRINT_DEBUG, "Error sending IQ request: %s.",
                  error->message);
    g_error_free(error);
  }

  lm_message_handler_unref(handler);
  lm_message_unref(iq);
}

// Mark carbons as disabled (e.g. when a connection terminates)
void carbons_reset()
{
  _carbons_enabled = 0;
}

void carbons_disable()
{
  LmMessage *iq;
  LmMessageNode *disable;
  LmMessageHandler *handler;
  GError *error = NULL;

  //We cannot disable carbons if there is no carbon support on the server
  if (_carbons_available == 0) {
    scr_log_print(LPRINT_NORMAL, "Carbons not available on this server!");
    return;
  }

  //We can only disable carbons if they are disabled
  if (_carbons_enabled == 0) {
    return;
  }

  iq = lm_message_new_with_sub_type(NULL, LM_MESSAGE_TYPE_IQ,
                                    LM_MESSAGE_SUB_TYPE_SET);

  disable = lm_message_node_add_child(iq->node, "disable", NULL);
  lm_message_node_set_attribute(disable, "xmlns", NS_CARBONS_2);
  handler = lm_message_handler_new(cb_carbons, NULL, NULL);

  if (!lm_connection_send_with_reply(lconnection, iq, handler, &error)) {
    scr_log_print(LPRINT_DEBUG, "Error sending IQ request: %s.",
                  error->message);
    g_error_free(error);
  }

  lm_message_handler_unref(handler);
  lm_message_unref(iq);

}

void carbons_info()
{
  if (_carbons_enabled) {
    scr_log_print(LPRINT_NORMAL, "Carbons enabled.");
  } else {
    if (_carbons_available) {
      scr_log_print(LPRINT_NORMAL, "Carbons available, but not enabled.");
    } else {
      scr_log_print(LPRINT_NORMAL, "Carbons not available.");
    }
  }
}

static LmHandlerResult cb_carbons(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer user_data)
{
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_RESULT) {
    _carbons_enabled = (_carbons_enabled == 0 ? 1 : 0);
    if (_carbons_enabled) {
      scr_log_print(LPRINT_NORMAL, "Carbons enabled.");
    } else {
      scr_log_print(LPRINT_NORMAL, "Carbons disabled.");
    }
  } else {
    //Handle error cases
  }

  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
