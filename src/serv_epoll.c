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
#include "serv_epoll.h"

#ifdef EPOLL
int event_init(event_t *ev, int max_events) {
    ev->epfd = epoll_create1(0);
    ev->nfds = 0;
    ev->fd_index = 0;
    ev->max_events = max_events;

    ev->events = calloc(max_events, sizeof(struct epoll_event));
    if(ev->events == NULL)
        return -1;

    return ev->epfd;
}

int event_add_fd(event_t *ev, int fd, uint32_t flags) {
        struct epoll_event tmp_event;

        tmp_event.data.fd = fd;
        tmp_event.events = flags;

        return epoll_ctl(ev->epfd, EPOLL_CTL_ADD, fd, &tmp_event);
}

int event_mod_fd(event_t *ev, int fd, uint32_t flags) {
    struct epoll_event tmp_event;
    tmp_event.data.fd = fd;
    tmp_event.events = flags;

    return epoll_ctl(ev->epfd, EPOLL_CTL_MOD, fd, &tmp_event);
}

int event_remove_fd(event_t *ev, int fd) {
    struct epoll_event tmp_event; /* Required for linux versions before 2.6.9 */

    return epoll_ctl(ev->epfd, EPOLL_CTL_DEL, fd, &tmp_event);
}

int event_wait(event_t *ev, int *event_fd, int *event_type) {
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

int event_free(event_t *ev) {
    free(ev->events);

    if(close(ev->epfd) == -1)
        return -1;
}
#endif
