#ifndef __MCABBER_EVENTS_H__
#define __MCABBER_EVENTS_H__ 1

#include <mcabber/config.h>

#define EVS_DEFAULT_TIMEOUT 90
#define EVS_MAX_TIMEOUT     432000

#define EVS_CONTEXT_TIMEOUT 0U
#define EVS_CONTEXT_CANCEL  1U
#define EVS_CONTEXT_ACCEPT  2U
#define EVS_CONTEXT_REJECT  3U
/* There can be other user-defined contexts */

typedef gboolean (*evs_callback_t)(guint context, const char *arg, gpointer userdata);

const char *evs_new(const char *description, const char *id, time_t timeout, evs_callback_t callback, gpointer userdata, GDestroyNotify notify);
int         evs_del(const char *evid);
int         evs_callback(const char *evid, guint evcontext, const char *arg);
void        evs_display_list(void);
GSList     *evs_geteventslist(void);
void        evs_deinit(void);

#endif /* __MCABBER_EVENTS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
