/*
 * utils.c      -- Various utility functions
 * 
 * Copyright (C) 2005 Mikael Berthe <bmikael@lists.lilotux.net>
 * ut_* functions are derived from Cabber debug/log code.
 * from_iso8601() comes from the Gaim project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <config.h>

static int DebugEnabled;
static char *FName;

void ut_InitDebug(unsigned int level, const char *filename)
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

//  to_iso8601(dststr, timestamp)
// Convert timestamp to iso8601 format, and store it in dststr.
// NOTE: dststr should be at last 19 chars long.
// Return should be 0
int to_iso8601(char *dststr, time_t timestamp)
{
  struct tm *tm_time;
  int ret;

  tm_time = gmtime(&timestamp);

  ret = snprintf(dststr, 19, "%.4d%02d%02dT%02d:%02d:%02dZ",
        1900+tm_time->tm_year, tm_time->tm_mon+1, tm_time->tm_mday,
        tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);

  return ((ret == -1) ? -1 : 0);
}

//  from_iso8601(timestamp, utc)
// This function comes from the Gaim project, gaim_str_to_time().
// (Actually date may not be pure iso-8601)
// Thanks, guys!
time_t from_iso8601(const char *timestamp, int utc)
{
  struct tm t;
  time_t retval = 0;
  char buf[32];
  char *c;
  int tzoff = 0;

  time(&retval);
  localtime_r(&retval, &t);

  snprintf(buf, sizeof(buf), "%s", timestamp);
  c = buf;

  /* 4 digit year */
  if (!sscanf(c, "%04d", &t.tm_year)) return 0;
  c+=4;
  if (*c == '-')
    c++;

  t.tm_year -= 1900;

  /* 2 digit month */
  if (!sscanf(c, "%02d", &t.tm_mon)) return 0;
  c+=2;
  if (*c == '-')
    c++;

  t.tm_mon -= 1;

  /* 2 digit day */
  if (!sscanf(c, "%02d", &t.tm_mday)) return 0;
  c+=2;
  if (*c == 'T' || *c == '.') { /* we have more than a date, keep going */
    c++; /* skip the "T" */

    /* 2 digit hour */
    if (sscanf(c, "%02d:%02d:%02d", &t.tm_hour, &t.tm_min, &t.tm_sec) == 3 ||
        sscanf(c, "%02d%02d%02d", &t.tm_hour, &t.tm_min, &t.tm_sec) == 3) {
      int tzhrs, tzmins;
      c+=8;
      if (*c == '.') /* dealing with precision we don't care about */
        c += 4;

      if ((*c == '+' || *c == '-') &&
          sscanf(c+1, "%02d:%02d", &tzhrs, &tzmins)) {
        tzoff = tzhrs*60*60 + tzmins*60;
        if (*c == '+')
          tzoff *= -1;
      }

      if (tzoff || utc) {

//#ifdef HAVE_TM_GMTOFF
        tzoff += t.tm_gmtoff;
//#else
//#   ifdef HAVE_TIMEZONE
//        tzset();    /* making sure */
//        tzoff -= timezone;
//#   endif
//#endif
      }
    }
  }

  t.tm_isdst = -1;

  retval = mktime(&t);

  retval += tzoff;

  return retval;
}

