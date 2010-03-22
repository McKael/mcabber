#ifndef __MCABBER_HOOKS_H__
#define __MCABBER_HOOKS_H__ 1

#include <time.h>
#include <loudmouth/loudmouth.h>
#include <mcabber/xmpp.h>

// These two defines are used by hk_message_{in,out} arguments
#define ENCRYPTED_PGP   1
#define ENCRYPTED_OTR   2

#include <mcabber/config.h>
#ifdef MODULES_ENABLE
#include <glib.h>

// Core hooks
#define HOOK_PRE_MESSAGE_IN     "hook-pre-message-in"
#define HOOK_POST_MESSAGE_IN    "hook-post-message-in"
#define HOOK_MESSAGE_OUT        "hook-message-out"
#define HOOK_STATUS_CHANGE      "hook-status-change"
#define HOOK_MY_STATUS_CHANGE   "hook-my-status-change"
#define HOOK_POST_CONNECT       "hook-post-connect"
#define HOOK_PRE_DISCONNECT     "hook-pre-disconnect"
#define HOOK_UNREAD_LIST_CHANGE "hook-unread-list-change"

typedef enum {
  HOOK_HANDLER_RESULT_ALLOW_MORE_HANDLERS = 0,
  HOOK_HANDLER_RESULT_NO_MORE_HANDLER,
  HOOK_HANDLER_RESULT_NO_MORE_HANDLER_DROP_DATA,
} hk_handler_result;

typedef struct {
  const char *name;
  const char *value;
} hk_arg_t;

typedef guint (*hk_handler_t) (const gchar *hookname, hk_arg_t *args,
                               gpointer userdata);

guint hk_add_handler(hk_handler_t handler, const gchar *hookname,
                     gint priority, gpointer userdata);
void  hk_del_handler(const gchar *hookname, guint hid);
guint hk_run_handlers(const gchar *hookname, hk_arg_t *args);
#endif

void hk_message_in(const char *bjid, const char *resname,
                   time_t timestamp, const char *msg, LmMessageSubType type,
                   guint encrypted);
void hk_message_out(const char *bjid, const char *nickname,
                    time_t timestamp, const char *msg,
                    guint encrypted,  gpointer xep184);
void hk_statuschange(const char *bjid, const char *resname, gchar prio,
                     time_t timestamp, enum imstatus status,
                     char const *status_msg);
void hk_mystatuschange(time_t timestamp,
                       enum imstatus old_status, enum imstatus new_status,
                       const char *msg);

void hk_postconnect(void);
void hk_predisconnect(void);

void hk_unread_list_change(guint unread_count, guint attention_count,
                           guint muc_unread, guint muc_attention);

void hk_ext_cmd_init(const char *command);
void hk_ext_cmd(const char *bjid, guchar type, guchar info, const char *data);

#endif /* __MCABBER_HOOKS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
