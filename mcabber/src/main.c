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
#include "parsecfg.h"
#include "roster.h"
#include "commands.h"
#include "histolog.h"
#include "hooks.h"
#include "utils.h"
#include "harddefines.h"


void sig_handler(int signum)
{
  if (signum == SIGCHLD) {
    int status;
    pid_t pid;
    do {
      pid = waitpid (WAIT_ANY, &status, WNOHANG);
    } while (pid > 0);
    //if (pid < 0)
    //  ut_WriteLog("Error in waitpid: errno=%d\n", errno);
    signal(SIGCHLD, sig_handler);
  } else if (signum == SIGTERM) {
    // bud_TerminateBuddies();
    scr_TerminateCurses();
    jb_disconnect();
    printf("Killed by SIGTERM\nBye!\n");
    exit(EXIT_SUCCESS);
  } else {
    ut_WriteLog("Caught signal: %d\n", signum);
  }
}

ssize_t my_getpass (char **passstr, size_t *n)
{
  struct termios orig, new;
  int nread;

  /* Turn echoing off and fail if we can't. */
  if (tcgetattr(fileno(stdin), &orig) != 0)
      return -1;
  new = orig;
  new.c_lflag &= ~ECHO;
  if (tcsetattr(fileno(stdin), TCSAFLUSH, &new) != 0)
      return -1;

  /* Read the password. */
  nread = getline(passstr, n, stdin);

  /* Restore terminal. */
  (void) tcsetattr(fileno(stdin), TCSAFLUSH, &orig);

  return (ssize_t)nread;
}

char *compose_jid(const char *username, const char *servername,
        const char *resource)
{
  char *jid = g_new(char, 
          strlen(username)+strlen(servername)+strlen(resource)+3);
  strcpy(jid, username);
  strcat(jid, "@");
  strcat(jid, servername);
  strcat(jid, "/");
  strcat(jid, resource);
  return jid;
}

void credits(void)
{
  printf(MCABBER_VERSION "\n");
  printf(EMAIL "\n");
}

int main(int argc, char **argv)
{
  char *configFile = NULL;
  char *username, *password, *resource;
  char *servername, *portstring;
  char *jid;
  char *optstring, *optstring2;
  int optval, optval2;
  int key;
  unsigned int port;
  unsigned int ping;
  int ssl;
  int ret = 0;
  unsigned int refresh = 0;

  credits();

  /* SET THIS >0 TO ENABLE LOG */
  ut_InitDebug(0, NULL);

  ut_WriteLog("Setting signals handlers...\n");
  signal(SIGTERM, sig_handler);
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

  if (configFile)
    ut_WriteLog("Setting config file: %s\n", configFile);

  /* Parsing config file... */
  ut_WriteLog("Parsing config file...\n");
  cfg_file(configFile);
  if (configFile) g_free(configFile);

  optstring = cfg_read("debug");
  if (optstring)
    ut_InitDebug(1, optstring);

  servername = cfg_read("server");
  username = cfg_read("username");
  password = cfg_read("password");
  resource = cfg_read("resource");

  if (!servername) {
      printf("Server name has not been specified in the config file!\n");
      return -1;
  }
  if (!username) {
      printf("User name has not been specified in the config file!\n");
      return -1;
  }
  if (!password) {
      char *p;
      size_t passsize = 64;
      printf("Please enter password: ");
      my_getpass(&password, &passsize);
      printf("\n");
      for (p = password; *p; p++);
      for ( ; p > password ; p--)
          if (*p == '\n' || *p == '\r') *p = 0;
  }

  /* Initialize N-Curses */
  ut_WriteLog("Initializing N-Curses...\n");
  scr_InitCurses();

  ut_WriteLog("Drawing main window...\n");
  scr_DrawMainWindow(TRUE);

  optstring  = cfg_read("logging");
  optstring2 = cfg_read("load_logs");
  optval     = (optstring && (atoi(optstring) > 0));
  optval2    = (optstring2 && (atoi(optstring2) > 0));
  if (optval || optval2)
    hlog_enable(optval, cfg_read("logging_dir"), optval2);

  if ((optstring = cfg_read("events_command")) != NULL)
    hk_ext_cmd_init(optstring);

  ssl = 0;
  optstring = cfg_read("ssl");
  if (optstring && (atoi(optstring) > 0))
    ssl = 1;
  portstring = cfg_read("port");
  port = (portstring != NULL) ? (unsigned int) atoi(portstring) : 0;

  /* Connect to server */
  ut_WriteLog("Connecting to server: %s:%d\n", servername, port);
  scr_LogPrint("Connecting to server: %s:%d", servername, port);

  jid = compose_jid(username, servername, resource);
  jc = jb_connect(jid, port, ssl, password);
  g_free(jid);
  if (!jc) {
    ut_WriteLog("\terror!!!\n");
    fprintf(stderr, "Error connecting to (%s)\n", servername);
    scr_TerminateCurses();
    return -2;
  }

  ping = 40;
  if (cfg_read("pinginterval"))
    ping = (unsigned int) atoi(cfg_read("pinginterval"));
  jb_set_keepalive_delay(ping);
  ut_WriteLog("Ping interval stablished: %d secs\n", ping);

  optstring = cfg_read("hide_offline_buddies");
  if (optstring && (atoi(optstring) > 0))
    buddylist_set_hide_offline_buddies(TRUE);

  /* Initialize commands system */
  cmd_init();

  ut_WriteLog("Entering into main loop...\n\n");
  ut_WriteLog("Ready to send/receive messages...\n");

  jb_reset_keepalive();
  while (ret != 255) {
    key = scr_Getch();

    // The refresh is really an ugly hack, but we need to call doupdate()
    // from time to time to catch the RESIZE events, because getch keep
    // returning ERR until a real key is pressed :-(
    if (key != ERR) {
      ret = process_key(key);
      refresh = 0;
    } else if (refresh++ > 1) {
      doupdate();
      refresh = 0;
    }

    if (key != KEY_RESIZE)
      jb_main();
    if (update_roster)
      scr_DrawRoster();
  }

  jb_disconnect();
  //bud_TerminateBuddies();
  scr_TerminateCurses();

  printf("\n\nHave a nice day!\nBye!\n");

  return 0;
}
