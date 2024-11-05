// libraries
#include <assert.h>
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

const size_t k_max_msg = 4096;

// read exactly n bytes from fd into buf
static int32_t read_full(int fd, char *buf, size_t n){
    while (n > 0) {     // repeats read until ann n bytes are received or an error occurs
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n-= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// write exactly n bytes from buf into fd
static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// handle a single client request
static int32_t one_request(int connfd) {
    // 4-byte header for message length
    char rbuf[4 + k_max_msg + 1];
    errno = 0;

    // read the 4-byte header
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian for length
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // read the message body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // print the client's message
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // prepare a reply
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);          // 4-byte header for reply length
    memcpy(&wbuf[4], reply, len);   // reply body
    return write_all(connfd, wbuf, 4 + len);
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

    // acccept and handle client connections
    while (true) {                                  // starts infinite loop
        struct sockaddr_in client_addr = {};        // stores client's address info
        socklen_t socklen = sizeof(client_addr);    // defines size of client's address
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen); // accepts incoming connection
        if (connfd < 0) {
            continue;           //if accept failes, loop continues without handling error
        }

        // handle multiple requests from a single client
        while (true) {
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
        }
        close(connfd);
    }
    
    return 0;
}