/*
Copyright (C) 2011 Cem Saldırım <bytesong@gmail.com>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>

#include "serv.h"

#ifdef __linux__
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#endif

#ifdef _MSC_VER
#define inline __inline
#else
#define inline __inline__
#endif

#ifdef _WIN32

#include <Winsock2.h>
#include <Ws2tcpip.h>

#define close closesocket
#define SHUT_RDWR SD_BOTH

static inline int read(int fd, char *buffer, int size) {
    return recv(fd, buffer, size, 0);
}

static inline int write(int fd, char *buffer, int size) {
    return send(fd, buffer, size, 0);
}
#endif

/* Detect the best event notification mechanism available */
#ifdef __linux__
    #include <linux/version.h>
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,44)
        #define EPOLL
    #else
    /* TODO: Check if poll, kqueue or IOCP is avalable.
If not, fallback to select */
        #define SELECT
    #endif
#else
	#ifdef _WIN32
    	/* TODO: Check if IOCP exists. Otherwise, fallback to select */
    	#define SELECT
	#endif
#endif

#ifdef EPOLL
    #include <sys/epoll.h>
#endif

#ifdef SELECT
    #ifdef _WIN32
        #define FD_SETSIZE 10000 /* TODO: Find the max # of open sockets instead of a hardcoded value */
    #else
        #define FD_SETSIZE FOPEN_MAX
    #endif
    #ifdef __linux__
        #include <sys/select.h>
    #endif
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

#ifdef __GNUC__
#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#else
#define likely(x) x
#define unlikely(x) x
#endif

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 1
#endif

