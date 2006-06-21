/*
 * utils.c      -- Various utility functions
 *
 * Copyright (C) 2005, 2006 Mikael Berthe <bmikael@lists.lilotux.net>
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

/* For Cygwin (thanks go to Yitzchak Scott-Thoennes) */
#ifdef __CYGWIN__
#  define timezonevar
   extern long timezone;
#endif
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <glib.h>

#include <config.h>
#include "logprint.h"

static int DebugEnabled;
static char *FName;

void ut_InitDebug(unsigned int level, const char *filename)
{
  FILE *fp;
  struct stat buf;
  int err;

  if (level < 1) {
    DebugEnabled = 0;
    FName = NULL;
    return;
  }

  if (filename)
    FName = g_strdup(filename);
  else {
    FName = getenv("HOME");
    if (!FName)
      FName = "/tmp/mcabberlog";
    else {
      char *tmpname = g_new(char, strlen(FName) + 12);
      strcpy(tmpname, FName);
      strcat(tmpname, "/mcabberlog");
      FName = tmpname;
    }
  }

  DebugEnabled = level;

  fp = fopen(FName, "a");
  if (!fp) {
    fprintf(stderr, "ERROR: Cannot open tracelog file\n");
    return;
  }

  err = fstat(fileno(fp), &buf);
  if (err || buf.st_uid != geteuid()) {
    fclose(fp);
    DebugEnabled = 0;
    FName = NULL;
    if (err) {
      fprintf(stderr, "ERROR: cannot stat the tracelog file!\n");
    } else {
      fprintf(stderr, "ERROR: tracelog file does not belong to you!\n");
    }
    return;
  }
  fchmod(fileno(fp), S_IRUSR|S_IWUSR);

  fprintf(fp, "New trace log started.\n"
	  "----------------------\n");
  fclose(fp);
}

void ut_WriteLog(unsigned int flag, const char *data)
{
  if (!DebugEnabled || !FName) return;

  if (((DebugEnabled == 2) && (flag & (LPRINT_LOG|LPRINT_DEBUG))) ||
      ((DebugEnabled == 1) && (flag & LPRINT_LOG))) {
    FILE *fp = fopen(FName, "a+");
    if (!fp) {
      scr_LogPrint(LPRINT_NORMAL, "ERROR: Cannot open tracelog file");
      return;
    }
    fputs(data, fp);
    fclose(fp);
  }
}

//  checkset_perm(name, setmode)
// Check the permissions of the "name" file/dir
// If setmode is true, correct the permissions if they are wrong
// Return values: -1 == bad file/dir, 0 == success, 1 == cannot correct
int checkset_perm(const char *name, unsigned int setmode)
{
  int fd;
  struct stat buf;

#ifdef __CYGWIN__
  // Permission checking isn't efficent on Cygwin
  return 0;
#endif

  fd = lstat(name, &buf);
  if (fd == -1) return -1;

  if (buf.st_uid != geteuid()) {
    scr_LogPrint(LPRINT_LOGNORM, "Wrong file owner [%s]", name);
    return 1;
  }

  if (buf.st_mode & (S_IRGRP | S_IWGRP | S_IXGRP) ||
      buf.st_mode & (S_IROTH | S_IWOTH | S_IXOTH)) {
    if (setmode) {
      mode_t newmode = 0;
      scr_LogPrint(LPRINT_LOGNORM, "Bad permissions [%s]", name);
      if (S_ISDIR(buf.st_mode))
        newmode |= S_IXUSR;
      newmode |= S_IRUSR | S_IWUSR;
      if (chmod(name, newmode)) {
        scr_LogPrint(LPRINT_LOGNORM, "WARNING: Failed to correct permissions!");
        return 1;
      }
      scr_LogPrint(LPRINT_LOGNORM, "Permissions have been corrected");
    } else {
      scr_LogPrint(LPRINT_LOGNORM, "WARNING: Bad permissions [%s]", name);
      return 1;
    }
  }

  return 0;
}

