#ifndef __MCABBER_HISTOLOG_H__
#define __MCABBER_HISTOLOG_H__ 1

#include <glib.h>

#include <mcabber/xmpp.h>

void hlog_enable(guint enable, const char *root_dir, guint loadfile);
char *hlog_get_log_jid(const char *bjid);
void hlog_read_history(const char *bjid, GList **p_buddyhbuf, guint width);
void hlog_write_message(const char *bjid, time_t timestamp, int sent,
                        const char *msg);
void hlog_write_status(const char *bjid, time_t timestamp,
                       enum imstatus status, const char *status_msg);
void hlog_save_state(void);
void hlog_load_state(void);

#endif /* __MCABBER_HISTOLOG_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
