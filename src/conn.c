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

srv_conn **conns;
int szconns;

void conn_init(int maxfd) {
    szconns = maxfd;
    conns = malloc(maxfd * sizeof(srv_conn *));
}

srv_conn *new_conn(srv_t *ctx, int fd) {
    if(fd >= szconns) {
        return 0;
    }

    srv_conn *conn;
    conn = malloc(sizeof(srv_conn));
    conn->ctx = ctx;
    conn->fd = fd;
    conn->hnd_read = ctx->hnd_read;
    conn->hnd_write = ctx->hnd_write;

    conns[fd] = conn;
    
    return conn;
}

srv_conn *get_conn_by_fd(int fd) {
    if(fd >= szconns) {
        return 0;
    }
    return conns[fd];
}

void remove_conn_by_fd(int fd) {
    if(fd < szconns) {
        free(conns[fd]);
        conns[fd] = 0;
    }
}

