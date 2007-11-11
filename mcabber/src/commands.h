#ifndef __COMMANDS_H__
#define __COMMANDS_H__ 1

#include <glib.h>

// Command structure
typedef struct {
  char name[32];
  const char *help;
  guint completion_flags[2];
  void (*func)(char *);
} cmd;

void cmd_init(void);
cmd *cmd_get(const char *command);
int  process_line(const char *line);
int  process_command(const char *line, guint iscmd);
char *expandalias(const char *line);

extern char *mcabber_version(void);
extern void mcabber_connect(void);
extern void mcabber_set_terminate_ui(void);

void room_whois(gpointer bud, char *nick_locale, guint interactive);
void room_leave(gpointer bud, char *arg);
void setstatus(const char *recipient, const char *arg);

#endif /* __COMMANDS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
