/*
Copyright (C) 2011 Cem Saldırım <cem.saldirim@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "serv_internal.h"
#include "serv_tcp.h"

#ifdef _WIN32
int read(int fd, char *buffer, int size) {
    return recv(fd, buffer, size, 0);
}

int write(int fd, char *buffer, int size) {
    return send(fd, buffer, size, 0);
}
#endif

int srv_setnoblock(int fd) {
#ifndef _WIN32
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;

    return fcntl(fd, F_SETFL, flags);
#else
    u_long argp = 1;
    return ioctlsocket(fd, FIONBIO, &argp);
#endif
}

int srv_tcp_create_listener(srv_t *ctx) {
    int status, fd, reuse_addr;
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(!ctx->host)
        hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(ctx->host, ctx->port, &hints, &servinfo);
    if(status) {
        /* TODO: Check the error code and set errno appropriately */
        return -1;
    }

    fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(fd == -1)
        return -1;

    /* Make the socket available for reuse immediately after it's closed */
    reuse_addr = 1;
#ifdef _WIN32
    /* NOTE: It turns out Win32 definition of SO_REUSEADDR is not the same as the POSIX definition.
	   Instead we use SO_EXCLUSIVEADDRUSE */
    status = setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&reuse_addr, sizeof(reuse_addr));
#else
    status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
#endif
    if(status == -1)
        return -1;

    /* Bind the socket to the address */
    status = bind(fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(status == -1)
        return -1;

    /* Listen for incoming connections */
    status = listen(fd, ctx->backlog);
    if(status == -1)
        return -1;

    return fd;
}

int srv_tcp_accept(int fd, char *ip, int *port, int flags) {
    int fd_new;
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

#ifdef linux
    fd_new = accept4(fd, (struct sockaddr *) &addr, &addrlen, flags);
#else
    /* Fallback to accept if accept4 is not implemented */
    fd_new = accept(fd, (struct sockaddr *) &addr, &addrlen);
#endif

    if(fd_new == -1) {
        return -1;
    }
    else {
#ifndef linux
        /* accept() doesn't accept flags so we need to set non-blocking mode manually */
        if(flags == SOCK_NONBLOCK)
            srv_setnoblock(fd_new);
#endif
        /* Fill in address info buffers */
        if(addr.ss_family == AF_INET) {
            /* IPv4 */
            struct sockaddr_in *s = (struct sockaddr_in *) &addr;

            if(ip) inet_ntop(AF_INET, &s->sin_addr, ip, INET6_ADDRSTRLEN);
            if(port) *port = ntohs(s->sin_port);
        }
        else {
            /* IPv6 */
            struct sockaddr_in6 *s = (struct sockaddr_in6 *) &addr;

            if(ip) inet_ntop(AF_INET6, &s->sin6_addr, ip, INET6_ADDRSTRLEN);
            if(port) *port = ntohs(s->sin6_port);
        }

        return fd_new;
    }
}
