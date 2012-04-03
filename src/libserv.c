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

#include "libserv.h"

#ifdef __linux__
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#endif

#ifdef WIN32
#include <WinSock2.h>
#include <Ws2tcpip.h>

#define close closesocket
#define SHUT_RDWR SD_BOTH

typedef int socklen_t;

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
#endif

#ifdef WIN32
    /* TODO: Check if IOCP exists. Otherwise, fallback to select */
    #define SELECT
#endif

#ifdef EPOLL
#include <sys/epoll.h>
#else
#define FD_SETSIZE FOPEN_MAX
#ifdef __linux__
#include <sys/select.h>
#endif
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

/* TODO: Tweak this number and profile */
/* TODO: Disregard that. Better let the user specify MAXEVENTS */
#define MAXEVENTS 64

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

#define LISTEN_BACKLOG 5 /* TODO: This one should be a variable that the user is
                            allowed to specify */

static int setnoblock(int fd) {
#ifndef WIN32
    int flags;
    flags = fcntl(fd, F_GETFL, 0);
    flags |= O_NONBLOCK;

    return fcntl(fd, F_SETFL, flags);
#else
    u_long argp = 1;
    return ioctlsocket(fd, FIONBIO, &argp);
#endif
}

static int tcp_create_listener(char *hostname, char *port) {
    int status, fd;
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


#ifndef WIN32
    int reuse_addr = 1;
#else
    char reuse_addr = true;
#endif

    /* Make the socket available for reuse immediately after it's closed */
#ifdef WIN32
    /* NOTE: It turns out Win32 definition of SO_REUSEADDR is not the same as the POSIX definition.
       Instead we use SO_EXCLUSIVEADDRUSE */
    status = setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, &reuse_addr, sizeof(reuse_addr));
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
    status = listen(fd, LISTEN_BACKLOG);
    if(status == -1)
        return -1;

    return fd;
}

