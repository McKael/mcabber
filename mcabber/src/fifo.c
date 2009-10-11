/*
 * fifo.c       -- Read commands from a named pipe
 *
 * Copyright (C) 2008,2009 Mikael Berthe <mikael@lilotux.net>
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
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include "commands.h"
#include "logprint.h"
#include "utils.h"
#include "settings.h"

#include "hbuf.h"   // For HBB_BLOCKSIZE

static FILE *sfd;
static char *fifo_name;

static const char *FIFO_ENV_NAME = "MCABBER_FIFO";

//  fifo_init(fifo_path)
// Create and open the FIFO file.
// If fifo_path is NULL, reopen the current pipe.
// Return 0 (success) or -1 (failure).
int fifo_init(const char *fifo_path)
{
  struct stat buf;
  int fd;
  char *fifo_path_xp;

  if (!sfd && !fifo_path)
    return -1;  // Nothing to do...

  if (sfd && !fifo_path) {  // We want to reinitialize the pipe
    fclose(sfd);
    sfd = NULL;
    if (fifo_name)
      goto fifo_init_open;
  }
  sfd = NULL;

  fifo_path_xp = expand_filename(fifo_path);

  if (!stat(fifo_path_xp, &buf)) {
    if (!S_ISFIFO(buf.st_mode)) {
      scr_LogPrint(LPRINT_LOGNORM, "WARNING: Cannot create the FIFO. "
                   "%s already exists and is not a pipe", fifo_path_xp);
      g_free(fifo_path_xp);
      return -1;
    }

    if (unlink(fifo_path_xp)) {
      scr_LogPrint(LPRINT_LOGNORM, "WARNING: Unable to unlink FIFO %s [%s]",
                   fifo_path_xp, g_strerror(errno));
      g_free(fifo_path_xp);
      return -1;
    }
  }

  if (mkfifo(fifo_path_xp, S_IWUSR | S_IRUSR)) {
    scr_LogPrint(LPRINT_LOGNORM, "WARNING: Cannot create the FIFO [%s]",
                 g_strerror(errno));
    g_free(fifo_path_xp);
    return -1;
  }

  fifo_name = fifo_path_xp;

fifo_init_open:
  fd = open(fifo_name, O_RDONLY | O_NONBLOCK);
  if (!fd)
    return -1;

  setenv(FIFO_ENV_NAME, fifo_name, 1);

  sfd = fdopen(fd, "r");
  if (fifo_path)
    scr_LogPrint(LPRINT_LOGNORM, "FIFO initialized (%s)", fifo_name);
  return 0;
}

//  fifo_deinit()
// Close the current FIFO pipe and delete it.
void fifo_deinit(void)
{
  unsetenv(FIFO_ENV_NAME);
  if (sfd) {
    fclose(sfd);
    sfd = NULL;
  }
  if (fifo_name) {
    unlink(fifo_name);
    g_free(fifo_name);
    fifo_name = NULL;
  }
}

//  fifo_read()
// Read a line from the FIFO pipe (if available), and execute it.
void fifo_read(void)
{
  struct timeval tv;
  fd_set fds;
  char *getbuf;
  char buf[HBB_BLOCKSIZE+1];
  int fd;

  if (!sfd) {
    return;
  }

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  fd = fileno(sfd);

  FD_ZERO(&fds);
  FD_SET(fd, &fds);

  select(fd + 1, &fds, NULL, NULL, &tv);

  if (!FD_ISSET(fd, &fds)) {
    return;
  }

  getbuf = fgets(buf, HBB_BLOCKSIZE, sfd);
  if (getbuf) {
    guint logflag;
    char *eol = buf;
    guint fifo_ignore = settings_opt_get_int("fifo_ignore");

    // Strip trailing newlines
    for ( ; *eol ; eol++)
      ;
    if (eol > buf)
      eol--;
    while (eol > buf && *eol == '\n')
      *eol-- = 0;

    if (settings_opt_get_int("fifo_hide_commands"))
      logflag = LPRINT_LOG;
    else
      logflag = LPRINT_LOGNORM;
    scr_LogPrint(logflag, "%s FIFO command: %s",
                 (fifo_ignore ? "Ignoring" : "Executing"), buf);
    if (!fifo_ignore) {
      if (process_command(buf, TRUE) == 255)
        mcabber_set_terminate_ui();
    }
  } else {
    if (feof(sfd))
      fifo_init(NULL);  // Reopen the FIFO on EOF
  }
}

//  fifo_get_fd()
// Return the FIFO file descriptor (-1 if none).
int fifo_get_fd(void)
{
  if (sfd)
    return fileno(sfd);
  return -1;
}

/* vim: set expandtab cindent cinoptions=>2\:2(0:  For Vim users... */
