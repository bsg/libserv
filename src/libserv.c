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

inline int read(int fd, char *buffer, int size) {
    return recv(fd, buffer, size, 0);
}

inline int write(int fd, char *buffer, int size) {
    return send(fd, buffer, size, 0);
}
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
#define MAXEVENTS 64

#ifdef __GNUC__
#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#else
#define likely(x) x
#define unlikely(x) x
#endif

#ifdef __GNUC__
#define error(msg)  _error(__func__, __LINE__, msg)
#endif

#ifdef MSVC
#define error(msg) _error(__FUNCTION__, __LINE__, msg)
#endif

static inline void _error(const char *func, int line, const char *msg) {
    printf("[ERROR] Line %d in %s(): %s\n", line, func, msg);
}

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 1
#endif

int setnoblock(int fd) {
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

int tcp_create_listener(char *hostname, char *port) {
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
        perror("getaddrinfo");
        return -1;
    }

    fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(fd == -1) {
        perror("socket");
        return -1;
    }



#ifndef WIN32
    int reuse_addr = 1;
#else
    char reuse_addr = true;
#endif

    /* Make the socket available for reuse immediately after it's closed */
    status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
    if(status == -1) {
        perror("setsockopt");
        return -1;
    }

    /* Bind the socket to the address */
    status = bind(fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(status == -1) {
        perror("bind");
        return -1;
    }

    /* Listen for incoming connections */
    status = listen(fd, 5);
    if(status == -1) {
        perror("listen");
        return -1;
    }

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
        error("getaddrinfo");
    }

    fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(fd == -1) {
        perror("socket");
        return -1;
    }

    status = connect(fd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(status == -1) {
        perror("connect");
        return -1;
    }

    freeaddrinfo(servinfo);

    return fd;
}

int tcp_close(int fd) {
    return close(fd);
}

int tcp_accept(int fd, char *ip, int *port, int flags) {
    int fd_new;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

#ifdef linux
    fd_new = accept4(fd, (struct sockaddr *) &addr, &addrlen, flags);
#else
    /* Fallback to accept if accept4 is not implemented */

    //fd_new = accept(fd, (struct sockaddr *) &addr, &addrlen);
    /* FIXME: The line above fails on WIN32 for some reason. Below is a temporary kludge. */
    fd_new = accept(fd, (struct sockaddr *) &addr, NULL);
#endif

    if(fd_new == -1) {
#ifdef WIN32
        if(WSAGetLastError() != WSAEWOULDBLOCK)
#else
        if((errno != EAGAIN) || (errno != EWOULDBLOCK))
#endif
        {
            error("accept");
        }
        return -1;
    }

    else {
#ifndef linux
        /* accept() doesn't accept flags so we need to set non-blocking mode manually */
        if(flags == SOCK_NONBLOCK)
            setnoblock(fd_new);
#endif
        /* Fill in address info buffers */
        if(ip) inet_ntop(AF_INET, &addr.sin_addr, ip, INET6_ADDRSTRLEN);
        if(port) *port = ntohs(addr.sin_port);

        return fd_new;
    }
}

/* TODO: Inline and profile */
int tcp_read(int fd, char *buf, int size) {
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
    int nwritten, total_written = 0;

    /* Make sure 'size' bytes are written */
    while(total_written != size) {
        nwritten = write(fd, buf, size - total_written);

        switch(nwritten) {
            case  0: return total_written; break;
            case -1: return -1; break;
        }

        total_written += nwritten;
        buf += nwritten;
    }
    return total_written;
}

#ifdef EPOLL
int tcp_server(char *hostname, char *port,
    int (*read_handler)(int),
    void(*on_accept)(int, char *, int *)) {

        struct epoll_event event;
        struct epoll_event *events;

        int listener_fd, fd_epoll, cli_fd, nfds, i, status;
        char *cli_addr;
        int  *cli_port;

        /* We must have a read handler */
        if(read_handler == NULL) {
            error("No read handler");
            return -1;
        }

        /* Create a listener socket */
        listener_fd = tcp_create_listener(hostname, port);
        if(listener_fd == -1) {
            error("Could not create a listener socket");
            goto exit; /* Yeah yeah yeah... I know... */
        }

        /* The listener must not block */
        status = setnoblock(listener_fd);
        if(status == -1) {
            perror("setnoblock");
            goto exit;
        }

        fd_epoll = epoll_create1(0);
        if(fd_epoll == -1) {
            perror("epoll_create1");
            goto exit;
        }

        /* Add the listener to the fd set */
        event.data.fd = listener_fd;
        event.events = EPOLLIN | EPOLLET;
        status = epoll_ctl(fd_epoll, EPOLL_CTL_ADD, listener_fd, &event);
        if(status == -1) {
            perror("epoll_ctl");
            goto exit;
        }

        events = calloc(MAXEVENTS, sizeof(event));
        if(events == NULL) {
            error("calloc");
            goto exit;
        }

        /* Event loop */
        while(1) {
            /* Wait for an event */
            nfds = epoll_wait(fd_epoll, events, MAXEVENTS, -1);
            if(nfds == -1) {
                perror("epoll_wait");
                goto exit;
            }

            /* Handle all events */
            for(i = 0; i < nfds; i++) {
                if ((events[i].events & EPOLLERR)||
                    (events[i].events & EPOLLHUP) ||
                    (!(events[i].events & EPOLLIN))) {

                        close (events[i].data.fd);
                        continue;
                }
                else if(listener_fd == events[i].data.fd) {
                    /* Incoming connection */
                    while(1) {
                        /* Allocate space for address and port info */
                        cli_addr = (char *) calloc(INET6_ADDRSTRLEN, sizeof(char));
                        cli_port = (int  *) calloc(1, sizeof(int));

                        /* Accept the connection */
                        cli_fd = tcp_accept(listener_fd, cli_addr, cli_port, SOCK_NONBLOCK);
                        if(cli_fd == -1) {
                            if(likely((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
                                /* We've processed all incoming connections */
                                break;
                            }
                            else {
                                error("accept");
                                break;
                            }
                        }

                        /* Add the new fd to the event list */
                        event.data.fd = cli_fd;
                        event.events = EPOLLIN | EPOLLET;
                        status = epoll_ctl(fd_epoll, EPOLL_CTL_ADD, cli_fd, &event);
                        if(status == -1) {
                            perror("epoll_ctl");
                            goto exit;
                        }

                        /* Accepted connection. Call the on_accept handler */
                        if(on_accept != NULL) {
                            (*on_accept)(cli_fd, cli_addr, cli_port);
                        }

                        /* Free the resources */
                        free(cli_addr);
                        free(cli_port);
                    }
                    continue;
                }
                else {
                    /* Data available for read */
                    status = (*read_handler)(events[i].data.fd);
                    if(status) {
                        /* Handler requested the connection to be closed. Close the socket. */
                        close(events[i].data.fd);

                        /* Remove the fd from the fd set */
                        struct epoll_event ev_tmp; /* This one is ignored by epoll_ctl. The whole
                                                   purpose of it's existence is compatibility with
                                                   Linux kernel versions before 2.6.9 */

                        /* TODO: epoll_ctl seems to be expensive. Remove and profile */
                        epoll_ctl(fd_epoll, EPOLL_CTL_DEL, events[i].data.fd, &ev_tmp);
                    }
                }
            }
        }

exit:
        /* Free the resources */
        shutdown(listener_fd, SHUT_RDWR);
        close(listener_fd);

        close(fd_epoll);
        free(events);

        return -1;
}

#else

int tcp_server(char *hostname, char *port,
    int (*read_handler)(int),
    void(*on_accept)(int, char *, int *)) {

        int listener_fd, cli_fd, i, status, fdmax;
        char *cli_addr;
        int  *cli_port;

        /* fd sets for select() */
        fd_set fds_master, fds_read;

        /* We must have a read handler */
        if(read_handler == NULL) {
            error("No read handler");
            return -1;
        }

#ifdef WIN32
        WSAData wsaData;

        if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            perror("WSAStartup failed.");
            goto exit;
        }
#endif

        /* Create a listener socket */
        listener_fd = tcp_create_listener(hostname, port);
        if(listener_fd == -1) {
            error("Could not create a listener socket");
            goto exit;
        }

        /* The listener must not block */
        status = setnoblock(listener_fd);
        if(status == -1) {
            error("setnoblock");
            goto exit;
        }

        /* Initialize the fd sets */
        FD_ZERO(&fds_master);
        FD_ZERO(&fds_read);

        /* Add the listener fd to the master set */
        FD_SET(listener_fd, &fds_master);
        fdmax = listener_fd;

        /* Event loop */
        while(1) {
            /* Update fds_read */
            fds_read = fds_master;

            /* Wait for an event */
            status = select(fdmax + 1, &fds_read, NULL, NULL, NULL);
            if(status == -1) {
                perror("select");
                goto exit;
            }

            /* Handle all events */
            for(i = 0; i <= fdmax; i++) {
                if(FD_ISSET(i, &fds_read)) {
                    if(i == listener_fd) {
                        /* Incoming connection */
                        while(1) {
                            /* Allocate space for address and port info */
                            cli_addr = (char *) calloc(INET6_ADDRSTRLEN, sizeof(char));
                            cli_port = (int  *) calloc(1, sizeof(int));

                            /* Accept the connection */
                            cli_fd = tcp_accept(listener_fd, cli_addr, cli_port, SOCK_NONBLOCK);
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
                                    error("accept");
                                    break;
                                }
                            }

                            /* Add the new fd to the fd set */
                            FD_SET(cli_fd, &fds_master);

                            /* Update fdmax */
                            if(cli_fd > fdmax)
                                fdmax = cli_fd;

                            /* Accepted connection. Call the on_accept handler */
                            if(on_accept != NULL) {
                                (*on_accept)(cli_fd, cli_addr, cli_port);
                            }

                            /* Free the resources */
                            free(cli_addr);
                            free(cli_port);
                        }
                        continue;
                    }
                    else {
                        /* Data available for read */
                        status = (*read_handler)(i);
                        if(status) {
                            /* Handler requested the connection to be closed. Close the socket */
                            close(i);

                            /* Remove the fd from the fd set */
                            FD_CLR(i, &fds_master);
                        }
                    }
                }
            }
        }

exit:
        /* Free the resources */
        shutdown(listener_fd, SHUT_RDWR);
        close(listener_fd);
#ifdef WIN32
        WSACleanup();
#endif
        return -1;
}
#endif
