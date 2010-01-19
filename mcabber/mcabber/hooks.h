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

#define HOOK_MESSAGE_IN       ( 0x00000001 )
#define HOOK_MESSAGE_OUT      ( 0x00000002 )
#define HOOK_STATUS_CHANGE    ( 0x00000004 )
#define HOOK_MY_STATUS_CHANGE ( 0x00000008 )
#define HOOK_POST_CONNECT     ( 0x00000010 )
#define HOOK_PRE_DISCONNECT   ( 0x00000020 )
#define HOOK_INTERNAL         ( HOOK_POST_CONNECT | HOOK_PRE_DISCONNECT )

typedef struct {
  const char *name;
  const char *value;
} hk_arg_t;

typedef void (*hk_handler_t) (guint32 flags, hk_arg_t *args, gpointer userdata);

void hk_add_handler (hk_handler_t handler, guint32 flags, gpointer userdata);
void hk_del_handler (hk_handler_t handler, gpointer userdata);
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
                              enum imstatus old_status,
                              enum imstatus new_status, const char *msg);

void hk_postconnect(void);
void hk_predisconnect(void);

void hk_ext_cmd_init(const char *command);
void hk_ext_cmd(const char *bjid, guchar type, guchar info, const char *data);

#endif /* __MCABBER_HOOKS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
