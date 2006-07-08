#ifndef __CONNWRAP_H__
#define __CONNWRAP_H__

#if HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/socket.h>

int cw_connect(int sockfd, const struct sockaddr *serv_addr, int addrlen, int ssl);

#define CW_CONNECT_STARTED 0x1
#define CW_CONNECT_SSL 0x2
#define CW_CONNECT_WANT_READ 0x4
#define CW_CONNECT_WANT_WRITE 0x8
#define CW_CONNECT_WANT_SOMETHING 0xC
#define CW_CONNECT_BLOCKING 0x10

/* non-blocking socket
   state should be initialized with 0, subsequent calls should keep the
   modified state (state is a bitwise OR between CW_CONNECT_XXX)
   returns 0 for OK, or if it wants subsequent calls
	   -1 for a fatal error
 */
int cw_nb_connect(int sockfd, const struct sockaddr *serv_addr, int addrlen, int ssl, int *state);
int cw_accept(int s, struct sockaddr *addr, int *addrlen, int ssl);

int cw_write(int fd, const void *buf, int count, int ssl);
int cw_read(int fd, void *buf, int count, int ssl);

void cw_close(int fd);

void cw_set_ssl_options(int sslverify, const char *sslcafile, const char *sslcapath, const char *sslciphers, const char *sslpeer);
const char *cw_get_ssl_error(void);
void cw_setproxy(const char *aproxyhost, int aproxyport, const char *aproxyuser, const char *aproxypass);
void cw_setbind(const char *abindaddr);

char *cw_base64_encode(const char *in);

#ifdef __cplusplus
}
#endif

#endif
