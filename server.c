// libraries
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

// helper function to write simple error message
static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

// error handling
static void die(const char *msg){
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

// function do_seomthing, called when new client connection is accepted
static void do_something(int connfd) {
    char rbuf[128] = {};                                 // creates buffer to store client message
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);   // reads client's message connfd into rbuf
    if (n < 0) {                                        // if reading fails (n < 0), prints error and exit
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);                  // displays client's message

    char wbuf[] = "Ha's here!";                              // defines response message
    write(connfd, wbuf, strlen(wbuf));                  // sends response to client through connection
}

// initates server and manages incoming connections
int main() {
    // creates server socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);           // TCP socket for IPv4
    if (fd < 0) {                                       // if fd for the socket is negative, prints out error and exit
        die("socket()");
    }

    // configure socket options
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
        // 2nd and 3rd arguments are options to set
        // 4th argument is the option value
        // last argument is val's length as option value is arbitrary bytes

    // bind address
    struct sockaddr_in addr = {};   // sets up address structure for the server
    addr.sin_family = AF_INET;      // specifies IPv4
    addr.sin_port = htons(1234);    // sets port to 1234
    addr.sin_addr.s_addr = htonl(0);// sets IP to 0.0.0.0

    // bind socket
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));    // bind associates socket fd with addr (server address)
    if (rv) {           // if binding fails, prints out error and exit
        die("bind()");
    }

    // listen for connections
    rv = listen(fd, SOMAXCONN); // prepares fd to accept incoming connections
        //SOMAXCONN sets max number of queued connections
    if (rv){                     // if listen fails, prints out error and exit
        die("listen()");
    }

    // acccept onnections in a loop
    while (true) {                                  // starts infinite loop
        struct sockaddr_in client_addr = {};        // stores client's address info
        socklen_t socklen = sizeof(client_addr);    // defines size of client's address
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen); // accepts incoming connection
        if (connfd < 0) {
            continue;           //if accept failes, loop continues without handling error
        }

        do_something(connfd);   // read client message and send resoonse
        close(connfd);          // closes connection to client

    }
    
    return 0;
}