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

// response type tags, must match server.c's protocol
enum {
    RES_NIL = 0,
    RES_ERR = 1,
    RES_STR = 2,
    RES_INT = 3,
};

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

// query function: sends a command as a list of strings, e.g. {"set", "key1", "hello"}
// wire format (after the outer 4-byte total length): [nstr][len1][str1][len2][str2]...
static int32_t send_req(int fd, const char **cmd, size_t n) {
    uint32_t len = 4;   // 4 bytes to hold nstr itself
    for (size_t i = 0; i < n; i++) {
        len += 4 + (uint32_t)strlen(cmd[i]);   // 4-byte length prefix + the string bytes
    }
    if (len > MAX_MSG_SIZE) {   // returns -1 if the whole body exceeds allowed size
        return -1;
    }

    char wbuf[4 + MAX_MSG_SIZE];    // 4 bytes for outer length + the body
    memcpy(wbuf, &len, 4);          // outer length header

    uint32_t nstr = (uint32_t)n;
    memcpy(&wbuf[4], &nstr, 4);     // number of strings in this request

    size_t pos = 8;                 // position just past outer-len (4) + nstr (4)
    for (size_t i = 0; i < n; i++) {
        uint32_t slen = (uint32_t)strlen(cmd[i]);
        memcpy(&wbuf[pos], &slen, 4);
        pos += 4;
        memcpy(&wbuf[pos], cmd[i], slen);
        pos += slen;
    }
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

    // reading the response body
    err = read_full(fd, &rbuf[4], len);     // reads message into rbuf after header
    if (err) {                              // error
        msg("read() error");
        return err;
    }

    if (len < 1) {
        msg("empty response");
        return -1;
    }

    uint8_t type = (uint8_t)rbuf[4];        // first byte of the body is the type tag
    const char *payload = &rbuf[5];         // everything after the type tag
    size_t plen = len - 1;

    switch (type) {
    case RES_NIL:
        printf("server says: (nil)\n");
        break;
    case RES_ERR: {
        if (plen < 4) {
            msg("malformed error response");
            return -1;
        }
        uint32_t code = 0;
        memcpy(&code, payload, 4);
        printf("server says: (error %u) %.*s\n", code, (int)(plen - 4), payload + 4);
        break;
    }
    case RES_STR:
        printf("server says: %.*s\n", (int)plen, payload);
        break;
    case RES_INT: {
        if (plen < 8) {
            msg("malformed integer response");
            return -1;
        }
        int64_t val = 0;
        memcpy(&val, payload, 8);
        printf("server says: (integer) %lld\n", (long long)val);
        break;
    }
    default:
        msg("unknown response type");
        return -1;
    }
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

    // multiple pipelined requests, each now a list of strings (command + args)
    const char *cmd1[] = {"set", "key1", "hello"};
    const char *cmd2[] = {"get", "key1"};
    const char *cmd3[] = {"del", "key1"};
    const char *cmd4[] = {"get", "key1"};   // should be (nil) now that key1 is deleted
    const char *cmd5[] = {"bogus", "key1"}; // should trigger an error response
    const char *cmd6[] = {"set", "key2", "world"};
    const char *cmd7[] = {"expire", "key2", "1"};  // key2 expires in 1 second
    const char *cmd8[] = {"ttl", "key2"};          // should report ~1 second remaining

    struct {
        const char **cmd;
        size_t n;
    } requests[8] = {
        {cmd1, sizeof(cmd1) / sizeof(cmd1[0])},
        {cmd2, sizeof(cmd2) / sizeof(cmd2[0])},
        {cmd3, sizeof(cmd3) / sizeof(cmd3[0])},
        {cmd4, sizeof(cmd4) / sizeof(cmd4[0])},
        {cmd5, sizeof(cmd5) / sizeof(cmd5[0])},
        {cmd6, sizeof(cmd6) / sizeof(cmd6[0])},
        {cmd7, sizeof(cmd7) / sizeof(cmd7[0])},
        {cmd8, sizeof(cmd8) / sizeof(cmd8[0])},
    };

    for (size_t i = 0; i < 8; i++) {                          // loop through requests and send each one
        if (send_req(fd, requests[i].cmd, requests[i].n) < 0) { // if error occurs, program exit
            goto L_DONE;
        }
        // after sending all requests, clients waits for resppnses
        if (read_res(fd) < 0) {                 // if any read fails, program exit
            goto L_DONE;
        }
    }

    // wait for key2's TTL to pass, then confirm it's gone (lazy expiration on access)
    printf("(sleeping 2s to let key2 expire...)\n");
    sleep(2);
    const char *cmd9[] = {"get", "key2"};   // should now be (nil)
    if (send_req(fd, cmd9, sizeof(cmd9) / sizeof(cmd9[0])) < 0) {
        goto L_DONE;
    }
    if (read_res(fd) < 0) {
        goto L_DONE;
    }

L_DONE:         // uses goto L_DONE if error occurs, skipping further requests
    close(fd);  // closes connection to server before exiting
    return 0;   
}