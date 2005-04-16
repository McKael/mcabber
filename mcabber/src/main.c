#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <getopt.h>

#include "jabglue.h"
#include "screen.h"
#include "parsecfg.h"
#include "lang.h"
#include "utils.h"
#include "harddefines.h"


void sig_handler(int signum)
{
  switch (signum) {
  case SIGALRM:
    jb_keepalive();
    break;

  case SIGTERM:
    // bud_TerminateBuddies();
    scr_TerminateCurses();
    jb_disconnect();
    printf("Killed by SIGTERM\nBye!\n");
    exit(EXIT_SUCCESS);
    break;

  }
  signal(SIGALRM, sig_handler);
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
  char *jid = malloc(strlen(username)+strlen(servername)+strlen(resource)+3);
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
  char configFile[4096];
  char *username, *password, *resource;
  char *servername;
  char *jid;
  char *portstring, *sslstring;
  int key;
  unsigned int port;
  unsigned int ping;
  int ssl;
  int ret = 0;

  credits();

  /* SET THIS >0 TO ENABLE LOG */
  ut_InitDebug(1);

  lng_InitLanguage();

  ut_WriteLog("Setting signals handlers...\n");
  signal(SIGTERM, sig_handler);
  signal(SIGALRM, sig_handler);


  sprintf(configFile, "%s/.mcabberrc", getenv("HOME"));

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
	strncpy(configFile, optarg, 1024);
	break;
      }
  }

  ut_WriteLog("Setting config file: %s\n", configFile);


  /* Parsing config file... */
  ut_WriteLog("Parsing config file...\n");
  cfg_file(configFile);

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
  scr_DrawMainWindow();

  ssl = 0;
  sslstring = cfg_read("ssl");
  if (sslstring && (atoi(sslstring) > 0))
    ssl = 1;
  portstring = cfg_read("port");
  port = (portstring != NULL) ? (unsigned int) atoi(portstring) : 0;

  /* Connect to server */
  ut_WriteLog("Connecting to server: %s:%d\n", servername, port);
  scr_LogPrint("Connecting to server: %s:%d", servername, port);

  jid = compose_jid(username, servername, resource);
  jc = jb_connect(jid, port, ssl, password);
  free(jid);
  if (!jc) {
    ut_WriteLog("\terror!!!\n");
    fprintf(stderr, "Error connecting to (%s)\n", servername);
    scr_TerminateCurses();
    return -2;
  }

  ping = 20;
  if (cfg_read("pinginterval"))
    ping = (unsigned int) atoi(cfg_read("pinginterval"));

  ut_WriteLog("Ping interval stablished: %d secs\n", ping);

  ut_WriteLog("Entering into main loop...\n\n");
  ut_WriteLog("Ready to send/receive messages...\n");

  keypad(scr_GetInputWindow(), TRUE);
  while (ret != 255) {
    alarm(ping);
    key = scr_Getch();
    if (key != ERR)
      ret = process_key(key);
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
