#ifndef __SERVER_H__
#define __SERVER_H__ 1

typedef enum {
  SM_MESSAGE,
  SM_PRESENCE,
  SM_UNHANDLED
} SRV_MSGTYPE;

typedef struct {
  SRV_MSGTYPE m;		/* message type: see above! */
  int connected;		/* meaningful only with SM_PRESENCE */
  char *from;			/* sender */
  char *body;			/* meaningful only with SM_MESSAGE */
} srv_msg;

char *srv_poll(int sock);
int srv_connect(const char *server, unsigned int port);
char *srv_login(int sock, const char *server, const char *user,
		const char *pass, const char *resource);
int srv_setpresence(int sock, const char *type);
char *srv_getroster(int sock);
int srv_sendtext(int sock, const char *to, const char *text,
		 const char *from);
int check_io(int fd1, int fd2);
srv_msg *readserver(int sock);
void srv_DelBuddy(int sock, char *jidname);
void srv_AddBuddy(int sock, char *jidname);
#endif
