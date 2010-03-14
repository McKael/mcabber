/*
 * utils.c      -- Various utility functions
 *
 * Copyright (C) 2005-2010 Mikael Berthe <mikael@lilotux.net>
 * Some of the ut_* functions are derived from Cabber debug/log code.
 * from_iso8601() comes from the Pidgin (libpurple) project.
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef HAVE_LIBIDN
#include <idna.h>
#include <stringprep.h>
static char idnprep[1024];
#endif

#include <glib.h>
#include <glib/gprintf.h>

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

#include "utils.h"
#include "logprint.h"
#include "settings.h"

static int DebugEnabled;
static char *FName;

//  jidtodisp(jid)
// Strips the resource part from the jid
// The caller should g_free the result after use.
char *jidtodisp(const char *fjid)
{
  char *ptr;
  char *alias;

  alias = g_strdup(fjid);

  if ((ptr = strchr(alias, JID_RESOURCE_SEPARATOR)) != NULL) {
    *ptr = 0;
  }
  return alias;
}

char *jid_get_username(const char *fjid)
{
  char *ptr;
  char *username;

  username = g_strdup(fjid);
  if ((ptr = strchr(username, JID_DOMAIN_SEPARATOR)) != NULL) {
    *ptr = 0;
  }
  return username;
}

char *compose_jid(const char *username, const char *servername,
                  const char *resource)
{
  char *fjid;

  if (!strchr(username, JID_DOMAIN_SEPARATOR)) {
    fjid = g_strdup_printf("%s%c%s%c%s", username,
                           JID_DOMAIN_SEPARATOR, servername,
                           JID_RESOURCE_SEPARATOR, resource);
  } else {
    fjid = g_strdup_printf("%s%c%s", username,
                           JID_RESOURCE_SEPARATOR, resource);
  }
  return fjid;
}

gboolean jid_equal(const char *jid1, const char *jid2)
{
  char *a,*b;
  int ret;
  if (!jid1 && !jid2)
    return TRUE;
  if (!jid1 || !jid2)
    return FALSE;

  a = jidtodisp(jid1);
  b = jidtodisp(jid2);
  ret = strcasecmp(a, b);
  g_free(a);
  g_free(b);
  return (ret == 0) ? TRUE : FALSE;
}

//  expand_filename(filename)
// Expand "~/" with the $HOME env. variable in a file name.
// The caller must free the string after use.
char *expand_filename(const char *fname)
{
  if (!fname)
    return NULL;
  if (!strncmp(fname, "~/", 2)) {
    char *homedir = getenv("HOME");
    if (homedir)
      return g_strdup_printf("%s%s", homedir, fname+1);
  }
  return g_strdup(fname);
}

void fingerprint_to_hex(const unsigned char *fpr, char hex[49])
{
  int i;
  char *p;

  for (p = hex, i = 0; i < 15; i++, p+=3)
    g_sprintf(p, "%02X:", fpr[i]);
  g_sprintf(p, "%02X", fpr[i]);
  hex[48] = '\0';
}

gboolean hex_to_fingerprint(const char *hex, char fpr[16])
{
  int i;
  char *p;

  if (strlen(hex) != 47)
    return FALSE;
  for (i = 0, p = (char*)hex; *p && *(p+1); i++, p += 3)
    fpr[i] = (char) g_ascii_strtoull (p, NULL, 16);
  return TRUE;
}

static gboolean tracelog_create(void)
{
  FILE *fp;
  struct stat buf;
  int err;

  fp = fopen(FName, "a");
  if (!fp) {
    scr_LogPrint(LPRINT_NORMAL, "ERROR: Cannot open tracelog file: %s!",
                 strerror(errno));
    return FALSE;
  }

  err = fstat(fileno(fp), &buf);
  if (err || buf.st_uid != geteuid()) {
    fclose(fp);
    if (err)
      scr_LogPrint(LPRINT_NORMAL, "ERROR: cannot stat the tracelog file: %s!",
                   strerror(errno));
    else
      scr_LogPrint(LPRINT_NORMAL, "ERROR: tracelog file does not belong to you!");
    return FALSE;
  }
  fchmod(fileno(fp), S_IRUSR|S_IWUSR);

  fputs("New trace log started.\n----------------------\n", fp);
  fclose(fp);

  return TRUE;
}

static gchar *tracelog_level_guard(const gchar *key, const gchar *new_value)
{
  int new_level = 0;
  if (new_value)
    new_level = atoi(new_value);
  if (DebugEnabled < 1 && new_level > 0 && FName && !tracelog_create())
    DebugEnabled = 0;
  else
    DebugEnabled = new_level;
  return g_strdup(new_value);
}

static gchar *tracelog_file_guard(const gchar *key, const gchar *new_value)
{
  gchar *new_fname = NULL;

  if (new_value)
    new_fname = expand_filename(new_value);

  if (g_strcmp0(FName, new_fname)) {
    g_free(FName);
    FName = new_fname;
    if (DebugEnabled > 0 && !tracelog_create()) {
      g_free(FName);
      FName = NULL;
    }
  } else
    g_free(new_fname);

  return g_strdup(new_value);
}

//  ut_init_debug()
// Installs otpion guards before initial config file parsing.
void ut_init_debug(void)
{
  DebugEnabled = 0;
  FName        = NULL;
  settings_set_guard("tracelog_level", tracelog_level_guard);
  settings_set_guard("tracelog_file",  tracelog_file_guard);
}

void ut_write_log(unsigned int flag, const char *data)
{
  if (!DebugEnabled || !FName) return;

  if (((DebugEnabled >= 2) && (flag & (LPRINT_LOG|LPRINT_DEBUG))) ||
      ((DebugEnabled == 1) && (flag & LPRINT_LOG))) {
    FILE *fp = fopen(FName, "a+");
    if (!fp) {
      scr_LogPrint(LPRINT_NORMAL, "ERROR: Cannot open tracelog file: %s.",
                   strerror(errno));
      return;
    }
    if (fputs(data, fp) == EOF)
      scr_LogPrint(LPRINT_NORMAL, "ERROR: Cannot write to tracelog file.");
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
  // Permission checking isn't efficient on Cygwin
  return 0;
#endif

  fd = stat(name, &buf);
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
        (int)(1900+tm_time->tm_year), tm_time->tm_mon+1, tm_time->tm_mday,
        tm_time->tm_hour, tm_time->tm_min, tm_time->tm_sec);

  return ((ret == -1) ? -1 : 0);
}

//  from_iso8601(timestamp, utc)
// This function came from the Pidgin project, gaim_str_to_time().
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
  int tmpyear;

  time(&retval);
  localtime_r(&retval, &t);

  /* Reset time to midnight (00:00:00) */
  t.tm_hour = t.tm_min = t.tm_sec = 0;

  snprintf(buf, sizeof(buf), "%s", timestamp);
  c = buf;

  /* 4 digit year */
  if (!sscanf(c, "%04d", &tmpyear)) return 0;
  t.tm_year = tmpyear;
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

