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

#include "serv.h"
#include "serv_internal.h"
#include "serv_tcp.h"
#include "serv_select.h"
#include "serv_epoll.h"

#ifdef __cplusplus
extern "C" {
#endif

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

int srv_close(srv_t *ctx, int fd) {
    event_remove_fd(ctx->ev, fd);
    return close(fd);
}

int srv_read(int fd, char *buf, int size) {
    /* TODO: WSAGetLastError */
    return read(fd, buf, size);
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
    /* TODO: WSAGetLastError */
    return write(fd, buf, size);
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

int srv_init(srv_t *ctx) {
#ifdef _WIN32
    WSADATA wsaData;
#endif

    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

#ifdef _WIN32
    if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        /* TODO: Set errno */
        return -1;
    }
#endif

    /* Set default values for the options */
    ctx->host = NULL;
    ctx->port = NULL;
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
int srv_run(srv_t *ctx) {
    event_t ev;

    int event_fd, cli_fd, event_type;

    int  cli_port;
    char cli_addr[INET6_ADDRSTRLEN];
       

    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    /* Pointer to th event_t structure. Needed for srv_notify_event()
       and srv_newfd_notify_event() */
    ctx->ev = (void *) &ev;

    /* Port must be specified and we must have a read handler */
    if(ctx->port == NULL || ctx->hnd_read == NULL) {
        errno = EINVAL; /* Invalid argument */
        return -1;
    }

    /* Create a listener socket */
    if((ctx->fdlistener = srv_tcp_create_listener(ctx)) == -1)
        return -1;

    /* The listener must not block */
    if(srv_setnoblock(ctx->fdlistener) == -1)
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
        if(!event_type) {
            /* No events. We should never get here in the first place */
            continue;
        }

        if(event_type & EVENTERR) {
            /* An error has occured */

            /* Notify the caller */
            if(ctx->hnd_error) {
                (*(ctx->hnd_error))(ctx, event_fd, 0); /* TODO: Return the proper error no */
            }

            event_remove_fd(&ev, event_fd);
            close(event_fd);
        }

        if(event_type & EVENTHUP) {
            /* The connection has been shutdown unexpectedly */

            /* Notify the caller */
            if(ctx->hnd_hup) {
                (*(ctx->hnd_hup))(ctx, event_fd);
            }

            event_remove_fd(&ev, event_fd);
            close(event_fd);
        }

        if (event_type & EVENTRDHUP) {
            /* The client has closed the connection */

            /* Notify the caller */
            if(ctx->hnd_rdhup) {
                (*(ctx->hnd_rdhup))(ctx, event_fd);
            }

            event_remove_fd(&ev, event_fd);
            close(event_fd);
        }

        if(event_type & EVENTRD) {
            if(event_fd == ctx->fdlistener) {
                /* Incoming connection */
                while(1) {
                    /* Accept the connection */
                    cli_fd = srv_tcp_accept(ctx->fdlistener, (char *)&cli_addr,
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
                            if(ctx->hnd_error) {
                                ((*ctx->hnd_error))(ctx, cli_fd, SRV_EACCEPT);
                            }
                            break;
                        }
                    }

                    /* Add the new fd to the event list */
                    event_add_fd(&ev, cli_fd, ctx->newfd_event_flags); /* TODO: Error handling */

                    /* Accepted connection. Call the accept handler */
                    if(ctx->hnd_accept) {
                        (*(ctx->hnd_accept))(ctx, cli_fd, cli_addr, cli_port);
                    }
                }
            }
            else {
                /* Data available for read */
                (*(ctx->hnd_read))(ctx, event_fd);
            }
        }
    }

    if(event_type & EVENTWR) {
        /* Socket ready for write */
        if(ctx->hnd_write) {
            (*(ctx->hnd_write))(ctx, event_fd);
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

int srv_hnd_read(srv_t *ctx, void (*h)(srv_t *, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_read = h;
    return 0;
}

int srv_hnd_write(srv_t *ctx, void (*h)(srv_t *, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_write = h;
    return 0;
}

int srv_hnd_accept(srv_t *ctx, void (*h)(srv_t *, int, char *, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_accept = h;
    return 0;
}

int srv_hnd_hup(srv_t *ctx, void (*h)(srv_t *, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_hup = h;
    return 0;
}

int srv_hnd_rdhup(srv_t *ctx, void (*h)(srv_t *, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_rdhup = h;
    return 0;
}

int srv_hnd_error(srv_t *ctx, void (*h)(srv_t *, int, int)) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    ctx->hnd_error = h;
    return 0;
}

void srv_set_host(srv_t *ctx, char *host) {
    ctx->host = host;
}

void srv_set_port(srv_t *ctx, char *port) {
    ctx->port = port;
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

int srv_register_fd(srv_t *ctx, int fd, unsigned int flags) {
    uint32_t f;

    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    f = 0;
    if(flags & SRV_EVENTRD)
        f |= EVENTRD;
    if(flags & SRV_EVENTWR)
        f |= EVENTWR;

    return event_add_fd((event_t *) ctx->ev, fd, f);
}

int srv_notify_event(srv_t *ctx, int fd, unsigned int flags) {
    uint32_t f;
    
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    f = 0;
    if(flags & SRV_EVENTRD)
        f |= EVENTRD;
    if(flags & SRV_EVENTWR)
        f |= EVENTWR;

    return event_mod_fd((event_t *) ctx->ev, fd, f);
}

int srv_newfd_notify_event(srv_t *ctx, unsigned int flags) {
    uint32_t f;
    
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    f = 0;
    if(flags & SRV_EVENTRD)
        f |= EVENTRD;
    if(flags & SRV_EVENTWR)
        f |= EVENTWR;

    ctx->newfd_event_flags = f;
    return 0;
}

int srv_get_listenerfd(srv_t *ctx) {
    if(!ctx) {
        errno = EINVAL;
        return -1;
    }

    return ctx->fdlistener;
}

#ifdef __cplusplus
}
#endif
