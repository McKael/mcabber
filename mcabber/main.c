#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <termios.h>

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
  if (tcgetattr(fileno (stdin), &orig) != 0)
      return -1;
  new = orig;
  new.c_lflag &= ~ECHO;
  if (tcsetattr(fileno (stdin), TCSAFLUSH, &new) != 0)
      return -1;

  /* Read the password. */
  nread = getline(passstr, n, stdin);

  /* Restore terminal. */
  (void) tcsetattr(fileno (stdin), TCSAFLUSH, &orig);

  return nread;
}

void credits(void)
{
  printf(VERSION "\n");
  printf(EMAIL "\n");
}

int main(int argc, char **argv)
{
  int i;
  char configFile[4096];
  char *buffer;
  char *secbuffer;
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
  port = (portstring != NULL) ? atoi(portstring) : -1;

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
  ut_WriteLog("Connected to %s: %s\n", servername, idsession);
  free(idsession);

  ut_WriteLog("Requesting roster...\n");
  bud_InitBuddies(sock);

  ut_WriteLog("Sending presence...\n");
  srv_setpresence(sock, "Online!");


  ut_WriteLog("Drawing main window...\n");
  scr_DrawMainWindow();

  ping = 15;
  if (cfg_read("pinginterval"))
    ping = atoi(cfg_read("pinginterval"));

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
      keypad(scr_GetRosterWindow(), TRUE);
      key = scr_Getch();
      ret = process_key(key);
      /*
      switch (key) {
      case KEY_IC:
	bud_AddBuddy(sock);
	break;
      case KEY_DC:
	bud_DeleteBuddy(sock);
	break;
      case KEY_DOWN:
	bud_RosterDown();
	break;
      case KEY_UP:
	bud_RosterUp();
	break;

      case 0x19a:
	endwin();
	printf("\nRedimensionado no implementado\n");
	printf("Reinicie Cabber.\n\n\n");
	exit(EXIT_FAILURE);
	break;

      case KEY_NPAGE:
	for (i = 0; i < 10; i++)
	  bud_RosterDown();
	break;

      case KEY_PPAGE:
	for (i = 0; i < 10; i++)
	  bud_RosterUp();
	break;

      case 'z':
      case KEY_F(1):
	buffer = (char *) calloc(1, 4096);
	secbuffer = (char *) calloc(1, 4096);

	sprintf(secbuffer, "INS   = %s ", i18n("Add contact"));
	i = strlen(secbuffer);
	strcpy(buffer, secbuffer);
	sprintf(secbuffer, "DEL   = %s ", i18n("Delete contact"));
	strcat(buffer, secbuffer);
	sprintf(secbuffer, "SPACE = %s ", i18n("View buddy window"));
	strcat(buffer, secbuffer);
	sprintf(secbuffer, "INTRO = %s ", i18n("Send message"));
	strcat(buffer, secbuffer);
	sprintf(secbuffer, "ESC   = %s ", i18n("Exit"));
	strcat(buffer, secbuffer);

	scr_CreatePopup(i18n("help"), buffer, i, 0, NULL);
	free(buffer);
	free(secbuffer);
	break;

      case '\n':
	scr_WriteMessage(sock);
	break;

      case ' ':
	scr_ShowBuddyWindow();
	break;
      }
      */
    }
  }

  bud_TerminateBuddies();
  scr_TerminateCurses();

  srv_setpresence(sock, "unavailable");

  close(sock);

  printf("\n\nHave a nice day!\nBye!\n");

  return 0;
}
