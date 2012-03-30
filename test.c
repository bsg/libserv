#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "libserv.h"

int read_handler(int fd) {
	char buffer[256];
	int nread;

	nread = tcp_read(fd, buffer, 255);
	buffer[nread] = '\0';
	printf("Received: %s", buffer);

	tcp_write(fd, buffer, nread);

	return 1;
}

void on_accept(int fd, char *addr, int *port) {
	printf("Incoming connection from %s:%d\n", addr, *port);
}

int main(void) {
	tcp_server(NULL, "9999", &read_handler, &on_accept);

	return 0;
}
