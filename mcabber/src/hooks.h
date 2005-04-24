#ifndef __HOOKS_H__
#define __HOOKS_H__ 1

#include <time.h>
#include "jabglue.h"


inline void hk_message_in(const char *jid, time_t timestamp, const char *msg);
inline void hk_message_out(const char *jid, time_t timestamp, const char *msg);
inline void hk_statuschange(const char *jid, time_t timestamp, 
        enum imstatus status);

#endif /* __HOOKS_H__ */
