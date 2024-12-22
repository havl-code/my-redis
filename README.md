# My Redis-Inspired Program

This project is inspired by the "Build Your Own Redis with C/C++" guide. It aims to break down and implement essential features of an in-memory key-value store.

## Project Goals
- Hands-on learning: Build an understanding of systems programming with C, focusing on socket communication, memory management, data structures, and efficient client-server protocols.
- Feature staging: Develop in incremental stages to gradually introduce network protocols, command handling, and storage techniques

## Current Featues
1. **TCP Server-Client Communication:**
   - The server and client communicate using a structured message protocol, with each message prefixed by a 4-byte length header.
   - The server listens on a specified port, accepts multiple requests per client connection, and returns structured responses.

2. **Error Handling and Debugging:**
   - Expanded error handling to manage socket errors, unexpected disconnections, and message size validations.
   - Helper functions provide consistent logging and error messages for ease of debugging.

3. **Pipelined Request Handling:**
   - Managing multiple client requests in a single connection efficiently.

4. **Non-blocking I/O Operations:**
   - Implementing non-blocking reads and writes to enhance performance.

## Getting Started
### Prerequisites
- **C Compiler**: Required for building the code
- **Linux Environment**: The project is currently developed on Linux for compatibility with networking libraries.

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
- **server.c**: Contains the server code with modular handling of client requests, structured message protocol, and multi-request capability per client connection.
- **client.c**: Implements the client code with multiple requests to the server, modular query handling, and complete data transmission with helper functions.

## Resources
This project is based on concepts and code from the ["Build Your Own Redis with C/C++"](https://build-your-own.org/redis) guide.