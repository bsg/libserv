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

#ifndef _LIBSERV_H
#define _LIBSERV_H

#define SRV_EACCEPT   0x1
#define SRV_ESOCKET   0x2
#define SRV_ENOBLOCK  0x3
#define SRV_EEVINIT   0x4
#define SRV_EEVADD    0x5
#define SRV_ECLOSE    0x6
#define SRV_ESHUT     0x7

typedef struct {
    int fdlistener, maxevents, backlog;
    int szreadbuf, szwritebuf;
    unsigned int newfd_event_flags;

    int (*hnd_read)(int);
    int (*hnd_write)(int);
    int (*hnd_accept)(int, char *, int);
    int (*hnd_hup)(int);
    int (*hnd_rdhup)(int);
    int (*hnd_error)(int, int);
} srv_t;

int srv_init(srv_t *);

int srv_run(srv_t *, char *, char *);
int srv_read(int, char *, int);
int srv_write(int, char *, int);
int srv_readall(int, char *, int);
int srv_writeall(int, char *, int);

int srv_connect(char *, char *);
int srv_closeconn(int);

int srv_set_backlog(srv_t *, int);
int srv_set_maxevents(srv_t *, int);

int srv_notify_read(srv_t *, int, int);
int srv_notify_write(srv_t *, int, int);

int srv_newfd_notify_read(srv_t *, int);
int srv_newfd_notify_write(srv_t *, int);

int srv_hnd_read(srv_t *, int (*)(int));
int srv_hnd_write(srv_t *, int (*)(int));
int srv_hnd_accept(srv_t *, int (*)(int, char *, int));
int srv_hnd_hup(srv_t *, int (*)(int));
int srv_hnd_rdhup(srv_t *, int (*)(int));
int srv_hnd_error(srv_t *, int (*)(int, int));

int srv_get_listenerfd(srv_t *);

#endif
