#ifndef __MCABBER_COMMANDS_H__
#define __MCABBER_COMMANDS_H__ 1

#include <glib.h>

#include <mcabber/config.h>

// Command structure
typedef struct {
  char name[32];
  const char *help;
  guint completion_flags[2];
  void (*func)(char *);
  gpointer userdata;
} cmd;

void cmd_init(void);
cmd *cmd_get(const char *command);
void process_line(const char *line);
void process_command(const char *line, guint iscmd);
char *expandalias(const char *line);
#ifdef MODULES_ENABLE
gpointer cmd_del(gpointer id);
gpointer cmd_add(const char *name, const char *help, guint flags1, guint flags2,
                 void (*f)(char*), gpointer userdata);
gboolean cmd_set_safe(const gchar *name, gboolean safe);
#endif
gboolean cmd_is_safe(const gchar *name);

void cmd_room_whois(gpointer bud, const char *nick, guint interactive);
void cmd_room_leave(gpointer bud, char *arg);
void cmd_setstatus(const char *recipient, const char *arg);
void say_cmd(char *arg, int parse_flags);

#endif /* __MCABBER_COMMANDS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
