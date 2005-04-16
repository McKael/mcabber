#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

/* Variables globales a UTILS.C */
static int DebugEnabled;

void ut_InitDebug(int level)
{
  FILE *fp = fopen("/tmp/mcabberlog", "w");

  DebugEnabled = level;

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

  if (DebugEnabled) {
    fp = fopen("/tmp/mcabberlog", "a+");
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

