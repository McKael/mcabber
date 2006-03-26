#ifndef __COMMANDS_H__
#define __COMMANDS_H__ 1

#include <glib.h>

// Command structure
typedef struct {
  char name[32];
  const char *help;
  guint completion_flags[2];
  void (*func)();
} cmd;

void cmd_init(void);
cmd *cmd_get(const char *command);
int  process_line(char *line);
int  process_command(char *line);
char *expandalias(char *line);

extern char *mcabber_version(void);
extern void mcabber_connect(void);

#endif /* __COMMANDS_H__ */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