const char *ut_get_tmpdir(void)
{
  static const char *tmpdir;
  const char *tmpvars[] = { "MCABBERTMPDIR", "TMP", "TMPDIR", "TEMP" };
  unsigned int i;

  if (tmpdir)
    return tmpdir;

  for (i = 0; i < (sizeof(tmpvars) / sizeof(const char *)); i++) {
    tmpdir = getenv(tmpvars[i]);
    if (tmpdir && tmpdir[0] && tmpdir[0] == '/' && tmpdir[1]) {
      // Looks ok.
      return tmpdir;
    }
  }

  // Default temporary directory
  tmpdir = "/tmp";
  return tmpdir;
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
// This function came from the Gaim project, gaim_str_to_time().
// (Actually date may not be pure iso-8601)
// Thanks, guys!
// ** Modified by somian 10 Apr 2006 with advice from ysth.
time_t from_iso8601(const char *timestamp, int utc)
{
  struct tm t;
  time_t retval = 0;
  char buf[32];
  char *c;
  int tzoff = 0;
  int hms_succ = 0;

  time(&retval);
  localtime_r(&retval, &t);

  /* Reset time to midnight (00:00:00) */
  t.tm_hour = t.tm_min = t.tm_sec = 0;

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
    if (sscanf(c, "%02d:%02d:%02d", &t.tm_hour, &t.tm_min, &t.tm_sec) == 3)
    {
      hms_succ = 1;
      c += 8;
    }
    else if (sscanf(c, "%02d%02d%02d", &t.tm_hour, &t.tm_min, &t.tm_sec) == 3)
    {
       hms_succ = 1;
       c += 6;
    }

    if (hms_succ) {
      int tzhrs, tzmins;

      if (*c == '.') /* dealing with precision we don't care about */
        c += 4;

      if ((*c == '+' || *c == '-') &&
          sscanf(c+1, "%02d:%02d", &tzhrs, &tzmins)) {
        tzoff = tzhrs*60*60 + tzmins*60;
        if (*c == '+')
          tzoff *= -1;
      }

      if (tzoff || utc) {
#ifdef HAVE_TM_GMTOFF
        tzoff += t.tm_gmtoff;
#else
#  ifdef HAVE_TIMEZONE
        tzset();    /* making sure */
        tzoff -= timezone;
#  endif
#endif
      }
    }
  }

  t.tm_isdst = -1;

  retval = mktime(&t);

  retval += tzoff;

  return retval;
}

// Should only be used for delays < 1s
inline void safe_usleep(unsigned int usec)
{
  struct timespec req;
  req.tv_sec = 0;
  req.tv_nsec = (long)usec * 1000L;
  nanosleep(&req, NULL);
}

/**
 * Derived from libjabber/jid.c, because the libjabber version is not
 * really convenient for our usage.
 *
 * Check if the full JID is valid
 * Return 0 if it is valid, non zero otherwise
 */
int check_jid_syntax(char *jid)
{
  char *str;
  char *domain, *resource;
  int domlen;

  if (!jid) return 1;

  domain = strchr(jid, '@');

  /* the username is optional */
  if (!domain) {
    domain = jid;
  } else {
    /* node identifiers may not be longer than 1023 bytes */
    if ((domain == jid) || (domain-jid > 1023))
      return 1;
    domain++;

    /* check for low and invalid ascii characters in the username */
    for (str = jid; *str != '@'; str++) {
      if (*str <= 32 || *str == ':' || *str == '@' ||
              *str == '<' || *str == '>' || *str == '\'' ||
              *str == '"' || *str == '&') {
        return 1;
      }
    }
    /* the username is okay as far as we can tell without LIBIDN */
  }

  resource = strchr(domain, '/');

  /* the resource is optional */
  if (resource) {
    domlen = resource - domain;
    resource++;
    /* resources may not be longer than 1023 bytes */
    if ((*resource == '\0') || strlen(resource) > 1023)
      return 1;
  } else {
    domlen = strlen(domain);
  }

  /* there must be a domain identifier */
  if (domlen == 0) return 1;

  /* and it must not be longer than 1023 bytes */
  if (domlen > 1023) return 1;

  /* make sure the hostname is valid characters */
  for (str = domain; *str != '\0' && *str != '/'; str++) {
    if (!(isalnum(*str) || *str == '.' || *str == '-' || *str == '_'))
      return 1;
  }

  /* it's okay as far as we can tell without LIBIDN */
  return 0;
}


