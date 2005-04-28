#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static int DebugEnabled;
static char *FName;

void ut_InitDebug(unsigned int level, char *filename)
{
  FILE *fp;

  if (!level) {
    DebugEnabled = 0;
    FName = NULL;
    return;
  }

  if (filename)
    FName = strdup(filename);
  else {
    FName = getenv("HOME");
    if (!FName)
      FName = "/tmp/mcabberlog";
    else {
      char *tmpname = malloc(strlen(FName) + 12);
      strcpy(tmpname, FName);
      strcat(tmpname, "/mcabberlog");
      FName = tmpname;
    }
  }

  DebugEnabled = level;

  fp = fopen(FName, "w");
  if (!fp) return;
  fprintf(fp, "Debugging mode started...\n"
	  "-----------------------------------\n");
  fclose(fp);
}

void ut_WriteLog(const char *fmt, ...)
{
  FILE *fp = NULL;
  time_t ahora;
  va_list ap;
  char *buffer = NULL;

  if (DebugEnabled && FName) {
    fp = fopen(FName, "a+");
    if (!fp) return;
    buffer = (char *) calloc(1, 64);

    ahora = time(NULL);
    strftime(buffer, 64, "[%H:%M:%S] ", localtime(&ahora));
    fprintf(fp, "%s", buffer);

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    free(buffer);
    fclose(fp);
  }
}

