#ifndef __EVENTS_H__
#define __EVENTS_H__ 1

#include "jabglue.h"


#define EVS_DEFAULT_TIMEOUT 90
#define EVS_MAX_TIMEOUT     600

#define EVS_CONTEXT_USER    0
#define EVS_CONTEXT_TIMEOUT 1

/* Common structure for events (evs) and IQ requests (iqs) */
typedef struct {
  char *id;
  time_t ts_create;
  time_t ts_expire;
  guint8 type;
  gpointer data;
  void (*callback)();
  xmlnode xmldata;
} eviqs;


#endif /* __EVENTS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
