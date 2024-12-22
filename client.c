// libraries
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

// constant definition used to limit the size of data sent and received
#define MAX_MSG_SIZE 4096

// function to handle errors
static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// read_full function
static int32_t read_full(int fd, char *buf, size_t n) {   // reads n bytes from fd into buf
    while (n > 0) {                                     // continues reading until requested n bytes is read
        ssize_t rv = read(fd, buf, n);                  // reads data into buf, if operation fails reutns -1
        if (rv <= 0) {                                  
            return -1;
        }
        assert((size_t)rv <= n);                        // ensures rv does not exceed n
        n -= (size_t)rv;                                // reduces n by number of bytes read
        buf += rv;                                      // advances the buf pointer for next read
    }
    return 0;
}

// write all function
static int32_t write_all(int fd, const char *buf, size_t n) {   // writes n bytes from buf to fd
    while (n > 0) {                                             // loop writes until n bytes are sent
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// query function
static int32_t send_req(int fd, const char *text) { // query sends text to server and receives a response
    uint32_t len = (uint32_t)strlen(text);          // gets the length of text
    if(len > MAX_MSG_SIZE) {                           // returns -1 if text exceeds allowed size
        return -1;
    }

    char wbuf[4 + MAX_MSG_SIZE];           // allocates wbuf with 4 bytes for text len and space for text
    memcpy(wbuf, &len, 4);              // copies len into first 4 bytes of wbuf
    memcpy(&wbuf[4], text, len);        // copies text into wbuf starting after length bytes
    return write_all(fd, wbuf, 4 + len);
}

 static int32_t read_res(int fd) {
    // reading server response header
    char rbuf[4 + MAX_MSG_SIZE + 1];           // buffer for server's response
    errno = 0;                              // resets errno to catch new errors

    int32_t err = read_full(fd, rbuf, 4);   // reads 4 byte-length header from server
    if (err) {                              // if reading fails, prints EOF if errno = 0 or message if it's non-zero
        if (errno == 0) {                           
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }
    
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // reads message length from response header
    if (len > MAX_MSG_SIZE) {  // returns error if message exceedds allowed length
        msg("too long");
        return -1;
    }

    // reading and printing message body
    err = read_full(fd, &rbuf[4], len);     // reads message into rbuf after header
    if (err) {                              // error
        msg("read() error");
        return err;
    }

    rbuf[4 + len] = '\0';                   // adds null terminator after message for safe printing
    printf("server says: %s\n", &rbuf[4]);  // displays server's message
    return 0;
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

    // multiple pipelined requests
    const char *query_list[3] = {"hello1", "hello2", "hello3"}; // define list of queries
    for (size_t i = 0; i < 3; i++) {            // loop through query_list and send each request
        if (send_req(fd, query_list[i]) < 0) {  // if error occurs, program exit
            goto L_DONE;
        }
        // after sending all requests, clients waits for resppnses
        if (read_res(fd) < 0) {                 // if any read fails, program exit
            goto L_DONE;
        }
    }

L_DONE:         // uses goto L_DONE if error occurs, skipping further requests
    close(fd);  // closes connection to server before exiting
    return 0;   
}
