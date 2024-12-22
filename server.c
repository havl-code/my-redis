// libraries
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#define MAX_MSG_SIZE 4096
#define MAX_FD 1024  // Maximum file descriptors for simplicity

// helper function to write simple error message
static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

// error handling
static void die(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// set file descriptor to nonblocking mode
static void fd_set_nb(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);  // get current flags for the file descriptor
    if (flags == -1) {
        die("fcntl F_GETFL");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) { // add the O_NONBLOCK flag
        die("fcntl F_SETFL O_NONBLOCK");
    }
}

// define connection states
enum {
    STATE_REQ = 0,  // waiting for client request (read)
    STATE_RES = 1,  // ready to send response to client (write)
    STATE_END = 2,  // mark connection for closure (client disconnected or error)
};

// store connection data
struct Conn {
    int fd;                         // file descriptor
    uint32_t state;                 // current state (REQ, RES, END)
    size_t rbuf_size;               // size of current data in read buffer
    uint8_t rbuf[4 + MAX_MSG_SIZE]; // read buffer (header + msg)
    size_t wbuf_size;               // size of data in write buffer
    size_t wbuf_sent;               // number of bytes already sent from write buffer
    uint8_t wbuf[4 + MAX_MSG_SIZE]; // write buffer (header + message)
};

// store and retrieve connection data by file descriptor
struct Conn *fd2conn[MAX_FD] = {NULL};

// store a connection object in the global array
static void conn_put(struct Conn *conn) {
    if (conn->fd >= 0 && conn->fd < MAX_FD) {
        fd2conn[conn->fd] = conn;   // add connection to the global array
    }
}

// accept new client connection
static int32_t accept_new_conn(int fd) {
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen); // accept a new connection
    if(connfd < 0) {
        msg("accept() error");
        return -1;
    }
    fd_set_nb(connfd);  // set new connection to nonblocking mode

    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn)); // allocate memory for connection
    if(!conn) {
        close(connfd);
        return -1;
    }

    conn->fd = connfd;
    conn->state = STATE_REQ;    // initialise connection in the read state
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(conn); // store connection in the global array
    return 0;
}

// try to process one request
static bool try_one_request(struct Conn *conn) {
    if (conn->rbuf_size < 4) {  // ensure enough data is available for a message header
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, conn->rbuf, 4);    // extract message length
    if(len > MAX_MSG_SIZE) {        // validate message length
        msg("request too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {    // check if the complete message has been received
        return false;
    }

    printf("client says: %.*s\n", len, &conn->rbuf[4]); // print the message from the client

    memcpy(conn->wbuf, &len, 4);    // copy length to write buffer
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);    // copy message to write buffer
    conn->wbuf_size = 4 + len;

    size_t remain = conn->rbuf_size - 4 - len;  // remove processed data from the read buffer
    if (remain > 0) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;
    conn->state = STATE_RES;    // switch to write state
    return true;
}

// fill read buffer with data
static bool try_fill_buffer(struct Conn *conn) {
    // continuously read data from the client and fill the connection's read buffer
    while (1) {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;              // calculate the available space in the buffer
        ssize_t rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap); // number of bytes read
        
        // nonblocking check
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {  // if no more data is available
            return false;
        }

        // error handling
        if (rv <= 0) {
            msg("read error or EOF");
            conn->state = STATE_END;
            return false;
        }

        // buffer update
        conn->rbuf_size += rv;              // increase rbuf_size by rv
        while (try_one_request(conn)) {}    // process requests from the buffer until no more complete requests remain
    }
    return true;
}

// flush write buffer
static bool try_flush_buffer(struct Conn *conn) {
    // continuously write data from the connection's write buffer to the client
    while (1) {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;  // calculate how much data remains to be written
        ssize_t rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
        
        // nonblocking check
        if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { // if the socket's write buffer is full
            return false;
        }

        // error handling
        if (rv <= 0) {
            msg("Write error");
            conn->state = STATE_END;
            return false;
        }

        // buffer update
        conn->wbuf_sent += rv;
        if (conn->wbuf_sent == conn->wbuf_size) {
            while (try_one_request(conn)) {}
            conn->state = STATE_REQ;    // when fully sent, the state transitions back to STATE_REQ
            
            // buffer counters are reset, preparing for the next request
            conn->wbuf_sent = 0;
            conn->wbuf_size = 0;
            return false;
        }
    }
}

// manages state transitions
static void connection_io(struct Conn *conn) {
    if (conn->state == STATE_REQ) {
        try_fill_buffer(conn);  // fill the read buffer
    } else if (conn->state == STATE_RES) {
        try_flush_buffer(conn); // flush the write buffer
    }
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

    fd_set_nb(fd);
    struct pollfd poll_fds[MAX_FD];

    // acccept and handle client connections
    while (1) {
        poll_fds[0].fd = fd;
        poll_fds[0].events = POLLIN;    // monitor server socket for incoming connections

        int rv = poll(poll_fds, 1, 1000);   // poll for events

        if (rv < 0) {
            die("poll()");
        }
        if(poll_fds[0].revents & POLLIN) {  // accept new connections if server socket is ready
            accept_new_conn(fd);
        }

        for (int i = 0; i < MAX_FD; i++) {
            if(fd2conn[i]) {
                connection_io(fd2conn[i]);
                if (fd2conn[i]->state == STATE_END) {   // cleanup closed connections
                    close(fd2conn[i]->fd);
                    free(fd2conn[i]);
                    fd2conn[i] = NULL;
                }
            }
        }
    }
    return 0;
}