#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "list.h"
#include "parsecfg.h"
#include "screen.h"
#include "socket.h"
#include "utf8.h"
#include "server.h"
#include "harddefines.h"
#include "utils.h"
#include "buddies.h"

#define JABBERPORT 5222

/* Desc: poll data from server
 * 
 * In  : socket
 * Out : pending buffer (or NULL if no incoming data)
 *
 * Note: it is up to the caller to free the returned string
 */
char *srv_poll(int sock)
{
  struct pollfd sock_p;
  sock_p.fd = sock;
  sock_p.events = (short int) (POLLIN | POLLPRI);
  sock_p.revents = 0;
  poll(&sock_p, 1, 0);

  if (sock_p.revents) {
    return sk_recv(sock);
  }

  return NULL;
}


/* Desc: resolve host
 * 
 * In  : hostname
 * Out : 32bit address (or 0 if error)
 *
 * Note: -
 */
static u_long srv_resolve(const char *host)
{
  in_addr_t i;
  struct hostent *he;

  if ((i = inet_addr(host)) == INADDR_NONE) {
    if (!(he = gethostbyname(host)))
      return 0;
    else
      return (*(u_long *) he->h_addr); // XXX Memory leak
  }

  return (u_long) i;
}


/* Desc: connect to jabber server
 * 
 * In  : config
 * Out : socket (or -1 on error)
 *
 * Note: if port is -1, the default Jabber port will be used
 */
int srv_connect(const char *server, unsigned int port)
{
  struct sockaddr_in name;
  int sock;

  if (server == NULL) {
    fprintf(stderr, "You must supply a server name\n\r");
    return -1;
  }

  if (port == -1U) {
    port = JABBERPORT;
  }

  name.sin_family = AF_INET;
  name.sin_port = htons(port);

  if (!(name.sin_addr.s_addr = srv_resolve(server))) {
    fprintf(stderr, "Cant resolve \"%s\"\n", server);
    return -1;
  }

  if ((sock = sk_conn((struct sockaddr *) &name)) < 0) {
    fprintf(stderr, "Can't connect to \"%s:%u\"\n", server, port);
    return -1;
  }

  return sock;
}


/* Desc: login into jabber server
 * 
 * In  : socket, servername, user, password, resource
 * Out : idsession
 *
 * Note: it is up to the caller to free the returned string
 */
char *srv_login(int sock, const char *server, const char *user,
		const char *pass, const char *resource)
{
  char *stringtosend = malloc(2048);
  char *response, *aux;
  char *idsession = calloc(64, sizeof(char));
  int pos = 0;

  memset(stringtosend, 0, 2048);
  strcpy(stringtosend, "<?xml version='1.0' encoding='UTF-8' ?>");
  strcat(stringtosend, "<stream:stream to='");
  strcat(stringtosend, server);
  strcat(stringtosend, "' xmlns='jabber:client' xmlns:stream='");
  strcat(stringtosend, "http://etherx.jabber.org/streams'>\n");

  if (!sk_send(sock, stringtosend)) {
    perror("senddata (server.c:132)");
    free(stringtosend);
    free(idsession);
    return NULL;
  }
  response = sk_recv(sock);
  if (strstr(response, "error")) {
    /* fprintf(stderr, "Response not valid:\n%s\n\n", response); */
    scr_CreatePopup("Error",
		    "Bad answer from the server", 60, 0, NULL);
    free(response);
    free(idsession);
    free(stringtosend);
    return NULL;
  }
  aux = response;
  while (strncmp(aux, "id", 2))
    aux++;
  pos = 0;
  aux += 4;
  while (strncmp(aux, "'", 1)) {
    aux++;
    pos++;
  }
  aux -= pos;
  if (pos > 64-1) {
    ut_WriteLog("Bad session ID!\n");
    free(response);
    free(idsession);
    free(stringtosend);
    return NULL;
  }
  strncpy(idsession, aux, (size_t)pos);

  free(response);

  strcpy(stringtosend, "<iq type='set' id='1000'>");
  strcat(stringtosend, "<query xmlns='jabber:iq:auth'>");
  strcat(stringtosend, "<username>");
  strcat(stringtosend, user);
  strcat(stringtosend, "</username><password>");
  strcat(stringtosend, pass);
  strcat(stringtosend, "</password><resource>");
  strcat(stringtosend, resource);
  strcat(stringtosend, "</resource></query></iq>\n");
  if (!sk_send(sock, stringtosend)) {
    perror("senddata (server.c:167)");
    return NULL;
  }
  response = sk_recv(sock);
  if (strstr(response, "error")) {
/*	fprintf(stderr, "Response not valid:\n%s\n\n", response);*/
    scr_CreatePopup("Error",
		    "Account doesn't exist, or bad password", 60, 0,
		    NULL);

    /*
    scr_CreatePopup("Info", "Trying to create the account...", 60, 0, NULL);

    strcpy(stringtosend, "<iq type='set' id='reg' to='");
    strcat(stringtosend, server);
    strcat(stringtosend, "'>");
    strcat(stringtosend, "<query xmlns='jabber:iq:register'>");
    strcat(stringtosend, "<username>");
    strcat(stringtosend, user);
    strcat(stringtosend, "</username><password>");
    strcat(stringtosend, pass);
    strcat(stringtosend, "</password>");
    strcat(stringtosend, "</query></iq>\n");
    if (!sk_send(sock, stringtosend)) {
      perror("senddata (server.c:167)");
      return NULL;
    }

    response = sk_recv(sock);
    */
    scr_TerminateCurses();
    /* printf("Reinicie cabber!\n\n"); */
    free(idsession);
    free(stringtosend);
    return NULL;
  }
  free(response);
  free(stringtosend);

  return idsession;
}


