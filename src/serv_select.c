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

#include "serv_internal.h"
#include "serv_select.h"

#ifdef SELECT
int event_init(event_t *ev, int max_events) {
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

int event_add_fd(event_t *ev, int fd, uint32_t flags) {
    if(fd >= FD_SETSIZE) {
        errno = ENOSPC; /* fd set is full */
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

int event_mod_fd(event_t *ev, int fd, uint32_t flags) {
    if(flags & EVENTRD) {
        /* Add the fd to the read fd_set */
        FD_SET(fd, &(ev->fds_read_master));
    }
    else {
        /* Remove the fd from the read fd_set */
        FD_CLR(fd, &(ev->fds_read_master));
    }

    if(flags & EVENTWR) {
        /* Add the fd to the write fd_set */
        FD_SET(fd, &(ev->fds_write_master));
    }
    else {
        /* Remove the fd from the write fd_set */
        FD_CLR(fd, &(ev->fds_write_master));
    }

    return 0;
}

int event_remove_fd(event_t *ev, int fd) {
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

int event_wait(event_t *ev, int *event_fd, int *event_type) {
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

int event_free(event_t *ev) {
    /* Nothing to free */
    return 0;
}
#endif
