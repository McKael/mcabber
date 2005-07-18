#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <glib.h>

#include "settings.h"
#include "commands.h"
#include "utils.h"
#include "screen.h"

//  cfg_file(filename)
// Read and parse config file "filename".  If filename is NULL,
// try to open the configuration file at the default locations.
//
// This function comes from Cabber, and has been slightly modified.
int cfg_file(char *filename)
{
  FILE *fp;
  char *buf;
  char *line;
  unsigned int ln = 0;
  int err = 0;

  if (!filename) {
    // Use default config file locations
    char *home = getenv("HOME");
    if (!home) {
      ut_WriteLog("Can't find home dir!\n");
      fprintf(stderr, "Can't find home dir!\n");
      return -1;
    }
    filename = g_new(char, strlen(home)+24);
    sprintf(filename, "%s/.mcabber/mcabberrc", home);
    if ((fp = fopen(filename, "r")) == NULL) {
      // 2nd try...
      sprintf(filename, "%s/.mcabberrc", home);
      if ((fp = fopen(filename, "r")) == NULL) {
        fprintf(stderr, "Cannot open config file!\n");
        return -1;
      }
    }
    g_free(filename);
  }
  else if ((fp = fopen(filename, "r")) == NULL) {
    perror("fopen (parsecfg.c:46)");
    return -1;
  }

  // This should be fully rewritten...
  buf = g_new(char, 512);

  while (fgets(buf+1, 511, fp) != NULL) {
    line = buf+1;
    ln++;

    while (isspace(*line))
      line++;

    while ((strlen(line) > 0) && isspace((int) line[strlen(line) - 1]))
      line[strlen(line) - 1] = '\0';

    if ((*line == '\n') || (*line == '\0') || (*line == '#'))
      continue;

    if ((strchr(line, '=') != NULL)) {
      while ((strlen(line) > 0) && isspace(line[strlen(line) - 1]))
	line[strlen(line) - 1] = '\0';

      if (strncmp(line, "set ", 4) &&
          strncmp(line, "bind ", 5) &&
          strncmp(line, "alias ", 6)) {
        scr_LogPrint("Error in configuration file (l. %d)", ln);
        err++;
        continue;
      }
      *(--line) = '/';
      process_command(line);
    } else {
      scr_LogPrint("Error in configuration file (l. %d)", ln);
      err++;
    }
  }
  g_free(buf);
  fclose(fp);
  return err;
}
