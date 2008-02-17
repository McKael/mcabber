#ifndef __HOOKS_H__
#define __HOOKS_H__ 1

#include <time.h>
#include "jabglue.h"


void hk_mainloop(void);
void hk_message_in(const char *bjid, const char *resname,
                          time_t timestamp, const char *msg, const char *type,
                          guint encrypted);
void hk_message_out(const char *bjid, const char *nickname,
                           time_t timestamp, const char *msg, guint encrypted);
void hk_statuschange(const char *bjid, const char *resname, gchar prio,
                            time_t timestamp, enum imstatus status,
                            char const *status_msg);
void hk_mystatuschange(time_t timestamp,
                              enum imstatus old_status,
                              enum imstatus new_status, const char *msg);

void hook_execute_internal(const char *hookname);

void hk_ext_cmd_init(const char *command);
void hk_ext_cmd(const char *bjid, guchar type, guchar info, const char *data);

#endif /* __HOOKS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