void mc_strtolower(char *str)
{
  if (!str) return;
  for ( ; *str; str++)
    *str = tolower(*str);
}

//  strip_arg_special_chars(string)
// Remove quotes and backslashes before an escaped quote
// Only quotes need a backslash
// Ex.: ["a b"] -> [a b]; [a\"b] -> [a"b]
void strip_arg_special_chars(char *s)
{
  int instring = FALSE;
  int escape = FALSE;
  char *p;

  for (p = s; *p; p++) {
    if (*p == '"') {
      if (!escape) {
        instring = !instring;
        strcpy(p, p+1);
        p--;
      } else {
        escape = FALSE;
      }
    } else if (*p == '\\') {
      if (!escape) {
        if (*(p+1) == '"') {
          strcpy(p, p+1);
          p--;
        }
        escape = TRUE;
      } else {
        escape = FALSE;
      }
    }
  }
}

//  split_arg(arg, n, preservelast)
// Split the string arg into a maximum of n pieces, taking care of
// double quotes.
// Return a null-terminated array of strings.  This array should be freed
// be the caller after use, for example with free_arg_lst().
// If dontstriplast is true, the Nth argument isn't stripped (i.e. no
// processing of quote chars)
char **split_arg(const char *arg, unsigned int n, int dontstriplast)
{
  char **arglst;
  const char *p, *start, *end;
  unsigned int i = 0;
  int instring = FALSE;
  int escape = FALSE;

  arglst = g_new0(char*, n+1);

  if (!arg || !n) return arglst;

  // Skip leading space
  for (start = arg; *start && *start == ' '; start++) ;
  // End of string pointer
  for (end = start; *end; end++) ;
  // Skip trailing space
  while (end > start+1 && *(end-1) == ' ')
    end--;

  for (p = start; p < end; p++) {
    if (*p == '"' && !escape)
      instring = !instring;
    if (*p == '\\' && !escape)
      escape = TRUE;
    else if (escape)
      escape = FALSE;
    if (*p == ' ' && !instring && i+1 < n) {
      // end of parameter
      *(arglst+i) = g_strndup(start, p-start);
      strip_arg_special_chars(*(arglst+i));
      for (start = p+1; *start && *start == ' '; start++) ;
      p = start;
      i++;
    }
  }

  if (start < end) {
    *(arglst+i) = g_strndup(start, end-start);
    if (!dontstriplast || i+1 < n)
      strip_arg_special_chars(*(arglst+i));
  }

  return arglst;
}

//  free_arg_lst(arglst)
// Free an array allocated by split_arg()
void free_arg_lst(char **arglst)
{
  char **arg_elt;

  for (arg_elt = arglst; *arg_elt; arg_elt++)
    g_free(*arg_elt);
  g_free(arglst);
}

//  replace_nl_with_dots(bufstr)
// Replace '\n' with "(...)" (or with a NUL if the string is too short)
void replace_nl_with_dots(char *bufstr)
{
  char *p = strchr(bufstr, '\n');
  if (p) {
    if (strlen(p) >= 5)
      strcpy(p, "(...)");
    else
      *p = 0;
  }
}

//  ut_expand_tabs(text)
// Expand tabs in string text.
// If there is no tab in the string, a pointer to text is returned (be
// careful _not_ to free the pointer in this case).
// If there are some tabs, a new string with expanded chars is returned; this
// is up to the caller to free this string after use.
char *ut_expand_tabs(const char *text)
{
  char *xtext;
  char *p, *q;
  guint8 n=0;

  xtext = (char*)text;
  for (p=xtext; *p; p++)
    if (*p == '\t') n++;

  if (!n)
    return (char*)text;

  xtext = g_new(char, strlen(text) + 1 + 8*n);
  p = (char*)text;
  q = xtext;
  do {
    if (*p == '\t') {
      do { *q++ = ' '; } while ((q-xtext)%8);
    } else {
      *q++ = *p;
    }
  } while (*p++);

  return xtext;
}


/* Cygwin's newlib does not have strcasestr() */
/* The author of the code before the endif is
 *    Jeffrey Stedfast <fejj@ximian.com>
 * and this code is reusable in compliance with the GPL v2. -- somian */

