#ifndef __COMMANDS_H__
#define __COMMANDS_H__ 1

#include <glib.h>

// Command structure
typedef struct {
  char name[32];
  const char *help;
  guint completion_flags[2];
  void *(*func)();
} cmd;

void cmd_init(void);
cmd *cmd_get(char *command);
int  process_line(char *line);

#endif /* __COMMANDS_H__ */

