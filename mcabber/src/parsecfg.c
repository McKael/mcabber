#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <glib.h>

#include "settings.h"
#include "utils.h"

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
  char *value;

  if (!filename) {
    // Use default config file locations
    char *home = getenv("HOME");
    if (!home) {
      ut_WriteLog("Can't find home dir!\n");
      exit(EXIT_FAILURE);
    }
    filename = g_new(char, strlen(home)+24);
    sprintf(filename, "%s/.mcabber/mcabberrc", home);
    if ((fp = fopen(filename, "r")) == NULL) {
      // 2nd try...
      sprintf(filename, "%s/.mcabberrc", home);
      if ((fp = fopen(filename, "r")) == NULL) {
        fprintf(stderr, "Cannot open config file!\n");
        exit(EXIT_FAILURE);
      }
    }
    g_free(filename);
  }
  else if ((fp = fopen(filename, "r")) == NULL) {
    perror("fopen (parsecfg.c:46)");
    exit(EXIT_FAILURE);
  }

  buf = g_new(char, 256);

  while (fgets(buf, 256, fp) != NULL) {
    line = buf;

    while (isspace((int) *line))
      line++;

    while ((strlen(line) > 0)
	   && isspace((int) line[strlen(line) - 1]))
      line[strlen(line) - 1] = '\0';

    if ((*line == '\n') || (*line == '\0') || (*line == '#'))
      continue;

    if ((strchr(line, '=') != NULL)) {
      value = strchr(line, '=');
      *value = '\0';
      value++;

      while (isspace((int) *value))
	value++;

      while ((strlen(line) > 0)
	     && isspace((int) line[strlen(line) - 1]))
	line[strlen(line) - 1] = '\0';

      settings_set(SETTINGS_TYPE_OPTION, line, value);
      continue;
    }
    fprintf(stderr, "CFG: orphaned line \"%s\"\n", line);
  }
  g_free(buf);
  return 1;
}
