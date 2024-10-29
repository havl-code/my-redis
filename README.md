# My Redis-Inspired Program

This project is inspired by the "Build Your Own Redis with C/C++" guide. It aims to break down and implement essential features of an in-memory key-value store.

## Project Goals
- Hands-on learning: Build an understanding of systems programming with C, focusing on socket communication, memory management, and data structures
- Feature staging: Develop in incremental stages to gradually introduce network protocols, command handling, and storage techniques

## Current Featues
1. TCP Server-Client Communication:TCP Server-Client Communication:
   - Implemented a basic TCP server and client setup to allow communication between client requests and server responses.
   - The server listens on a specified port, accepts client messages, and returns responses.

2. Error Handling and Debugging:
   - Basic error handling to manage socket errors and unexpected disconnections.
   - Helper functions for consistent logging and debugging. 

## Getting Started
### Prerequisites
- C Compiler: Required for building the code
- Linux Environment: The project is currently developed on Linux for compatibility with networking libraries.

### Cloning the Repository
```bash
git clone https://github.com/havl-code/my-redis.git
cd my-redis
```

### Compilation and Running
1. Compile the code:
```bash
gcc -o server server.c
gcc -o client client.c
```
2. Run the server:
```bash
./server
```
3. Run the client in a new terminal:
```bash
./client
```
The client will send a message to the server and receive a response.

## Project Structure
- server.c: Contains the server code for setting up a TCP socket and handling client connections.
- client.c: Implements the client code to connect to the server, send requests, and handle responses.
- README.md: This documentation file.

## Future Features (In Progress)

## Resources
This project is based on concepts and code from the ["Build Your Own Redis with C/C++"](https://build-your-own.org/redis) guide.