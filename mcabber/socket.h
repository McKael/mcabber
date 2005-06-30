#ifndef __SOCKET_H__
#define __SOCKET_H__ 1

#include <sys/socket.h>

int sk_conn(struct sockaddr *name);
int sk_send(int sock, char *buffer);
char *sk_recv(int sock);

#endif
