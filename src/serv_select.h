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

#ifndef _SERV_SELECT_H
#define _SERV_SELECT_H

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

int event_init(event_t *ev, int max_events);
int event_add_fd(event_t *ev, int fd, uint32_t flags);
int event_mod_fd(event_t *ev, int fd, uint32_t flags);
int event_remove_fd(event_t *ev, int fd);
int event_wait(event_t *ev, int *event_fd, int *event_type);
int event_free(event_t *ev);

#endif
#endif