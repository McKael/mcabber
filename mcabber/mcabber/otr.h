#ifndef __MCABBER_OTR_H__
#define __MCABBER_OTR_H__ 1

#include <mcabber/config.h>

#ifdef HAVE_LIBOTR

#ifndef HAVE_LIBOTR3
# include <libotr/instag.h>
#endif
#include <libotr/proto.h>
#include <libotr/message.h>
#include <libotr/privkey.h>

enum otr_policy {
  plain,
  opportunistic,
  manual,
  always
};

void otr_init(const char *jid);
void otr_terminate(void);

void otr_establish  (const char * buddy);
void otr_disconnect (const char * buddy);
void otr_fingerprint(const char * buddy, const char * trust);
void otr_print_info (const char * buddy);

void otr_smp_query  (const char * buddy, const char * secret);
void otr_smp_respond(const char * buddy, const char * secret);
void otr_smp_abort  (const char * buddy);

void otr_key        (void);

int  otr_receive    (char **otr_data, const char * buddy, int * free_msg);
int  otr_send       (char **msg, const char *buddy);

#endif /* HAVE_LIBOTR */

int  otr_enabled    (void);

#endif /* __MCABBER_OTR_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
