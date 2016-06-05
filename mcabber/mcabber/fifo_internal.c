/*
 * fifo_internal.c      -- Read commands from a named pipe
 *
 * Copyright (C) 2008,2009 Mikael Berthe <mikael@lilotux.net>
 * Copyright (C) 2009      Myhailo Danylenko <isbear@ukrpost.net>
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
 */

#include <stdlib.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "commands.h"
#include "logprint.h"
#include "utils.h"
#include "settings.h"
#include "main.h"

static char *fifo_name = NULL;
static GIOChannel *fifo_channel = NULL;

static const char *FIFO_ENV_NAME = "MCABBER_FIFO";

static gboolean attach_fifo(const char *name);

static guint fifo_callback(GIOChannel *channel,
                           GIOCondition condition,
                           gpointer data)
{
  if (condition & (G_IO_IN|G_IO_PRI)) {
    GIOStatus  chstat;
    gchar     *buf;
    gsize      endpos;

    chstat = g_io_channel_read_line(channel, &buf, NULL, &endpos, NULL);
    if (chstat == G_IO_STATUS_ERROR || chstat == G_IO_STATUS_EOF) {
      if (!attach_fifo(fifo_name))
        scr_LogPrint(LPRINT_LOGNORM,
                     "Reopening fifo failed! Fifo will not work from now!");
      return FALSE;
    }
    if (buf) {
      guint logflag;
      guint fifo_ignore = settings_opt_get_int("fifo_ignore");

      if (endpos)
        buf[endpos] = '\0';

      if (settings_opt_get_int("fifo_hide_commands"))
        logflag = LPRINT_LOG;
      else
        logflag = LPRINT_LOGNORM;
      scr_LogPrint(logflag, "%s FIFO command: %s",
                   (fifo_ignore ? "Ignoring" : "Executing"), buf);
      if (!fifo_ignore) {
        process_command(buf, TRUE);
      }

      g_free(buf);
    }
  } else if (condition & (G_IO_ERR|G_IO_NVAL|G_IO_HUP)) {
    if (!attach_fifo(fifo_name))
      scr_LogPrint(LPRINT_LOGNORM,
                   "Reopening fifo failed! Fifo will not work from now!");
    return FALSE;
  }
  return TRUE;
}

static void fifo_destroy_callback(gpointer data)
{
  GIOChannel *channel = (GIOChannel *)data;
  g_io_channel_unref(channel);
  channel = NULL;
}

static gboolean check_fifo(const char *name)
{
  struct stat finfo;
  if (stat(name, &finfo) == -1) {
    /* some unknown error */
    if (errno != ENOENT)
      return FALSE;
    /* fifo not yet exists */
    if (mkfifo(name, S_IRUSR|S_IWUSR) != -1)
      return check_fifo(name);
    else
      return FALSE;
  }

  /* file exists */
  if (S_ISFIFO(finfo.st_mode))
    return TRUE;
  else
    return FALSE;
}

static gboolean attach_fifo(const char *name)
{
  GSource *source;
  int fd = open (name, O_RDONLY|O_NONBLOCK);
  if (fd == -1)
    return FALSE;

  if (fifo_channel)
    g_io_channel_unref(fifo_channel);

  fifo_channel = g_io_channel_unix_new(fd);

  g_io_channel_set_flags(fifo_channel, G_IO_FLAG_NONBLOCK, NULL);
  g_io_channel_set_encoding(fifo_channel, NULL, NULL);
  g_io_channel_set_close_on_unref(fifo_channel, TRUE);

  source = g_io_create_watch(fifo_channel,
                             G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL);
  g_source_set_callback(source, (GSourceFunc)fifo_callback,
                        (gpointer)fifo_channel,
                        (GDestroyNotify)fifo_destroy_callback);
  g_source_attach(source, main_context);

  return TRUE;
}

void fifo_deinit(void)
{
  unsetenv(FIFO_ENV_NAME);

  if (fifo_channel)
    g_source_remove_by_user_data(fifo_channel);
  /* channel itself should be destroyed by destruction callback */
  /* destroy open fifo */
  if (fifo_name) {
    /* well, that may create fifo, and then unlink,
     * but at least we will not destroy non-fifo data */
    if (check_fifo(fifo_name))
      unlink(fifo_name);
    g_free(fifo_name);
    fifo_name = NULL;
  }
}

//  fifo_init_internal(path)
// If path is NULL, reopen existing fifo, else open anew.
static int fifo_init_internal(const char *fifo_path)
{
  if (fifo_path) {
    fifo_deinit();
    fifo_name = expand_filename(fifo_path);

    if (!check_fifo(fifo_name)) {
      scr_LogPrint(LPRINT_LOGNORM, "WARNING: Cannot create the FIFO. "
                   "%s already exists and is not a pipe", fifo_name);
      g_free(fifo_name);
      fifo_name = NULL;
      return -1;
    }
  } else if (fifo_name)
    g_source_remove_by_user_data(fifo_channel);
  else
    return -1;

  if (!attach_fifo(fifo_name)) {
    scr_LogPrint(LPRINT_LOGNORM, "Error: Cannot open fifo");
    return -1;
  }

  setenv(FIFO_ENV_NAME, fifo_name, 1);

  scr_LogPrint(LPRINT_LOGNORM, "FIFO initialized (%s)", fifo_path);
  return 1;
}

static gchar *fifo_guard(const gchar *key, const gchar *new_value)
{
  if (new_value)
    fifo_init_internal(new_value);
  else
    fifo_deinit();
  return g_strdup(new_value);
}

// Returns 1 in case of success, -1 on error
int fifo_init(void)
{
  const char *path = settings_opt_get("fifo_name");
  static gboolean guard_installed = FALSE;
  if (!guard_installed)
    if (!(guard_installed = settings_set_guard("fifo_name", fifo_guard)))
      scr_LogPrint(LPRINT_DEBUG, "fifo: BUG: Cannot install option guard!");
  if (path)
    return fifo_init_internal(path);
  return 1;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0 sw=2 ts=2:  For Vim users... */
