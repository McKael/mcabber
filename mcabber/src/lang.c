#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>


#include "utils.h"

char Lang[100];

void lng_InitLanguage(void)
{
  FILE *fp;
  memset(Lang, 0, 100);
  sprintf(Lang, "./lang/%s.txt", getenv("LANG"));
/*    strcpy(Lang, "./lang/");
    strcat(Lang, getenv("LANG"));
    strcat(Lang, ".txt");
*/
  if ((fp = fopen(Lang, "r")) == NULL) {
    /* reverting to default */
    ut_WriteLog("Reverting language to default: POSIX\n");
    strcpy(Lang, "./lang/POSIX.txt");
  } else {
    fclose(fp);
    ut_WriteLog("Setting language to %s\n", getenv("LANG"));
  }
}

char *i18n(char *text)
{
  /* hack */
  char *buf = (char *) malloc(1024);
  static char result[1024];
  FILE *fp;
  char *line;
  char *value;
  int found = 0;

  memset(result, 0, 1024);

  if ((fp = fopen(Lang, "r")) != NULL) {
    while ((fgets(buf, 1024, fp) != NULL) && (!found)) {
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

	if (!strcasecmp(line, text)) {
	  strcpy(result, value);
	  found = 1;
	}
	continue;
      }
      /* fprintf(stderr, "CFG: orphaned line \"%s\"\n", line); */
    }
    fclose(fp);
  }

  if (!found) {
    strcpy(result, text);
  }

  free(buf);
  return result;
}
