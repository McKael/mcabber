#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <getopt.h>

#include "utils.h"
#include "screen.h"
#include "buddies.h"
#include "parsecfg.h"
#include "lang.h"
#include "server.h"
#include "harddefines.h"
#include "socket.h"

int sock;

void sig_handler(int signum)
{
  switch (signum) {
  case SIGALRM:
    sk_send(sock, " ");
    break;

  case SIGTERM:
    bud_TerminateBuddies();
    scr_TerminateCurses();
    srv_setpresence(sock, "unavailable");
    close(sock);
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

void credits(void)
{
  printf(VERSION "\n");
  printf(EMAIL "\n");
}

int main(int argc, char **argv)
{
  char configFile[4096];
  char *username, *password, *resource;
  char *servername;
  char *idsession;
  char *portstring;
  int key;
  unsigned int port;
  unsigned int ping;
  int ret = 0;


  credits();

  /* SET THIS >0 TO ENABLE LOG */
  ut_InitDebug(1);

  lng_InitLanguage();

  ut_WriteLog("Setting signals handlers...\n");
  signal(SIGTERM, sig_handler);
  signal(SIGALRM, sig_handler);


  sprintf(configFile, "%s/.mcabberrc", getenv("HOME"));

  /* Proceso opciones de usuario */
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

  /* Connect to server */
  portstring = cfg_read("port");
  port = (portstring != NULL) ? (unsigned int) atoi(portstring) : -1U;

  ut_WriteLog("Connecting to server: %s:%d\n", servername, port);
  if ((sock = srv_connect(servername, port)) < 0) {
    ut_WriteLog("\terror!!!\n");
    fprintf(stderr, "Error connecting to (%s)\n", servername);
    scr_TerminateCurses();
    return -2;
  }

  ut_WriteLog("Sending login string...\n");
  if ((idsession = srv_login(sock, servername, username, password, 
                             resource)) == NULL) {

    ut_WriteLog("\terror!!!\n");
    fprintf(stderr, "Error sending login string...\n");
    scr_TerminateCurses();
    return -3;
  }
  ut_WriteLog("Connected to %.48s: %s\n", servername, idsession);
  free(idsession);

  ut_WriteLog("Requesting roster...\n");
  bud_InitBuddies(sock);

  ut_WriteLog("Sending presence...\n");
  srv_setpresence(sock, "Online!");


  ut_WriteLog("Drawing main window...\n");
  scr_DrawMainWindow();

  ping = 15;
  if (cfg_read("pinginterval"))
    ping = (unsigned int) atoi(cfg_read("pinginterval"));

  ut_WriteLog("Ping interval stablished: %d secs\n", ping);

  ut_WriteLog("Entering into main loop...\n\n");
  ut_WriteLog("Ready to send/receive messages...\n");

  while (ret != 255) {
    int x;
    alarm(ping);
    x = check_io(sock, 0);
    if ((x & 1) == 1) {
      srv_msg *incoming = readserver(sock);

      switch (incoming->m) {
      case SM_PRESENCE:
	bud_SetBuddyStatus(incoming->from, incoming->connected);
	break;

      case SM_MESSAGE:
	scr_WriteIncomingMessage(incoming->from, incoming->body);
	free(incoming->body);
	free(incoming->from);
	break;

      case SM_UNHANDLED:
	break;
      }
      free(incoming);
    }
    if ((x & 2) == 2) {
      keypad(scr_GetInputWindow(), TRUE);
      key = scr_Getch();
      ret = process_key(key, sock);
      /*
      switch (key) {
      case KEY_IC:
	bud_AddBuddy(sock);
	break;
      case KEY_DC:
	bud_DeleteBuddy(sock);
	break;

      case KEY_RESIZE:
	endwin();
	printf("\nRedimensionado no implementado\n");
	printf("Reinicie Cabber.\n\n\n");
	exit(EXIT_FAILURE);
	break;
      }
      */
    }
    if (update_roaster) {
      // scr_LogPrint("Update roaster");
      bud_DrawRoster(scr_GetRosterWindow());
    }
  }

  bud_TerminateBuddies();
  scr_TerminateCurses();

  srv_setpresence(sock, "unavailable");

  close(sock);

  printf("\n\nHave a nice day!\nBye!\n");

  return 0;
}