#if !defined(HAVE_STRCASESTR)

#  define lowercase(c)  (isupper ((int) (c)) ? tolower ((int) (c)) : (int) (c))
#  define bm_index(c, icase)      ((icase) ? lowercase (c) : (int) (c))
#  define bm_equal(c1, c2, icase) ((icase) ? lowercase (c1) == lowercase (c2) : (c1) == (c2))

/* FIXME: this is just a guess... should really do some performace tests to get an accurate measure */
#  define bm_optimal(hlen, nlen)  (((hlen) ? (hlen) > 20 : 1) && (nlen) > 10 ? 1 : 0)

static unsigned char *
__boyer_moore (const unsigned char *haystack, size_t haystacklen,
               const unsigned char *needle, size_t needlelen, int icase)
{
  register unsigned char *hc_ptr, *nc_ptr;
  unsigned char *he_ptr, *ne_ptr, *h_ptr;
  size_t skiptable[256], n;
  register int i;

#ifdef BOYER_MOORE_CHECKS
  /* we don't need to do these checks since memmem/strstr/etc do it already */
  /* if the haystack is shorter than the needle then we can't possibly match */
  if (haystacklen < needlelen)
    return NULL;

  /* instant match if the pattern buffer is 0-length */
  if (needlelen == 0)
    return (unsigned char *) haystack;
#endif /* BOYER_MOORE_CHECKS */

  /* set a pointer at the end of each string */
  ne_ptr = (unsigned char *) needle + needlelen - 1;
  he_ptr = (unsigned char *) haystack + haystacklen - 1;

  /* create our skip table */
  for (i = 0; i < 256; i++)
    skiptable[i] = needlelen;
  for (nc_ptr = (unsigned char *) needle; nc_ptr < ne_ptr; nc_ptr++)
    skiptable[bm_index (*nc_ptr, icase)] = (size_t) (ne_ptr - nc_ptr);

  h_ptr = (unsigned char *) haystack;
  while (haystacklen >= needlelen) {
    hc_ptr = h_ptr + needlelen - 1;   /* set the haystack compare pointer */
    nc_ptr = ne_ptr;                  /* set the needle compare pointer */

    /* work our way backwards till they don't match */
    for (i = 0; nc_ptr > (unsigned char *) needle; nc_ptr--, hc_ptr--, i++)
      if (!bm_equal (*nc_ptr, *hc_ptr, icase))
        break;

    if (!bm_equal (*nc_ptr, *hc_ptr, icase)) {
      n = skiptable[bm_index (*hc_ptr, icase)];
      if (n == needlelen && i)
        if (bm_equal (*ne_ptr, ((unsigned char *) needle)[0], icase))
          n--;
      h_ptr += n;
      haystacklen -= n;
    } else
      return (unsigned char *) h_ptr;
  }

  return NULL;
}

/*
 * strcasestr:
 * @haystack: string to search
 * @needle: substring to search for
 *
 * Finds the first occurence of the substring @needle within the
 * string @haystack ignoring case.
 *
 * Returns a pointer to the beginning of the substring match within
 * @haystack, or NULL if the substring is not found.
 **/
char *
strcasestr (const char *haystack, const char *needle)
{
  register unsigned char *h, *n, *hc, *nc;
  size_t needlelen;

  needlelen = strlen (needle);

  if (needlelen == 0) {
    return (char *) haystack;
  } else if (bm_optimal (0, needlelen)) {
    return (char *) __boyer_moore ((const unsigned char *) haystack,
                                   strlen (haystack),
                                   (const unsigned char *) needle,
                                   needlelen, 1);
  }

  h = (unsigned char *) haystack;
  n = (unsigned char *) needle;

  while (*(h + needlelen - 1)) {
    if (lowercase (*h) == lowercase (*n)) {
      for (hc = h + 1, nc = n + 1; *hc && *nc; hc++, nc++)
        if (lowercase (*hc) != lowercase (*nc))
          break;

      if (!*nc)
        return (char *) h;
    }
    h++;
  }
  return NULL;
}
#endif /* !HAVE_STRCASESTR */

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
