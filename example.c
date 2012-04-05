/* A simple, 'single-thread multiple-connection' echo server */

/* After compiling and running the server, connect to it using 
 'nc localhost 9999' or 'telnet localhost 9999'. */

#include <stdlib.h>
#include <stdio.h>
#include "src/serv.h"

/* Read handler is called whenever a connected socket receives data.
   fd of the socket is passed to the handler function */
int read_handler(int fd) {
    char buffer[256];
    int nread;

    /* Read data from the client */
    nread = srv_readall(fd, buffer, 255);
    buffer[nread] = '\0';

    /* Echo the data back to the client */
    srv_writeall(fd, buffer, nread);

    /* Return 1 to close the connection, 0 to keep it alive */
    return 1;
}

/* Accept handler is called whenever a new connection is accepted
   by the server */
int accept_handler(int fd, char *addr, int port) {
    printf("Incoming connection from %s:%d\n", addr, port);
    
    return 0;
}

int main(void) {
    srv_t srv;

    /* Initialize the server */
    if(srv_init(&srv) == -1)
        perror("srv_init");

    /* Set read handler */
    srv_hnd_read(&srv, read_handler);
    
    /* Set write handler */
    srv_hnd_accept(&srv, accept_handler);

    /* Initialize and start the server */
    if(srv_run(&srv, NULL, "9999") == -1)
        perror("srv_run");

    /* Yep, that's all */

    return 0;
}
