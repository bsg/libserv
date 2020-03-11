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

#ifndef _SERV_H
#define _SERV_H

#if defined libserv_BUILT_AS_STATIC || !defined _WIN32
#define libserv_EXPORT
#else
#ifndef libserv_EXPORT
#ifdef libserv_EXPORTS
#define libserv_EXPORT __declspec(dllexport)
#else
#define libserv_EXPORT __declspec(dllimport)
#endif
#endif
#endif 

#define SRV_EACCEPT   1
#define SRV_ESOCKET   2
#define SRV_ENOBLOCK  4
#define SRV_EEVINIT   8
#define SRV_EEVADD    16
#define SRV_ECLOSE    32
#define SRV_ESHUT     64

#define SRV_EVENTRD   1
#define SRV_EVENTWR   2

typedef struct _srv      srv_t;
typedef struct _srv_conn srv_conn;

struct _srv {
    char *host, *port;
    int fdlistener, maxevents, backlog;
    int szreadbuf, szwritebuf;
    unsigned int newfd_event_flags;

    void (*hnd_read)(srv_conn *);
    void (*hnd_write)(srv_conn *);
    void (*hnd_accept)(srv_conn *);
    void (*hnd_hup)(srv_conn *);
    void (*hnd_rdhup)(srv_conn *);
    void (*hnd_error)(srv_conn *, int);

    /* Pointer to the event_t structure declared in srv_run() */
    void *ev;
};

struct _srv_conn {
    srv_t *ctx;
    int fd;
    char *host;
    int port;

    /* To be used internally for higher-level IO functions */
    void (*hnd_read)(srv_conn *);
    void (*hnd_write)(srv_conn *);
};

#ifdef __cplusplus
extern "C" {
#endif

libserv_EXPORT int srv_init(srv_t *);

libserv_EXPORT int srv_run(srv_t *);
libserv_EXPORT int srv_read(srv_conn *, char *, int);
libserv_EXPORT int srv_write(srv_conn *, char *, int);
libserv_EXPORT int srv_readall(srv_conn *, char *, int);
libserv_EXPORT int srv_writeall(srv_conn *, char *, int);

libserv_EXPORT int srv_connect(char *, char *);
libserv_EXPORT int srv_close(srv_conn *);

libserv_EXPORT void srv_set_host(srv_t *, char *);
libserv_EXPORT void srv_set_port(srv_t *, char *);
libserv_EXPORT int srv_set_backlog(srv_t *, int);
libserv_EXPORT int srv_set_maxevents(srv_t *, int);

libserv_EXPORT int srv_notify_event(srv_conn *, unsigned int);
libserv_EXPORT int srv_newfd_notify_event(srv_t *, unsigned int);

libserv_EXPORT int srv_hnd_read(srv_t *, void (*)(srv_conn *));
libserv_EXPORT int srv_hnd_write(srv_t *, void (*)(srv_conn *));
libserv_EXPORT int srv_hnd_accept(srv_t *, void (*)(srv_conn *));
libserv_EXPORT int srv_hnd_hup(srv_t *, void (*)(srv_conn *));
libserv_EXPORT int srv_hnd_rdhup(srv_t *, void (*)(srv_conn *));
libserv_EXPORT int srv_hnd_error(srv_t *, void (*)(srv_conn *, int));

libserv_EXPORT int srv_get_listenerfd(srv_t *);

#ifdef __cplusplus
}
#endif

#endif
