/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Jabber
 *  Copyright (C) 1998-1999 The Jabber Team http://jabber.org/
 */

#include "libxode.h"
#include "connwrap.h"

/* socket.c
 *
 * Simple wrapper to make socket creation easy.
 * type = NETSOCKET_SERVER is local listening socket
 * type = NETSOCKET_CLIENT is connection socket
 * type = NETSOCKET_UDP
 */

int make_netsocket(u_short port, char *host, int type, int ssl)
{
    int s, flag = 1;
    struct sockaddr_in sa;
    struct in_addr *saddr;
    int socket_type;

    /* is this a UDP socket or a TCP socket? */
    socket_type = (type == NETSOCKET_UDP)?SOCK_DGRAM:SOCK_STREAM;

    bzero((void *)&sa,sizeof(struct sockaddr_in));

    if((s = socket(AF_INET,socket_type,0)) < 0)
	return(-1);
    if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag)) < 0)
	return(-1);

    saddr = make_addr(host);
    if(saddr == NULL)
	return(-1);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if(type == NETSOCKET_SERVER)
    {
	/* bind to specific address if specified */
	if(host != NULL)
	    sa.sin_addr.s_addr = saddr->s_addr;

	if(bind(s,(struct sockaddr*)&sa,sizeof sa) < 0)
	{
	    close(s);
	    return(-1);
	}
    }
    if(type == NETSOCKET_CLIENT)
    {
	sa.sin_addr.s_addr = saddr->s_addr;
	if(cw_connect(s,(struct sockaddr*)&sa,sizeof sa,ssl) < 0)
	{
	    close(s);
	    return(-1);
	}
    }
    if(type == NETSOCKET_UDP)
    {
	/* bind to all addresses for now */
	if(bind(s,(struct sockaddr*)&sa,sizeof sa) < 0)
	{
	    close(s);
	    return(-1);
	}

	/* specify default recipient for read/write */
	sa.sin_addr.s_addr = saddr->s_addr;
	if(cw_connect(s,(struct sockaddr*)&sa,sizeof sa,ssl) < 0)
	{
	    close(s);
	    return(-1);
	}
    }


    return(s);
}

void change_socket_to_blocking(int s)
     /* make socket blocking */
{
    int val;
    val = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, val & (~O_NONBLOCK));
}

void change_socket_to_nonblocking(int s)
     /* make socket non-blocking */
{
    int val;
    val = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, val | O_NONBLOCK);
}

/* socket.c
 *
 * Simple wrapper to make non-blocking client socket creation easy.
 * type = NETSOCKET_SERVER is local listening socket
 * type = NETSOCKET_CLIENT is connection socket
 * type = NETSOCKET_UDP
 */

int make_nb_netsocket(u_short port, char *host, int type, int ssl, int* state)
{
    int s, flag = 1;
    struct sockaddr_in sa;
    struct in_addr *saddr;
    int socket_type;

    /* is this a UDP socket or a TCP socket? */
    socket_type = (type == NETSOCKET_UDP)?SOCK_DGRAM:SOCK_STREAM;

    bzero((void *)&sa,sizeof(struct sockaddr_in));

    if((s = socket(AF_INET,socket_type,0)) < 0)
	return(-1);
    if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag)) < 0)
	return(-1);
    change_socket_to_nonblocking(s);

    saddr = make_addr(host);
    if(saddr == NULL)
	return(-1);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if(type == NETSOCKET_SERVER)
    {
	/* bind to specific address if specified */
	if(host != NULL)
	    sa.sin_addr.s_addr = saddr->s_addr;

	if(bind(s,(struct sockaddr*)&sa,sizeof sa) < 0)
	{
	    close(s);
	    return(-1);
	}
    }
    if(type == NETSOCKET_CLIENT)
    {
	int rc;
	sa.sin_addr.s_addr = saddr->s_addr;
	rc = cw_nb_connect(s,(struct sockaddr*)&sa,sizeof sa,ssl, state);
	if (rc == -1 )
	{
	    close(s);
	    return(-1);
	}
    }
    if(type == NETSOCKET_UDP)
    {
	/* bind to all addresses for now */
	if(bind(s,(struct sockaddr*)&sa,sizeof sa) < 0)
	{
	    close(s);
	    return(-1);
	}

	/* specify default recipient for read/write */
	sa.sin_addr.s_addr = saddr->s_addr;
	if(cw_connect(s,(struct sockaddr*)&sa,sizeof sa,ssl) < 0)
	{
	    close(s);
	    return(-1);
	}
    }


    return(s);
}

struct in_addr *make_addr(char *host)
{
    struct hostent *hp;
    static struct in_addr addr;
    char myname[MAXHOSTNAMELEN + 1];

    if(host == NULL || strlen(host) == 0)
    {
	gethostname(myname,MAXHOSTNAMELEN);
	hp = gethostbyname(myname);
	if(hp != NULL)
	{
	    return (struct in_addr *) *hp->h_addr_list;
	}
    }else{
	addr.s_addr = inet_addr(host);
	if(addr.s_addr != -1)
	{
	    return &addr;
	}
	hp = gethostbyname(host);
	if(hp != NULL)
	{
	    return (struct in_addr *) *hp->h_addr_list;
	}
    }
    return NULL;
}

/* Sets a file descriptor to close on exec.  "flag" is 1 to close on exec, 0 to
 * leave open across exec.
 * -- EJB 7/31/2000
 */
int set_fd_close_on_exec(int fd, int flag)
{
    int oldflags = fcntl(fd,F_GETFL);
    int newflags;

    if(flag)
	newflags = oldflags | FD_CLOEXEC;
    else
	newflags = oldflags & (~FD_CLOEXEC);

    if(newflags==oldflags)
	return 0;
    return fcntl(fd,F_SETFL,(long)newflags);
}

