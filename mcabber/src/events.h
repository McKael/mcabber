#ifndef __EVENTS_H__
#define __EVENTS_H__ 1

#include "xmpp.h"


#define EVS_DEFAULT_TIMEOUT 90
#define EVS_MAX_TIMEOUT     432000

#define EVS_CONTEXT_TIMEOUT 0U
#define EVS_CONTEXT_CANCEL  1U
#define EVS_CONTEXT_USER    2U

typedef enum {
  EVS_TYPE_SUBSCRIPTION = 1,
  EVS_TYPE_INVITATION = 2
} evs_type;

/* Common structure for events (evs) and IQ requests (iqs) */
typedef struct {
  char *id;
  time_t ts_create;
  time_t ts_expire;
  guint8 type;
  gpointer data;
  int (*callback)();
  char *desc;
} eviqs;

typedef struct {
  char* to;
  char* from;
  char* passwd;
  char* reason;
} event_muc_invitation;

eviqs   *evs_new(guint8 type, time_t timeout);
int      evs_del(const char *evid);
int      evs_callback(const char *evid, guint evcontext);
gboolean evs_check_timeout();
void     evs_display_list(void);
GSList  *evs_geteventslist(int forcompl);

#endif /* __EVENTS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