static int setnoblock(int fd) {
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

static int tcp_create_listener(srv_t *ctx, char *hostname, char *port) {
    int status, fd, reuse_addr;
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if(!hostname)
        hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(hostname, port, &hints, &servinfo);
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

int srv_connect(char *hostname, char *port) {
    int status, fd;
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(hostname, port, &hints, &servinfo);
    if(status) {
        /* TODO: Check the error code and set errno appropriately */
        return -1;
    }

    fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(fd == -1)
        return -1;

    status = connect(fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(status == -1)
        return -1;

    freeaddrinfo(servinfo);

    return fd;
}

int srv_closeconn(int fd) {
    /* TODO: Remove from event mechanism */
    /* TODO: shutdown? */
    return close(fd);
}

static int tcp_accept(int fd, char *ip, int *port, int flags) {
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
            setnoblock(fd_new);
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

int srv_read(int fd, char *buf, int size) {
    /* TODO: Implementation */
    return 0;
}

int srv_readall(int fd, char *buf, int size) {
    /* TODO: Handle EINTR */
    int nread, total_read = 0;

    /* Make sure 'size' bytes are read */
    while(total_read != size) {
        nread = read(fd, buf, size - total_read);

        if(nread <= 0)
            return total_read;

        total_read += nread;
        buf += nread;
    }
    return total_read;
}

int srv_write(int fd, char *buf, int size) {
    /* TODO: Implementation */
    return 0;
}

int srv_writeall(int fd, char *buf, int size) {
    /* TODO: Handle EINTR */
    int nwritten, total_written = 0;

    /* Make sure 'size' bytes are written */
    while(total_written != size) {
        nwritten = write(fd, buf, size - total_written);

        switch(nwritten) {
            case 0: return total_written; break;
            case -1: return -1; break;
        }

        total_written += nwritten;
        buf += nwritten;
    }
    return total_written;
}

#ifdef EPOLL

#define EVENTRD EPOLLIN
#define EVENTWR EPOLLOUT
#define EVENTHUP EPOLLHUP
#define EVENTRDHUP EPOLLRDHUP
#define EVENTERR EPOLLERR

typedef struct {
    struct epoll_event *events;
    int epfd, nfds, fd_index, max_events;
} event_t;

static inline int event_init(event_t *ev, int max_events) {
    ev->epfd = epoll_create1(0);
    ev->nfds = 0;
    ev->fd_index = 0;
    ev->max_events = max_events;

    ev->events = calloc(max_events, sizeof(struct epoll_event));
    if(ev->events == NULL)
        return -1;

    return ev->epfd;
}

static inline int event_add_fd(event_t *ev, int fd, uint32_t flags) {
        struct epoll_event tmp_event;

        tmp_event.data.fd = fd;
        tmp_event.events = flags | EPOLLET; /* Use the edge-triggered mode */

        return epoll_ctl(ev->epfd, EPOLL_CTL_ADD, fd, &tmp_event);
}

static inline int event_remove_fd(event_t *ev, int fd) {
    struct epoll_event tmp_event; /* Required for linux versions before 2.6.9 */

    return epoll_ctl(ev->epfd, EPOLL_CTL_DEL, fd, &tmp_event);
}

static inline int event_wait(event_t *ev, int *event_fd, int *event_type) {
    if(ev->nfds == 0) {
        /* All events processed so far. Wait for new events */
        ev->fd_index = 0;
        ev->nfds = epoll_wait(ev->epfd, ev->events, ev->max_events, -1);
    }

    /* Preserve the errno and notify the caller that an error has occured */
    if(ev->nfds <= 0) {
        /* Error occured or there are no events waiting to be handled */
        *event_type = 0;
        return ev->nfds; /* Return value is -1 on error */
    }

    /* Pass the next event to the caller */
    *event_type = ev->events[ev->fd_index].events;
    *event_fd = ev->events[ev->fd_index].data.fd;

    /* Point to the next event to be handled */
    if(ev->fd_index < ev->nfds)
        ev->fd_index++;
    else {
        ev->nfds = 0;
        *event_type = 0;
        return 0; /* All events have been handled */
    }

    /* Return the number of events waiting to be handled */
    return (ev->nfds - ev->fd_index);
}

static inline int event_free(event_t *ev) {
    free(ev->events);

    if(close(ev->epfd) == -1)
        return -1;
}
#endif

#ifdef SELECT

#define EVENTRD 1
#define EVENTWR 2
#define EVENTHUP 4
#define EVENTRDHUP 8
#define EVENTERR 16

typedef struct {
    fd_set fds_read_master, fds_read, fds_write_master, fds_write;
    int fdmax, nfds, fd_index;
} event_t;

static inline int event_init(event_t *ev, int max_events) {
    /* Initialize the fd sets */
    FD_ZERO(&(ev->fds_read_master));
    FD_ZERO(&(ev->fds_read));

    FD_ZERO(&(ev->fds_write_master));
    FD_ZERO(&(ev->fds_write));

    ev->fdmax = 0;
    ev->nfds = 0;
    ev->fd_index = 0;

    return 0;
}

static inline int event_add_fd(event_t *ev, int fd, uint32_t flags) {
    if(fd >= FD_SETSIZE) {
        errno = EBUSY; /* fd set is full */
        return -1;
    }

    if(flags & EVENTRD) {
        /* Add the fd to the read fd_set */
        FD_SET(fd, &(ev->fds_read_master));
    }

    if(flags & EVENTWR) {
        /* Add the fd to the write fd_set */
        FD_SET(fd, &(ev->fds_write_master));
    }

    /* Update fdmax */
    if(fd > ev->fdmax)
        ev->fdmax = fd;

    return 0;
}

static inline int event_remove_fd(event_t *ev, int fd) {
    /* Remove from all fd sets */
    FD_CLR(fd, &(ev->fds_read_master));
    FD_CLR(fd, &(ev->fds_write_master));
    FD_CLR(fd, &(ev->fds_read));
    FD_CLR(fd, &(ev->fds_write));

    /* Update fdmax */
    if(fd == ev->fdmax) {
        ev->fdmax--;

        /* The fd that is being handled has just been removed. All the previous
fds have already been handled */
        if(fd == ev->fd_index)
            ev->nfds = 0;
    }

    return 0;
}

static inline int event_wait(event_t *ev, int *event_fd, int *event_type) {
    if(ev->nfds == 0) {
        /* All events processed so far. Wait for new events */
        ev->fds_read = ev->fds_read_master;
        ev->fds_write = ev->fds_write_master;
        ev->fd_index = 0;

        ev->nfds = select(ev->fdmax + 1, &(ev->fds_read),
                          &(ev->fds_write), NULL, NULL);
    }

    if(ev->nfds == -1) {
        /* An error occured */
        *event_type = EVENTERR;
        ev->nfds = 0;

        return -1;
    }
    else if(ev->nfds == 0) {
        /* No events waiting to be processed */
        *event_type = 0;

        return 0;
    }

    /* Find the next ready fd */
    for(;(ev->fd_index <= ev->fdmax) && (ev->nfds > 0); ev->fd_index++) {
        *event_type = 0;
        if(FD_ISSET(ev->fd_index, &(ev->fds_read))) {
            /* fd ready for read */
            *event_fd = ev->fd_index;
            *event_type |= EVENTRD;
            ev->nfds--;
            FD_CLR(ev->fd_index, &(ev->fds_read));
        }

        if(FD_ISSET(ev->fd_index, &(ev->fds_write))) {
            /* fd ready for write */
            *event_fd = ev->fd_index;
            *event_type |= EVENTWR;
            ev->nfds--;
        }

        if(*event_type) {
            /* TODO: Notify the caller about the event */
            break;
        }
    }

    return ev->nfds;
}

static inline int event_free(event_t *ev) {
    /* Nothing to free */
    return 0;
}

#endif

int srv_init(srv_t *ctx) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    /* Set default values for the options */
    ctx->backlog = 1;
    ctx->maxevents = 1000; /* Good enough? */
    ctx->szreadbuf  = 512;
    ctx->szwritebuf = 512;

    /* Initialize handler pointers to 0 */
    ctx->hnd_accept = 0;
    ctx->hnd_read   = 0;
    ctx->hnd_write  = 0;
    ctx->hnd_hup    = 0;
    ctx->hnd_rdhup  = 0;
    ctx->hnd_error  = 0;

    /* By default, only read events are reported for new fds */
    ctx->newfd_event_flags = EVENTRD;

    return 0;
}

/* TODO: WSACleanup on error */
int srv_run(srv_t *ctx, char *hostname, char *port) {
        event_t ev;

        int event_fd, cli_fd, event_type;

        int  cli_port;
        char cli_addr[INET6_ADDRSTRLEN];

        if(!ctx) {
            errno = EINVAL;
            return -1;
        }

#ifdef _WIN32
        WSADATA wsaData;
        if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            /* TODO: Set errno */
            return -1;
        }
#endif

        /* We must have a read handler */
        if(ctx->hnd_read == NULL) {
            errno = EINVAL; /* Invalid argument */
            return -1;
        }

        /* Create a listener socket */
        if((ctx->fdlistener = tcp_create_listener(ctx, hostname, port)) == -1)
            return -1;

        /* The listener must not block */
        if(setnoblock(ctx->fdlistener) == -1)
            return -1;

        /* Initialize the event notification mechanism */
        if(event_init(&ev, ctx->maxevents) == -1)
            return -1;

        /* Request read event notifications for the listener */
        if(event_add_fd(&ev, ctx->fdlistener, EVENTRD) == -1)
            return -1;

        /* Event loop */
        while(1) {
            if(event_wait(&ev, &event_fd, &event_type) == -1) {
                /* TODO: Handle EINTR */
                return -1;
            }

            /* Handle the event */
            if (!event_type) {
                /* No events. We should never get here in the first place */
                continue;
            }
            if (event_type & EVENTERR) {
                /* An error has occured */

                /* Notify the caller */
                if(ctx->hnd_error)
                    (*(ctx->hnd_error))(event_fd, 0); /* TODO: Return the proper error no */

                event_remove_fd(&ev, event_fd);
                close(event_fd);
            }
            if (event_type & EVENTHUP) {
                /* The connection has been shutdown unexpectedly */

                /* Notify the caller */
                if(ctx->hnd_hup)
                    (*(ctx->hnd_hup))(event_fd);

                event_remove_fd(&ev, event_fd);
                close(event_fd);
            }
            if (event_type & EVENTRDHUP) {
                /* The client has closed the connection */

                /* Notify the caller */
                if(ctx->hnd_rdhup)
                    (*(ctx->hnd_rdhup))(event_fd);

                event_remove_fd(&ev, event_fd);
                close(event_fd);
            }
            if(event_type & EVENTRD) {
                if(event_fd == ctx->fdlistener) {
                    /* Incoming connection */
                    while(1) {
                        /* Accept the connection */
                        /* TODO: Notify the caller and request permission to accept, maybe? */
                        cli_fd = tcp_accept(ctx->fdlistener, (char *)&cli_addr,
                                            (int *)&cli_port, SOCK_NONBLOCK);

                        if(cli_fd == -1) {
#ifdef _WIN32
                            if(WSAGetLastError() == WSAEWOULDBLOCK) {
#else
                            if(likely((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
#endif
                                /* We've processed all incoming connections */
                                break;
                            }
                            else {
                                /* accept returned error */
                                if(ctx->hnd_error)
                                    ((*ctx->hnd_error))(cli_fd, SRV_EACCEPT);
                                break;
                            }
                        }

                        /* Add the new fd to the event list */
                        event_add_fd(&ev, cli_fd, ctx->newfd_event_flags); /* TODO: Error handling */

                        /* Accepted connection. Call the on_accept handler */
                        if(ctx->hnd_accept != NULL) {
                            (*(ctx->hnd_accept))(cli_fd, cli_addr, cli_port);
                        }
                    }
                }
                else {
                    /* Data available for read */
                    if((*(ctx->hnd_read))(event_fd)) {
                        /* Handler requested the connection to be closed. */

                        /* Remove the fd from the event list */
                        /* TODO: Removal might be expensive on some event notification
                           mechanisms. Don't remove the fd if keeping it will be less expensive. */
                        event_remove_fd(&ev, event_fd);
                        close(event_fd);
                    }
                }
            }
        }

        /* Close the listener socket */
        if(shutdown(ctx->fdlistener, SHUT_RDWR) == -1)
            return -1;

        if(close(ctx->fdlistener) == -1)
            return -1;

        /* Deinitialize the event mechanism */
        if(event_free(&ev) == -1)
            return -1;

#ifdef _WIN32
        WSACleanup();
#endif
        return 0; /* Terminated succesfully */
}

int srv_hnd_read(srv_t *ctx, int (*h)(int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_read = h;
    return 0;
}

int srv_hnd_write(srv_t *ctx, int (*h)(int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_write = h;
    return 0;
}

int srv_hnd_accept(srv_t *ctx, int (*h)(int, char *, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_accept = h;
    return 0;
}

int srv_hnd_hup(srv_t *ctx, int (*h)(int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_hup = h;
    return 0;
}

int srv_hnd_rdhup(srv_t *ctx, int (*h)(int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_rdhup = h;
    return 0;
}

int srv_hnd_error(srv_t *ctx, int (*h)(int, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_error = h;
    return 0;
}

int srv_set_backlog(srv_t *ctx, int backlog) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->backlog = backlog;
    return 0;
}

int srv_set_maxevents(srv_t *ctx, int n) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->maxevents = n;
    return 0;
}

int srv_notify_read(srv_t *ctx, int fd, int yes) {
    /* TODO: Implementation */
    return 0;
}

int srv_notify_write(srv_t *ctx, int fd, int yes) {
    /* TODO: Implementation */
    return 0;
}

int srv_newfd_notify_read(srv_t *ctx, int yes) {
    /* TODO: Implementation */
    return 0;
}

int srv_newfd_notify_write(srv_t *ctx, int yes) {
    /* TODO: Implementation */
    return 0;
}

int srv_get_listenerfd(srv_t *ctx) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    return ctx->fdlistener;
}