/* Desc: broadcast presence
 * 
 * In  : socket, presence string
 * Out : ?
 *
 * Note: see `sk_send' for output values
 */
int srv_setpresence(int sock, const char *type)
{
  int rv;
  char *str = malloc(1024);

  sprintf(str, "<presence><status>%.512s</status></presence>", type);
  if (!(rv = sk_send(sock, str))) {
    perror("senddata (server.c:199)");
  }
  free(str);

  return rv;
}


/* Desc: request roster
 * 
 * In  : socket
 * Out : roster string
 *
 * Note: it is up to the caller to free the returned string
 */
char *srv_getroster(int sock)
{
  char *str = malloc(1024);

  strcpy(str, "<iq type='get' id='1001'><query xmlns='");
  strcat(str, "jabber:iq:roster'/></iq>\n");
  if (!sk_send(sock, str)) {
    perror("senddata (server.c:222)");
    free(str);
    return NULL;
  }
  free(str);

  return sk_recv(sock);
}


/* Desc: send text to buddy
 * 
 * In  : socket, destination jid, text, source jid
 * Out : 0 = ok
 *
 * Note: -
 */
int
srv_sendtext(int sock, const char *to, const char *text, const char *from)
{
  char *stringtosend = malloc(2048);
  char *utf8inputline = utf8_encode(text);

  sprintf(stringtosend,
	  "<message from='%s' to='%s' type='chat'><body>%.1536s</body></message>",
	  from, to, utf8inputline);
  if (!sk_send(sock, stringtosend)) {
    perror("senddata (server.c:247)");
    free(stringtosend);
    free(utf8inputline);
    return -1;
  }

  free(stringtosend);
  free(utf8inputline);
  return 0;
}

int check_io(int fd1, int fd2)
{
  int n = 0, i;
  fd_set fds;
  int io_pending = 0;

  i = fd1;
  if (fd2 > fd1)
    i = fd2;

  FD_ZERO(&fds);
  if (fd1 >= 0)
    FD_SET(fd1, &fds);
  else
    fd1 = 0;
  if (fd2 >= 0)
    FD_SET(fd2, &fds);
  else
    fd2 = 0;

  if (fd2 == 0 && io_pending)
    n = 2;
  else if (select(i + 1, &fds, NULL, NULL, NULL) > 0)
    n = 1 * (FD_ISSET(fd1, &fds) > 0) + 2 * (FD_ISSET(fd2, &fds) > 0);

  return (n);
}

/* Desc: read data from server
 *
 * In  : socket
 * Out : ptr to newly allocated srv_msg struct
 *
 * Note: returns NULL if no input from server
 */
