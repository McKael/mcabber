#ifndef __OTR_H__
#define __OTR_H__ 1

#ifdef HAVE_LIBOTR

#include <libotr/proto.h>
#include <libotr/message.h>

enum otr_policy {
  plain,
  opportunistic,
  manual,
  always
};

int  otr_init(const char *jid);
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

#endif /* __OTR_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
