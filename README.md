# Multi-threaded HTTP Server

A lightweight HTTP server built in C using epoll and a thread pool architecture.

## Features

- uses epoll for event handling
- thread pool for handling connections
- non-blocking io operations
- handles basic http requests
- includes test suite for parallel clients

## Implementation

- event handling managed through epoll
- worker threads scale with cpu cores (max 32)
- epoll configured in edge-triggered mode
- non-blocking sockets with backlog queue
- handles sigterm/sigint for clean shutdown
- distributes connections round-robin to workers

## Building and running

### server
```bash
make
./server
```
listens on port 8080

### tests
```bash
cd testing
make
./server-test
```

## project structure
```
.
├── server.c          # server implementation
├── Makefile
├── testing/
    ├── test.c       # test suite
    └── Makefile
```

## requirements
- linux
- gcc
- make