srv_msg *readserver(int sock)
{
  char *buffer = sk_recv(sock);

  if (buffer != NULL) {
    srv_msg *msg = calloc(1, sizeof(srv_msg));
    char *to = getattr(buffer, "to='");
    char *from = getattr(buffer, "from='");
    char *id = getattr(buffer, "id='");
    char *type = getattr(buffer, "type='");
    char *body = gettag(buffer, "body");
    char *status = gettag(buffer, "status");
    char *show = gettag(buffer, "show");
    char *line = (char *) malloc(1024);
    memset(line, 0, 1024);

    /* scan for buffer */
    if (!strncmp(buffer, "<message", 8)) {		/* manage messages */
      msg->m = SM_MESSAGE;
    } else if (!strncmp(buffer, "<presence", 9)) {	/* manage presences */
      msg->m = SM_PRESENCE;
      if (!strncmp(type, "UNK", 3)) {			/* assume online */
	msg->connected = FLAG_BUDDY_CONNECTED;
      } else if (!strncmp(type, "unavailable", 11)) {	/* offline */
	msg->connected = 0;
      }
    } else {
      msg->m = SM_UNHANDLED;
    }

    /* write the parsed buffer */
    switch (msg->m) {
    case SM_MESSAGE:
      {
	char *aux = strstr(from, "/");
	if (aux)
	  *aux = '\0';
	msg->from = from;
	msg->body = utf8_decode(body);
	ut_WriteLog("+OK [%s]\n", buffer);
      }
      break;

    case SM_PRESENCE:
      {
	char *aux = strstr(from, "/");
	if (aux)
	  *aux = '\0';
	msg->from = from;
      }
      break;

    case SM_UNHANDLED:
      ut_WriteLog("BAD [%s]\n", buffer);
      break;

    }
    free(line);
    if (strncmp(to, "UNK", 3))
      free(to);
    if (strncmp(from, "UNK", 3) && (msg->m != SM_MESSAGE)
	&& (msg->m != SM_PRESENCE))
      free(from);
    if (strncmp(id, "UNK", 3))
      free(id);
    if (strncmp(type, "UNK", 3))
      free(type);
    if (strncmp(body, "UNK", 3))
      free(body);
    if (strncmp(status, "UNK", 3))
      free(status);
    if (strncmp(show, "UNK", 3))
      free(show);
    free(buffer);

    return msg;
  }

  return NULL;
}

void srv_AddBuddy(int sock, char *jidname)
{
  char *buffer = (char *) malloc(1024);
  char *p, *str;
  int i;

  memset(buffer, 0, 1024);
  strcpy(buffer, "<iq type='set'>");
  strcat(buffer, "  <query xmlns='jabber:iq:roster'>");
  strcat(buffer, "    <item");
  strcat(buffer, "      jid='");
  strcat(buffer, jidname);
  strcat(buffer, "' name='");

  str = strdup(jidname);
  p = strstr(str, "@");
  if (p)
    *p = '\0';
  strcat(buffer, str);
  strcat(buffer, "'/></query></iq>");
  sk_send(sock, buffer);
  free(buffer);

  for (i = 0; i < 2; i++) {
    buffer = sk_recv(sock);
    ut_WriteLog("[Subscription]: %s\n", buffer);
    free(buffer);
  }

  buffer = (char *) malloc(1024);
  memset(buffer, 0, 1024);
  strcpy(buffer, "<presence to='");
  strcat(buffer, jidname);
  strcat(buffer, "' type='subscribe'>");
  strcat(buffer, "<status>I would like to add you!</status></presence>");
  sk_send(sock, buffer);
  free(buffer);

  buffer = sk_recv(sock);
  ut_WriteLog("[Subscription]: %s\n", buffer);
  free(buffer);

  buffer = (char *) malloc(1024);
  memset(buffer, 0, 1024);
  strcpy(buffer, "<presence to='");
  strcat(buffer, jidname);
  strcat(buffer, "' type='subscribed'/>");
  sk_send(sock, buffer);
  free(buffer);

  buffer = sk_recv(sock);
  ut_WriteLog("[Subscription]: %s\n", buffer);
  free(buffer);
}

void srv_DelBuddy(int sock, char *jidname)
{
  char *buffer = (char *) malloc(1024);

  strcpy(buffer, "<iq type='set'><query xmlns='jabber:iq:roster'>");
  strcat(buffer, "<item jid='");
  strcat(buffer, jidname);
  strcat(buffer, "' subscription='remove'/></query></iq>");

  sk_send(sock, buffer);
  free(buffer);

  buffer = sk_recv(sock);
  ut_WriteLog("[SubscriptionRemove]: %s\n", buffer);
  free(buffer);
}
