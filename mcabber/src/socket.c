#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#include "utils.h"
#include "socket.h"
#include "screen.h"

/* Desc: create socket connection
 * 
 * In  : servername, port
 * Out : socket (or -1 on error)
 *
 * Note: -
 */
int sk_conn(struct sockaddr *name)
{
  int sock;

  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket (socket.c:23)");
    return -1;
  }

  if (connect(sock, (struct sockaddr *) name, sizeof(struct sockaddr)) < 0) {
    perror("connect (socket.c:29)");
    return -1;
  }

  return sock;
}


/* Desc: send data through socket
 * 
 * In  : socket, buffer to send
 * Out : 0 = fail, 1 = pass
 *
 * Note: -
 */
int sk_send(int sock, char *buffer)
{
  //ut_WriteLog("Sending:%s\n", buffer);
  if ((send(sock, buffer, strlen(buffer), 0)) == -1)
    return 0;
  else
    return 1;
}

/* Desc: receive data through socket
 * 
 * In  : socket
 * Out : received buffer
 *
 * Note: it is up to the caller to free the returned string
 */
char *sk_recv(int sock)
{
  int i = 1;
  int tambuffer = 128;
  char mtag[16];    // For tag name

  char *buffer = calloc(1, tambuffer);
  char *retval = calloc(1, tambuffer + 1);

  while (1) {
    char *p1;
    int n = recv(sock, buffer, tambuffer, 0);
    if (n == -1) {
      // Error
      free(buffer);
      retval[0] = 0;
      return retval;
    }

    if (i == 1) {
      char *p2;
      strncpy(retval, buffer, tambuffer);
      p1 = retval+1;
      p2 = mtag;
      while (('a' <= *p1) && (*p1 <= 'z') && (p2-mtag < 14))
        *p2++ = *p1++;
      *p2++ = '>'; *p2++ = 0;
      //fprintf(stderr, "TAG=\"%s\"\n", mtag);
    } else {
      char *old_retval = retval;
      scr_LogPrint("Realloc %d [%d]", i-1, n);
      if (!n)
        break;
      retval = realloc(retval, (tambuffer * i) + 1);
      if (!retval) {
        scr_LogPrint("Memory allocation failure!!");
        ut_WriteLog("Memory allocation failure in sk_recv()\n", retval);
        free(buffer);
        return old_retval;
      }
      strncat(retval, buffer, tambuffer);
    }
    i++;
    p1 = retval + strlen(retval) - strlen(mtag);
    //fprintf(stderr, "buffer:[%s]\n", buffer);
    //fprintf(stderr, "End RET=[%s]\n", p1);
    if (!strcmp(p1, mtag))
      break;
    for (p1 = retval; *p1 && (*p1 != '>'); p1++);
    if ((*p1 == '>') && (*(p1-1) == '/'))
      break;
    memset(buffer, 0, tambuffer);
  }
  free(buffer);
  ut_WriteLog("Received:%s\n", retval);
  return retval;
}
