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

#ifndef __SERV_INTERNAL_H
#define __SERV_INTERNAL_H

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


#endif
