#ifndef __EVENTS_H__
#define __EVENTS_H__ 1

#include "jabglue.h"


#define EVS_DEFAULT_TIMEOUT 90
#define EVS_MAX_TIMEOUT     432000

#define EVS_CONTEXT_TIMEOUT 0
#define EVS_CONTEXT_CANCEL  1
#define EVS_CONTEXT_USER    2

typedef enum {
  EVS_TYPE_SUBSCRIPTION = 1
} evs_type;

/* Common structure for events (evs) and IQ requests (iqs) */
typedef struct {
  char *id;
  time_t ts_create;
  time_t ts_expire;
  guint8 type;
  gpointer data;
  void (*callback)();
  xmlnode xmldata;
  char *desc;
} eviqs;

eviqs  *evs_new(guint8 type, time_t timeout);
int     evs_del(const char *evid);
int     evs_callback(const char *evid, guint evcontext);
void    evs_check_timeout(time_t now_t);
void    evs_display_list(void);
GSList *evs_geteventscomplist(void);

#endif /* __EVENTS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
