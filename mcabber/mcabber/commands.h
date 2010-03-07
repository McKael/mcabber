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
#ifdef MODULES_ENABLE
  gpointer userdata;
#endif
} cmd;

void cmd_init(void);
cmd *cmd_get(const char *command);
int  process_line(const char *line);
int  process_command(const char *line, guint iscmd);
char *expandalias(const char *line);
#ifdef MODULES_ENABLE
void cmd_deinit(void);
gpointer cmd_del(const char *name);
void cmd_add(const char *name, const char *help, guint flags1, guint flags2, void (*f)(char*), gpointer userdata);
#endif

void cmd_room_whois(gpointer bud, char *nick_locale, guint interactive);
void cmd_room_leave(gpointer bud, char *arg);
void cmd_setstatus(const char *recipient, const char *arg);
void say_cmd(char *arg, int parse_flags);

#endif /* __MCABBER_COMMANDS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
