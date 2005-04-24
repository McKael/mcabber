#ifndef __HISTOLOG_H__
#define __HISTOLOG_H__ 1

#include <glib.h>

#include "jabglue.h"

void hlog_enable(guint enable, char *root_dir);
inline void hlog_write_message(const char *jid, time_t timestamp, int sent,
        const char *msg);
inline void hlog_write_status(const char *jid, time_t timestamp,
        enum imstatus status);

#endif /* __HISTOLOG_H__ */

