/* A simple, 'single-thread multiple-connection' echo server */

#include <stdlib.h>
#include <stdio.h>
#include "src/libserv.h"

/* Read handler is called whenever a connected socket receives data.
   fd of the socket is passed to the handler function */
int read_handler(int fd) {
	char buffer[256];
	int nread;

    /* Read data from the client */
	nread = tcp_read(fd, buffer, 255);
	buffer[nread] = '\0';

    /* Echo the data back to the client */
	tcp_write(fd, buffer, nread);

    /* Return 1 to close the connection, 0 to keep it alive */
	return 1;
}

/* On accept handler is called whenever a new connection is made to
   the server */
void on_accept(int fd, char *addr, int *port) {
	printf("Incoming connection from %s:%d\n", addr, *port);
}

int main(void) {
    /* Initialize and start the server */
	tcp_server(NULL, "9999", &read_handler, &on_accept);
    /* Yep, that's all */

	return 0;
}