/**
 * Derived from libjabber/jid.c, because the libjabber version is not
 * really convenient for our usage.
 *
 * Check if the full JID is valid
 * Return 0 if it is valid, non zero otherwise
 */
int check_jid_syntax(const char *fjid)
{
  const char *str;
  const char *domain, *resource;
  int domlen;
#ifdef HAVE_LIBIDN
  char *idnpp;
  int r;
#endif

  if (!fjid) return 1;

  domain = strchr(fjid, JID_DOMAIN_SEPARATOR);

  /* the username is optional */
  if (!domain) {
    domain = fjid;
  } else {
    /* node identifiers may not be longer than 1023 bytes */
    if ((domain == fjid) || (domain-fjid > 1023))
      return 1;
    domain++;

#ifdef HAVE_LIBIDN
    idnpp = idnprep;
    str = fjid;
    while (*str != JID_DOMAIN_SEPARATOR)
      *idnpp++ = *str++;
    *idnpp = 0;

    r = stringprep(idnprep, 1023, 0, stringprep_xmpp_nodeprep);
    if (r != STRINGPREP_OK || !idnprep[0])
      return 1;
    /* the username looks okay */
#else
    /* check for low and invalid ascii characters in the username */
    for (str = fjid; *str != JID_DOMAIN_SEPARATOR; str++) {
      if (*str <= ' ' || *str == ':' || *str == JID_DOMAIN_SEPARATOR ||
              *str == '<' || *str == '>' || *str == '\'' ||
              *str == '"' || *str == '&') {
        return 1;
      }
    }
    /* the username is okay as far as we can tell without LIBIDN */
#endif
  }

  resource = strchr(domain, JID_RESOURCE_SEPARATOR);

  /* the resource is optional */
  if (resource) {
    domlen = resource - domain;
    resource++;
    /* resources may not be longer than 1023 bytes */
    if ((*resource == '\0') || strlen(resource) > 1023)
      return 1;
#ifdef HAVE_LIBIDN
    strncpy(idnprep, resource, sizeof(idnprep));
    r = stringprep(idnprep, 1023, 0, stringprep_xmpp_resourceprep);
    if (r != STRINGPREP_OK || !idnprep[0])
      return 1;
#endif
  } else {
    domlen = strlen(domain);
  }

  /* there must be a domain identifier */
  if (domlen == 0) return 1;

  /* and it must not be longer than 1023 bytes */
  if (domlen > 1023) return 1;

#ifdef HAVE_LIBIDN
  idnpp = idnprep;
  str = domain;
  while (*str != '\0' && *str != JID_RESOURCE_SEPARATOR)
    *idnpp++ = *str++;
  *idnpp = 0;

  r = stringprep_nameprep(idnprep, 1023);
  if (r != STRINGPREP_OK || !idnprep[0])
    return 1;

  if (idna_to_ascii_8z(idnprep, &idnpp, IDNA_USE_STD3_ASCII_RULES) !=
      IDNA_SUCCESS)
    return 1;
  else
    free(idnpp);
#else
  /* make sure the hostname is valid characters */
  for (str = domain; *str != '\0' && *str != JID_RESOURCE_SEPARATOR; str++) {
    if (!(isalnum(*str) || *str == '.' || *str == '-' || *str == '_'))
      return 1;
  }
#endif

  /* it's okay as far as we can tell */
  return 0;
}


