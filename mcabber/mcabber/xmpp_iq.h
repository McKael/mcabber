#ifndef __MCABBER_XMPP_IQ_H__
#define __MCABBER_XMPP_IQ_H__ 1

LmHandlerResult handle_iq_dummy(LmMessageHandler *h,
                                LmConnection *c,
                                LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_commands(LmMessageHandler *h,
                                   LmConnection *c,
                                   LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_disco_items(LmMessageHandler *h,
                                      LmConnection *c,
                                      LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_disco_info(LmMessageHandler *h,
                                     LmConnection *c,
                                     LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_roster(LmMessageHandler *h, LmConnection *c,
                                 LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_ping(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_last(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_version(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_time(LmMessageHandler *h, LmConnection *c,
                               LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_time202(LmMessageHandler *h, LmConnection *c,
                                  LmMessage *m, gpointer ud);
LmHandlerResult handle_iq_vcard(LmMessageHandler *h, LmConnection *c,
                                LmMessage *m, gpointer ud);

void send_iq_error(LmConnection *c, LmMessage *m, guint error);

#endif /* __MCABBER_XMPP_IQ_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