int tcp_connect(char *hostname, char *port) {
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

int tcp_close(int fd) {
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

/* TODO: Inline and profile */
int tcp_read(int fd, char *buf, int size) {
    /* TODO: Handle EINTR */
    /* TODO: Busy-waiting until the specified size is read is not a good idea,
       considering huge data and/or slow connections. Add a 'noblock' argument
       that will let the function return in case EAGAIN/EWOULDBLOCK is set after
       read() */
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

/* TODO: Inline and profile */
int tcp_write(int fd, char *buf, int size) {
    /* TODO: Handle EINTR */
    /* TODO: Add a 'noblock' argument for the same reasons as in tcp_read() */
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

#define EVENTRD    EPOLLIN
#define EVENTWR    EPOLLOUT
#define EVENTHUP   EPOLLHUP
#define EVENTRDHUP EPOLLRDHUP
#define EVENTERR   EPOLLERR

typedef struct {
    struct epoll_event *events;
    int    epfd, nfds, fd_index, max_events;
} event_t;

static inline int event_init(event_t *event, int max_events) {
    event->epfd = epoll_create1(0);
    event->nfds = 0;
    event->fd_index = 0;
    event->max_events = max_events;

    event->events = calloc(max_events, sizeof(struct epoll_event));
    if(event->events == NULL)
        return -1;

    return event->epfd;        
}

static inline int event_add_fd(event_t *event, int fd, uint32_t flags) {
        struct epoll_event tmp_event;

        tmp_event.data.fd = fd;
        tmp_event.events = flags | EPOLLET; /* Use the edge-triggered mode */

        return epoll_ctl(event->epfd, EPOLL_CTL_ADD, fd, &tmp_event);
}

static inline int event_remove_fd(event_t *event, int fd) {
    struct epoll_event tmp_event; /* Required for linux versions before 2.6.9 */

    return epoll_ctl(event->epfd, EPOLL_CTL_DEL, fd, &tmp_event);
}

static inline int event_wait(event_t *event, int *event_fd, int *event_type) {
    if(event->nfds == 0) {
        /* All events processed so far. Wait for new events */
        event->fd_index = 0;
        event->nfds = epoll_wait(event->epfd, event->events, event->max_events, -1);
    }

    /* Preserve the errno and notify the caller that an error has occured */
    if(event->nfds <= 0) {
        /* Error occured or there are no events waiting to be handled */
        *event_type = 0;
        return event->nfds; /* Return value is -1 on error */
    }

    /* Pass the next event to the caller */
    *event_type = event->events[event->fd_index].events;
    *event_fd   = event->events[event->fd_index].data.fd;

    /* Point to the next event to be handled */
    if(event->fd_index < event->nfds)
        event->fd_index++;
    else {
        event->nfds = 0;
        *event_type = 0;
        return 0; /* All events have been handled */
    }
    
    /* Return the number of events waiting to be handled */
    return (event->nfds - event->fd_index);
}

static inline int event_free(event_t *event) {
    free(event->events);
    
    if(close(event->epfd) == -1)
        return -1;
}
#endif

#ifdef SELECT

#define EVENTRD     1
#define EVENTWR     2
#define EVENTHUP    4
#define EVENTRDHUP  8
#define EVENTERR   16

typedef struct {
    fd_set fds_read_master, fds_read, fds_write_master, fds_write;
    int fdmax, nfds, fd_index;
} event_t;

static inline int event_init(event_t *event, int max_events) {
    /* Initialize the fd sets */
    FD_ZERO(&(event->fds_read_master));
    FD_ZERO(&(event->fds_read));

    FD_ZERO(&(event->fds_write_master));
    FD_ZERO(&(event->fds_write));

    event->fdmax = 0;
    event->nfds  = 0;
    event->fd_index  = 0;

    return 0;
}

static inline int event_add_fd(event_t *event, int fd, uint32_t flags) {
    if(fd >= FD_SETSIZE) {
        errno = EBUSY; /* fd set is full */
        return -1;
    }

    if(flags & EVENTRD) {
        /* Add the fd to the read fd_set */
        FD_SET(fd, &(event->fds_read_master));
    }

    if(flags & EVENTWR) {
        /* Add the fd to the write fd_set */
        FD_SET(fd, &(event->fds_write_master));
    }

    /* Update fdmax */
    if(fd > event->fdmax)
        event->fdmax = fd;

    return 0;
}

static inline int event_remove_fd(event_t *event, int fd) {
    /* Remove from all fd sets */
    FD_CLR(fd, &(event->fds_read_master));
    FD_CLR(fd, &(event->fds_write_master));
    FD_CLR(fd, &(event->fds_read));
    FD_CLR(fd, &(event->fds_write));

    /* Update fdmax */
    if(fd == event->fdmax) {
        event->fdmax--;

        /* The fd that is being handled has just been removed. All the previous
           fds have already been handled */
        if(fd == event->fd_index)
            event->nfds = 0;
    }

    return 0;
}

static inline int event_wait(event_t *event, int *event_fd, int *event_type) {
    if(event->nfds == 0) {
        /* All events processed so far. Wait for new events */
        event->fds_read = event->fds_read_master;
        event->fds_write = event->fds_write_master;
        event->fd_index = 0;

        event->nfds = select(event->fdmax + 1, &(event->fds_read),
                            &(event->fds_write), NULL, NULL);
    }
    
    if(event->nfds == -1) {
        /* An error occured */
        *event_type = EVENTERR;
        event->nfds = 0;

        return -1;
    }
    else if(event->nfds == 0) {
        /* No events waiting to be processed */
        *event_type = 0;
        
        return 0;
    }

    /* Find the next ready fd */
    for(;(event->fd_index <= event->fdmax) && (event->nfds > 0); event->fd_index++) {
        *event_type = 0;
        if(FD_ISSET(event->fd_index, &(event->fds_read))) {
            /* fd ready for read */
            *event_fd   = event->fd_index;
            *event_type |= EVENTRD;
            event->nfds--;
            FD_CLR(event->fd_index, &(event->fds_read));
        }

        if(FD_ISSET(event->fd_index, &(event->fds_write))) {
            /* fd ready for write */
            *event_fd   = event->fd_index;
            *event_type |= EVENTWR;
            event->nfds--;
        }

        if(*event_type) {
            /* TODO: Notify the caller about the event */
            break;
        }
    }

    return event->nfds;
}

static inline int event_free(event_t *event) {
    /* Nothing to free */
    return 0;
} 

#endif


int tcp_server(char *hostname, char *port,
    int (*read_handler)(int),
    void(*on_accept)(int, char *, int *)) {

        event_t event;

        int listener_fd, cli_fd, event_fd, event_type, *cli_port;
        char *cli_addr;

        cli_addr = calloc(INET6_ADDRSTRLEN, sizeof(char));
        cli_port = calloc(1, sizeof(int));

        /* We must have a read handler */
        if(read_handler == NULL) {
            errno = EINVAL; /* Invalid argument */
            return -1;
        }

        /* Create a listener socket */
        if((listener_fd = tcp_create_listener(hostname, port)) == -1)
            return -1;

        /* The listener must not block */
        if(setnoblock(listener_fd) == -1)
            return -1;

        /* Initialize the event notification mechanism */
        if(event_init(&event, MAXEVENTS) == -1)
            return -1;

        /* Request read event notifications for the listener */
        if(event_add_fd(&event, listener_fd, EVENTRD) == -1)
            return -1;

        if(calloc(MAXEVENTS, sizeof(event)) == NULL)
            return -1;

        /* Event loop */
        while(1) {
            if(event_wait(&event, &event_fd, &event_type) == -1) {
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
                /* TODO: Notify the caller */
                event_remove_fd(&event, event_fd);
                close(event_fd);
            }
            if (event_type & EVENTHUP) {
                /* The connection has been shutdown unexpectedly */
                /* TODO: Notify the caller */
                event_remove_fd(&event, event_fd);
                close(event_fd);
            }
            if (event_type & EVENTRDHUP) {
                /* The client has closed the connection */
                /* TODO: Notify the caller */
                event_remove_fd(&event, event_fd);
                close(event_fd);
            }
            if(event_type & EVENTRD) {
                if(event_fd == listener_fd) {
                    /* Incoming connection */
                    while(1) {
                        /* Accept the connection */
                        /* TODO: Notify the caller and request permission
                           to accept, maybe? */
                        cli_fd = tcp_accept(listener_fd, cli_addr,
                                            cli_port, SOCK_NONBLOCK);

                        if(cli_fd == -1) {
#ifdef WIN32
                            if(WSAGetLastError() == WSAEWOULDBLOCK) {
#else
                            if(likely((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
#endif
                                /* We've processed all incoming connections */
                                break;
                            }
                            else {
                                /* accept returned error */
                                /* TODO: Notify the caller */
                                break;
                            }
                        }

                        /* Add the new fd to the event list */
                        event_add_fd(&event, cli_fd, EVENTRD);
                        /* TODO: Let the user specify the events they want to
                           be notified of */

                        /* Accepted connection. Call the on_accept handler */
                        if(on_accept != NULL) {
                            (*on_accept)(cli_fd, cli_addr, cli_port);
                        }
                    }
                }
                else {
                    /* Data available for read */
                    if((*read_handler)(event_fd)) {
                        /* Handler requested the connection to be closed. */

                        /* Remove the fd from the event list */
                        /* TODO: Removal might be expensive on some event
                           notification mechanisms. Don't remove the fd if
                           keeping it will be less expensive. */
                        event_remove_fd(&event, event_fd);
                        close(event_fd);
                    }
                }
            }
        }

        /* Close the listener socket */
        if(shutdown(listener_fd, SHUT_RDWR) == -1)
            return -1;

        if(close(listener_fd) == -1)
            return -1;

        /* Deinitialize the event mechanism */
        if(event_free(&event) == -1)
            return -1;

        return 0; /* Terminated succesfully */
}