inline void mc_strtolower(char *str)
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

  if (!s) return;

  for (p = s; *p; p++) {
    if (*p == '"') {
      if (!escape) {
        instring = !instring;
        strcpy(p, p+1);
        p--;
      } else
        escape = FALSE;
    } else if (*p == '\\') {
      if (!escape) {
        strcpy(p, p+1);
        p--;
      }
      escape = !escape;
    } else
      escape = FALSE;
  }
}

//  split_arg(arg, n, preservelast)
// Split the string arg into a maximum of n pieces, taking care of
// double quotes.
// Return a null-terminated array of strings.  This array should be freed
// by the caller after use, for example with free_arg_lst().
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
      p = start-1;
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
// Expand tabs and filter out some bad chars in string text.
// If there is no tab and no bad chars in the string, a pointer to text
// is returned (be careful _not_ to free the pointer in this case).
// If there are some tabs or bad chars, a new string with expanded chars
// and no bad chars is returned; this is up to the caller to free this
// string after use.
char *ut_expand_tabs(const char *text)
{
  char *xtext, *linestart;
  char *p, *q;
  guint n = 0, bc = 0;

  xtext = (char*)text;
  for (p=xtext; *p; p++)
    if (*p == '\t')
      n++;
    else if (*p == '\x0d')
      bc++;
  // XXX Are there other special chars we should filter out?

  if (!n && !bc)
    return (char*)text;

  xtext = g_new(char, strlen(text) + 1 + 8*n);
  p = (char*)text;
  q = linestart = xtext;
  do {
    if (*p == '\t') {
      do { *q++ = ' '; } while ((q-linestart)%8);
    } else if (*p != '\x0d') {
      *q++ = *p;
      if (*p =='\n')
        linestart = q;
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

//  startswith(str, word, ignore_case)
// Returns TRUE if string str starts with word.
int startswith(const char *str, const char *word, guint ignore_case)
{
  if (ignore_case && !strncasecmp(str, word, strlen(word)))
    return TRUE;
  else if (!ignore_case && !strncmp(str, word, strlen(word)))
    return TRUE;
  return FALSE;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
