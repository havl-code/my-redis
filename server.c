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
#define MAX_ARGS 200 // Maximum number of strings allowed in one request

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
    if (connfd >= MAX_FD) {   // reject if fd is beyond what fd2conn[] can hold
        msg("too many connections, rejecting");
        close(connfd);
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

// a single parsed argument: a pointer into the request buffer + its length
typedef struct {
    uint32_t len;
    const uint8_t *data;
} Arg;

// parse a request body of the form [nstr][len1][str1][len2][str2]...
// returns 0 on success, -1 if the body is malformed
static int32_t parse_req(const uint8_t *data, size_t len, uint32_t *out_nstr, Arg *out_args, uint32_t max_args) {
    if (len < 4) {
        return -1;
    }
    uint32_t nstr = 0;
    memcpy(&nstr, data, 4);
    if (nstr > max_args) {
        return -1;
    }

    size_t pos = 4;
    for (uint32_t i = 0; i < nstr; i++) {
        if (pos + 4 > len) {
            return -1;
        }
        uint32_t slen = 0;
        memcpy(&slen, &data[pos], 4);
        pos += 4;
        if (pos + slen > len) {   // string would run past the end of the body
            return -1;
        }
        out_args[i].len = slen;
        out_args[i].data = &data[pos];
        pos += slen;
    }
    if (pos != len) {   // trailing bytes that don't belong to any string
        return -1;
    }
    *out_nstr = nstr;
    return 0;
}

// placeholder command handler: echoes back the parsed argument list so we can
// verify the new protocol end-to-end. Real GET/SET/DEL dispatch comes next.
static uint32_t do_request(const Arg *args, uint32_t nstr, uint8_t *out_buf) {
    char tmp[MAX_MSG_SIZE];
    int pos = snprintf(tmp, sizeof(tmp), "parsed %u arg(s):", nstr);
    for (uint32_t i = 0; i < nstr && pos > 0 && (size_t)pos < sizeof(tmp); i++) {
        pos += snprintf(tmp + pos, sizeof(tmp) - (size_t)pos, " '%.*s'", args[i].len, args[i].data);
    }
    size_t rlen = (pos > 0) ? (size_t)pos : 0;
    if (rlen > MAX_MSG_SIZE) {
        rlen = MAX_MSG_SIZE;
    }
    memcpy(out_buf, tmp, rlen);
    return (uint32_t)rlen;
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

    // parse the body into a list of strings
    Arg args[MAX_ARGS];
    uint32_t nstr = 0;
    if (parse_req(&conn->rbuf[4], len, &nstr, args, MAX_ARGS) < 0) {
        msg("bad request");
        conn->state = STATE_END;
        return false;
    }

    printf("client says:");
    for (uint32_t i = 0; i < nstr; i++) {
        printf(" '%.*s'", args[i].len, args[i].data);
    }
    printf("\n");

    uint32_t rlen = do_request(args, nstr, &conn->wbuf[4]);   // build response after the 4-byte header
    memcpy(conn->wbuf, &rlen, 4);
    conn->wbuf_size = 4 + rlen;

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
    struct pollfd poll_fds[MAX_FD + 1];   // +1 for the listening socket

    // acccept and handle client connections
    while (1) {
        // build the poll_fds array fresh each iteration from active connections only
        nfds_t nfds = 0;

        poll_fds[nfds].fd = fd;
        poll_fds[nfds].events = POLLIN;    // monitor server socket for incoming connections
        poll_fds[nfds].revents = 0;
        nfds++;

        for (int i = 0; i < MAX_FD; i++) {
            struct Conn *conn = fd2conn[i];
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {0};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events |= POLLERR;   // always watch for errors
            poll_fds[nfds] = pfd;
            nfds++;
        }

        int rv = poll(poll_fds, nfds, 1000);   // poll only fds that are actually connected

        if (rv < 0) {
            die("poll()");
        }
        if (poll_fds[0].revents & POLLIN) {  // accept new connections if server socket is ready
            accept_new_conn(fd);
        }

        // walk poll_fds (skip index 0, the listening socket) and only service fds that fired
        for (size_t i = 1; i < nfds; i++) {
            uint32_t ready = poll_fds[i].revents;
            if (ready == 0) {
                continue;   // nothing happened on this fd, skip the syscalls entirely
            }

            struct Conn *conn = fd2conn[poll_fds[i].fd];
            if (!conn) {
                continue;
            }

            connection_io(conn);
            if (conn->state == STATE_END) {   // cleanup closed connections
                close(conn->fd);
                free(conn);
                fd2conn[poll_fds[i].fd] = NULL;
            }
        }
    }
    return 0;
}