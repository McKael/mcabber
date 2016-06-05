/*
 * nohtml.c     -- (X)HTML helper functions
 *
 * Copyright (C) 2008,2009 Mikael Berthe <mikael@lilotux.net>
 * Some portions come from the jabberd project, see below.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Some parts come from libjabber/str.c:
 * Copyright (c) 1999-2002 Jabber.com, Inc.  All Rights Reserved.  Contact
 * information for Jabber.com, Inc. is available at http://www.jabber.com/.
 * Portions Copyright (c) 1998-1999 Jeremie Miller.
 */

#include <string.h>
#include <glib.h>
#include <config.h>


/*  html_strip(htmlbuf)
 * Remove html entities from htmlbuf and try to convert it to plain text.
 * The caller must g_free the string after use.
 * Code mostly derived from strunescape(), in libjabber.
 */
char *html_strip(const char *htmlbuf)
{
  int i, j=0, html_len;
  char *nohtml;

  if (!htmlbuf) return(NULL);

  nohtml = g_strdup(htmlbuf);

  html_len = (int)strlen(htmlbuf);
  for (i = 0; i < html_len; i++) {
    if (htmlbuf[i] == '&') {
      if (!strncmp(&htmlbuf[i],"&amp;",5)) {
        nohtml[j] = '&';
        i += 4;
      } else if (!strncmp(&htmlbuf[i],"&quot;", 6)) {
        nohtml[j] = '\"';
        i += 5;
      } else if (!strncmp(&htmlbuf[i],"&apos;", 6)) {
        nohtml[j] = '\'';
        i += 5;
      } else if (!strncmp(&htmlbuf[i],"&lt;", 4)) {
        nohtml[j] = '<';
        i += 3;
      } else if (!strncmp(&htmlbuf[i],"&gt;", 4)) {
        nohtml[j] = '>';
        i += 3;
      } else {
        nohtml[j] = htmlbuf[i];
      }
    } else if (htmlbuf[i] == '<') {
      if (!strncmp(&htmlbuf[i],"<br>", 4)) {
        nohtml[j] = '\n';
        i += 3;
      } else if (!strncmp(&htmlbuf[i],"<br/>", 5)) {
        nohtml[j] = '\n';
        i += 4;
      } else if (!strncmp(&htmlbuf[i],"<FONT>", 6)) {
        /* Let's strip <FONT> from Adium */
        i += 5;
        j--;
      } else if (!strncmp(&htmlbuf[i],"</FONT>", 7)) {
        i += 6;
        j--;
      } else {
        nohtml[j] = htmlbuf[i];
      }
    } else
      nohtml[j] = htmlbuf[i];
    j++;
  }
  nohtml[j] = '\0';
  return nohtml;
}

/*  html_escape(text)
 * Add (x)html entities to the text.
 * The caller must g_free the string after use.
 * Code mostly derived from strescape(), in libjabber.
 */
char *html_escape(const char *text)
{
  int i, j;
  int oldlen, newlen;
  char *html;

  if (!text) return(NULL);

  oldlen = newlen = strlen(text);

  for (i = 0; i < oldlen; i++) {
    switch(text[i])
    {
      case '&':
          newlen += 5;
          break;
      case '\'':
          newlen += 6;
          break;
          case '\"':
              newlen += 6;
          break;
      case '<':
          newlen += 4;
          break;
      case '>':
          newlen += 4;
          break;
      case '\n':
          newlen += 5;
    }
  }

  if (oldlen == newlen)
    return g_strdup(text);

  html = g_new0(char, newlen+1);

  for (i = j = 0; i < oldlen; i++) {
    switch(text[i])
    {
      case '&':
          memcpy(&html[j], "&amp;", 5);
          j += 5;
          break;
      case '\'':
          memcpy(&html[j], "&apos;", 6);
          j += 6;
          break;
      case '\"':
          memcpy(&html[j], "&quot;", 6);
          j += 6;
          break;
      case '<':
          memcpy(&html[j], "&lt;", 4);
          j += 4;
          break;
      case '>':
          memcpy(&html[j], "&gt;", 4);
          j += 4;
          break;
      case '\n':
          memcpy(&html[j], "<br/>", 5);
          j += 5;
          break;
      default:
          html[j++] = text[i];
    }
  }
  return html;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
