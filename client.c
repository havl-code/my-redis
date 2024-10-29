// libraries
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

// function to handle errors
static void die(const char *msg) {
    int err = errno;                        // get error code (errno) into err
    fprintf(stderr, "[%d] %s\n", err, msg);  // prints an error message (msg) and error code (err) to standard error (stderr)
    abort();                                // terminate program
}

// client program
int main() {
    // create a TCP socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
        // AF_INET selects IP level protocol (IPv4)
        // SOCK_STREAM specifies TCP protocol
        // 0 is third argument
    if (fd < 0) {
        die("socket()");    // if socket file descriptor (fd) is negative, prints out error and exit
    }

    // define server address
    struct sockaddr_in addr = {};   // initalises address structure for server
    addr.sin_family = AF_INET;      // IPv4 for address family
    addr.sin_port = htons(1234);    // sets port to 1234 after using htons
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // sets IP address to 127.0.0.1 (loopback address) using htonl

    // connect to the server
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr)); // connect to the server using specified address and socket fd
    if (rv) {
        die("connect"); // if connects return non-zero value (rv), prints out error and exit
    }

    // send message to the server
    char msg[] = "hello";       // defines a message
    write(fd, msg, strlen(msg));// sends msg through socket fd, write transmits msg, and strlen(msg) provides message length

    // receive a response from the server
    char rbuf[128] = {};                            // allocates rbuf to store server's response
    ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);   // reads data from fd into rbuf, leave 1 space for null
    if (n < 0){                                     
        die("read");                                // if read returns negative value (n), prints out error and exit
    }

    printf("server says: %s\n", rbuf);   // display server's response

    close(fd);
    return 0;
}
