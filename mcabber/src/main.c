/*
 * main.c
 *
 * Copyright (C) 2005 Mikael Berthe <bmikael@lists.lilotux.net>
 * Parts of this file come from Cabber <cabber@ajmacias.com>
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>

#include "jabglue.h"
#include "screen.h"
#include "settings.h"
#include "roster.h"
#include "commands.h"
#include "histolog.h"
#include "hooks.h"
#include "utils.h"
#include "harddefines.h"


void mcabber_connect(void)
{
  const char *username, *password, *resource, *servername;
  char *jid;
  int ssl;
  unsigned int port;

  servername = settings_opt_get("server");
  username   = settings_opt_get("username");
  password   = settings_opt_get("password");
  resource   = settings_opt_get("resource");

  if (!servername) {
    scr_LogPrint(LPRINT_NORMAL, "Server name has not been specified!");
    return;
  }
  if (!username) {
    scr_LogPrint(LPRINT_NORMAL, "User name has not been specified!");
    return;
  }
  if (!password) {
    scr_LogPrint(LPRINT_NORMAL, "Password has not been specified!");
    return;
  }
  if (!resource)
    resource = "mcabber";

  ssl  = (settings_opt_get_int("ssl") > 0);
  port = (unsigned int) settings_opt_get_int("port");

  jb_set_priority(settings_opt_get_int("priority"));

  /* Connect to server */
  scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, "Connecting to server: %s",
               servername);
  if (port)
    scr_LogPrint(LPRINT_NORMAL|LPRINT_DEBUG, " using port %d", port);

  jid = compose_jid(username, servername, resource);
  jc = jb_connect(jid, servername, port, ssl, password);
  g_free(jid);

  if (!jc)
    scr_LogPrint(LPRINT_LOGNORM, "Error connecting to (%s)", servername);

  jb_reset_keepalive();
}

void mcabber_disconnect(const char *msg)
{
  jb_disconnect();
  scr_TerminateCurses();
  if (msg)
    fprintf(stderr, "%s\n", msg);
  printf("Bye!\n");
  exit(EXIT_SUCCESS);
}

void sig_handler(int signum)
{
  if (signum == SIGCHLD) {
    int status;
    pid_t pid;
    do {
      pid = waitpid (WAIT_ANY, &status, WNOHANG);
    } while (pid > 0);
    //if (pid < 0)
    //  scr_LogPrint(LPRINT_LOGNORM, "Error in waitpid: errno=%d", errno);
    signal(SIGCHLD, sig_handler);
  } else if (signum == SIGTERM) {
    mcabber_disconnect("Killed by SIGTERM");
  } else if (signum == SIGINT) {
    mcabber_disconnect("Killed by SIGINT");
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "Caught signal: %d", signum);
  }
}

static void ask_password(void)
{
  char *password, *p;
  size_t passsize = 128;
  struct termios orig, new;
  int nread;

  /* Turn echoing off and fail if we can't. */
  if (tcgetattr(fileno(stdin), &orig) != 0) return;
  new = orig;
  new.c_lflag &= ~ECHO;
  if (tcsetattr(fileno(stdin), TCSAFLUSH, &new) != 0) return;

  /* Read the password. */
  password = NULL;
  printf("Please enter password: ");
  nread = getline(&password, &passsize, stdin);

  /* Restore terminal. */
  tcsetattr(fileno(stdin), TCSAFLUSH, &orig);
  printf("\n");

  if (nread == -1 || !password) return;

  for (p = (char*)password; *p; p++)
    ;
  for ( ; p > (char*)password ; p--)
    if (*p == '\n' || *p == '\r') *p = 0;

  settings_set(SETTINGS_TYPE_OPTION, "password", password);
  free(password);
  return;
}

static void credits(void)
{
  printf(MCABBER_VERSION "\n");
  printf(EMAIL "\n");
}

int main(int argc, char **argv)
{
  char *configFile = NULL;
  const char *optstring;
  int optval, optval2;
  int key;
  unsigned int ping;
  int ret;
  unsigned int refresh = 0;

  credits();

  signal(SIGTERM, sig_handler);
  signal(SIGINT,  sig_handler);
  signal(SIGCHLD, sig_handler);

  /* Parse command line options */
  while (1) {
    int c = getopt(argc, argv, "hf:");
    if (c == -1) {
      break;
    } else
      switch (c) {
      case 'h':
	printf("Usage: %s [-f mcabberrc_file]\n\n", argv[0]);
        printf("Thanks to AjMacias for cabber!\n\n");
	return 0;
      case 'f':
	configFile = g_strdup(optarg);
	break;
      }
  }

  /* Initialize commands system */
  cmd_init();

  /* Parsing config file... */
  ret = cfg_read_file(configFile);
  /* free() configFile if it has been allocated during options parsing */
  if (configFile) g_free(configFile);
  /* Leave if there was an error in the config. file */
  if (ret)
    exit(EXIT_FAILURE);

  optstring = settings_opt_get("tracelog_file");
  if (optstring)
    ut_InitDebug(settings_opt_get_int("tracelog_level"), optstring);

  /* If no password is stored, we ask for it before entering
     ncurses mode */
  if (!settings_opt_get("password"))
    ask_password();

  /* Initialize N-Curses */
  scr_LogPrint(LPRINT_DEBUG, "Initializing N-Curses...");
  scr_InitCurses();

  scr_LogPrint(LPRINT_DEBUG, "Drawing main window...");
  scr_DrawMainWindow(TRUE);

  optval   = (settings_opt_get_int("logging") > 0);
  optval2  = (settings_opt_get_int("load_logs") > 0);
  if (optval || optval2)
    hlog_enable(optval, settings_opt_get("logging_dir"), optval2);

  optstring = settings_opt_get("events_command");
  if (optstring)
    hk_ext_cmd_init(optstring);

  ping = 40;
  if (settings_opt_get("pinginterval"))
    ping = (unsigned int) settings_opt_get_int("pinginterval");
  jb_set_keepalive_delay(ping);
  scr_LogPrint(LPRINT_DEBUG, "Ping interval established: %d secs", ping);

  if (settings_opt_get_int("hide_offline_buddies") > 0)
    buddylist_set_hide_offline_buddies(TRUE);

  /* Connection */
  if (settings_opt_get("password"))
    mcabber_connect();
  else
    scr_LogPrint(LPRINT_LOGNORM, "Can't connect: no password supplied");

  scr_LogPrint(LPRINT_DEBUG, "Entering into main loop...");

  for (ret = 0 ; ret != 255 ; ) {
    key = scr_Getch();

    /* The refresh is really an ugly hack, but we need to call doupdate()
       from time to time to catch the RESIZE events, because getch keep
       returning ERR until a real key is pressed :-(
       However, it allows us to handle an autoaway check here...
     */
    if (key != ERR) {
      ret = process_key(key);
      refresh = 0;
    } else if (refresh++ > 1) {
      doupdate();
      refresh = 0;
      scr_CheckAutoAway(FALSE);
    }

    if (key != KEY_RESIZE)
      jb_main();
    if (update_roster)
      scr_DrawRoster();
  }

  jb_disconnect();
  scr_TerminateCurses();

  printf("\n\nHave a nice day!\nBye!\n");

  return 0;
}
