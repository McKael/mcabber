/*
 * utf8.c       -- UTF-8 routines
 *
 * Copyright (C) 2006 Reimar Döffinger <Reimar.Doeffinger@stud.uni-karlsruhe.de>
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

#include "utf8.h"

char *prev_char(char *str, const char *limit)
{
  if (str <= limit)
    return str;
  str--;
  if (utf8_mode)
    while ((str > limit) && ((*str & 0xc0) == 0x80))
      str--;
  return str;
}

char *next_char(char *str)
{
  if (!*str)
    return str;
  str++;
  if (utf8_mode)
    while ((*str & 0xc0) == 0x80)
      str++;
  return str;
}

unsigned get_char(const char *str)
{
  unsigned char *strp = (unsigned char *)str;
  unsigned c = *strp++;
  unsigned mask = 0x80;
  int len = -1;
  if (!utf8_mode)
    return c;
  while (c & mask) {
    mask >>= 1;
    len++;
  }
  if (len <= 0 || len > 4)
    goto no_utf8;
  c &= mask - 1;
  while ((*strp & 0xc0) == 0x80) {
    if (len-- <= 0)
      goto no_utf8;
    c = (c << 6) | (*strp++ & 0x3f);
  }
  if (len)
    goto no_utf8;
  return c;

no_utf8:
  return *str;
}

char *put_char(char *str, unsigned c)
{
  int mask = 0xffffffc0;
  int i = 4;
  char code[5];
  if (!utf8_mode || c < 128) {
    *str++ = c;
    return str;
  }
  while (c & mask) {
    code[i--] = 0x80 | (c & 0x3f);
    c >>= 6;
    mask >>= 1;
    if (i < 0) {
      *str++ = '?';
      return str;
    }
  }
  code[i] = (mask << 1) | c;
  for (; i < 5; i++)
    *str++ = code[i];
  return str;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
