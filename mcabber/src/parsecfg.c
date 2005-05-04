#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <glib.h>

#include "list.h"

#define MAX_LENGHT_INPUT 1024
#define cfg_entry(n) list_entry(n, cfg_entry_t, list)

typedef struct _cfg_entry_t {
  char *key;
  char *value;
  struct list_head list;
} cfg_entry_t;

static LIST_HEAD(cfg_list);


void push_in_list(char *key, char *value)
{
  cfg_entry_t *new_entry = g_new0(1, sizeof(cfg_entry_t));

  new_entry->key = (char *) g_new0(1, strlen(key) + 1);
  new_entry->value = (char *) g_new0(1, strlen(value) + 1);

  strcpy(new_entry->key, key);
  strcpy(new_entry->value, value);

  list_add(&new_entry->list, &cfg_list);
}

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

  buf = g_new(255);

  while (fgets(buf, 255, fp) != NULL) {
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

      push_in_list(line, value);
      continue;
    }
    fprintf(stderr, "CFG: orphaned line \"%s\"\n", line);
  }
  g_free(buf);
  return 1;
}

char *cfg_read(char *key)
{
  struct list_head *n, *pos;
  cfg_entry_t *search_entry = NULL;

  list_for_each_safe(pos, n, &cfg_list) {
    search_entry = cfg_entry(pos);
    if (search_entry->key) {
      if (!strcasecmp(search_entry->key, key)) {
	return search_entry->value;
      }
    }
  }
  return NULL;
}
