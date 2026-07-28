# my-redis

[![Tests](https://github.com/havl-code/my-redis/actions/workflows/tests.yml/badge.svg)](https://github.com/havl-code/my-redis/actions/workflows/tests.yml)
[![Language](https://img.shields.io/badge/language-C-blue)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Licence](https://img.shields.io/badge/licence-MIT-green)](LICENSE)

A small in-memory key-value store built from scratch in C, inspired by the ["Build Your Own Redis with C/C++"](https://build-your-own.org/redis) guide. It implements a non-blocking event loop, a structured request/response protocol, a hash table backed store, and TTL support, built up in stages to learn how a tool like Redis actually works under the hood.

```
$ ./server &
$ ./client
server says: OK
server says: hello
server says: (integer) 1
server says: (nil)
server says: (error 1) unknown command
server says: OK
server says: (integer) 1
server says: (integer) 1
(sleeping 2s to let key2 expire...)
server says: (nil)
```

## Why

Libraries like `hiredis` or full builds like Redis itself already solve this properly, but using them doesn't teach much about *how* a key-value store or its wire protocol actually work. This project is the opposite: a small, readable client and server built up feature by feature, so each piece (the event loop, the protocol, the hash table, TTLs) stays easy to follow end to end.

## Design decisions

- **`poll()` rebuilt every iteration, not just watched once.** The server tracks every live connection and rebuilds its `pollfd` array each loop, watching `POLLIN` or `POLLOUT` depending on connection state, so idle connections are not needlessly read from or written to.
- **A structured request protocol, not raw text.** Requests are sent as a length-prefixed list of strings (`[nstr][len1][str1][len2][str2]...`) rather than one opaque blob, so commands like `SET key value` can be parsed properly instead of guessed at.
- **A typed response protocol.** Every response carries a 1-byte type tag (nil, error, string, or integer) so a client can tell the difference between, say, the string `"1"` and the integer `1` meaning "deleted successfully", rather than relying on ambiguous plain text.
- **`SET` clears any existing TTL.** This matches Redis's own behaviour: overwriting a key's value removes any expiry that was previously set on it.
- **Lazy expiration only.** A key is only actually removed once something looks it up again after its TTL has passed. There is no background sweep proactively hunting for expired keys, which is a genuine limitation, not an oversight (see Known limitations).
- **Fixed-size hash table.** A simple chained hash table (FNV-1a hashing, 4096 buckets) backs the store. It does not resize, which keeps the implementation easy to follow but caps how well it scales.

## Requirements

- A C compiler (developed and tested with `gcc`)
- A Linux environment (uses POSIX sockets and `poll()`)

## Setup

```bash
git clone https://github.com/havl-code/my-redis.git
cd my-redis
```

## Compilation and running

```bash
gcc -o server server.c
gcc -o client client.c
```

Run the server in one terminal:
```bash
./server
```

Run the client in a separate terminal:
```bash
./client
```

## Current features

1. **TCP server-client communication.** Messages are prefixed with a 4-byte length header, and the server accepts multiple pipelined requests per connection.
2. **Non-blocking event loop.** Built with `poll()`, only servicing file descriptors that actually have activity rather than looping over every connection unconditionally.
3. **Structured, multi-string request protocol.** Requests are sent as an argv-style list of strings, allowing real commands with arguments rather than a single line of text.
4. **Hash table backed key-value store.** Supports `GET`, `SET`, and `DEL` against an in-memory chained hash table.
5. **TTL support.** `EXPIRE` and `TTL` allow keys to be given a lifespan, with lazy expiry checked on access.
6. **Typed response protocol.** Responses are tagged as nil, error, string, or integer so results are unambiguous.
7. **Error handling.** Malformed requests, oversized messages, and unexpected disconnects are all handled without crashing the server.

## Commands supported

| Command | Example | Returns |
|---|---|---|
| `SET key value` | `SET key1 hello` | string `OK` |
| `GET key` | `GET key1` | string value, or nil if the key does not exist or has expired |
| `DEL key` | `DEL key1` | integer `1` if a key was deleted, `0` if it did not exist |
| `EXPIRE key seconds` | `EXPIRE key1 60` | integer `1` if the TTL was set, `0` if the key does not exist |
| `TTL key` | `TTL key1` | integer seconds remaining, `-1` if the key has no TTL, `-2` if the key does not exist |

Any unrecognised command, or a command called with the wrong number of arguments, returns an error response with a numeric code (`1` for unknown command, `2` for bad arguments).

## Project structure

- **server.c**: the server, including the event loop, request parsing, the hash table, and command dispatch.
- **client.c**: a demo client that pipelines a handful of requests to exercise every command and response type.

## Testing

Pure logic that doesn't need a live socket or root (request parsing, integer parsing, hash table operations, and command dispatch) has unit tests under `tests/`, run automatically on every push via GitHub Actions (see the Tests badge above).

```bash
cd tests
gcc -Wall -Wextra -o test_server_logic test_server_logic.c
./test_server_logic
```

The parts that genuinely need a live TCP connection (`accept_new_conn`, the `poll()` event loop, real client/server interaction) aren't covered by automated tests, since they need two live processes and an actual socket; they're better verified by running the server and client together, as shown in the example session above.

## Known limitations

- **The hash table does not resize.** It is fixed at 4096 buckets, so performance degrades as more keys are added than that was designed for.
- **Expiration is lazy only.** Expired keys are only cleaned up when accessed again, so a key that is never looked up again after expiring will sit in memory indefinitely.
- **Hard limits on size.** Messages are capped at 4096 bytes and the server tracks at most 1024 file descriptors, both for simplicity rather than tuned for production use.
- **No persistence, authentication, or clustering.** Everything lives in memory in a single process and is lost when the server exits.

## Learning objectives

- Socket programming in C, including non-blocking I/O
- Building and maintaining a real `poll()` based event loop
- Designing a structured, length-prefixed binary wire protocol
- Writing a chained hash table from scratch
- Implementing lazy TTL expiration
- Designing a typed response protocol so results are unambiguous to a client
- Unit testing `static` C functions without a build system, by including the source file directly into a test binary

## Licence

Released under the MIT Licence, see [LICENSE](LICENSE).

## Resources

This project is based on concepts and code from the ["Build Your Own Redis with C/C++"](https://build-your-own.org/redis) guide.