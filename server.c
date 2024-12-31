#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define MAX_EVENTS 64
#define MAX_WORKERS 32
#define BUFFER_SIZE 4096
#define MAX_CONNECTIONS 1000

typedef struct {
    int epoll_fd;
    int worker_id;
    pthread_t thread;
} worker_t;

static worker_t* workers;
static int num_workers = 0;
static int server_fd;
static volatile bool running = true;

static void* worker_thread(void* arg);
static void setup_socket();
static void handle_connection(int client_fd, int worker_id);
static void signal_handler(int signum);

static void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    running = false;
    close(server_fd);  // This will break the accept loop
}

static int make_socket_non_blocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(sfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    return 0;
}

static void* worker_thread(void* arg) {
    worker_t* worker = (worker_t*)arg;
    struct epoll_event events[MAX_EVENTS];

    printf("Worker %d started\n", worker->worker_id);

    while (running) {
        int n = epoll_wait(worker->epoll_fd, events, MAX_EVENTS, 1000);
        
        if (n == -1) {
            if (errno == EINTR) continue;  // Interrupted system call
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].events & EPOLLIN) {
                handle_connection(events[i].data.fd, worker->worker_id);
            }
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                printf("Worker %d: Client disconnected\n", worker->worker_id);
                epoll_ctl(worker->epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
                close(events[i].data.fd);
            }
        }
    }

    return NULL;
}

static void handle_connection(int client_fd, int worker_id) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Worker %d received: %s", worker_id, buffer);

        // simple HTTP response
        const char* response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/plain\r\n"
                             "Connection: close\r\n"
                             "\r\n"
                             "Hello from worker %d!\n";
        
        char formatted_response[512];
        snprintf(formatted_response, sizeof(formatted_response), response, worker_id);
        write(client_fd, formatted_response, strlen(formatted_response));
        close(client_fd);
    } else if (bytes_read == 0) {
        // closed connection
        printf("Worker %d: Client closed connection\n", worker_id);
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read");
        }
    }
}

// setup main server socket
static void setup_socket() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    if (make_socket_non_blocking(server_fd) == -1) {
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CONNECTIONS) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);
}

int main() {
    // setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    num_workers = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_workers <= 0 || num_workers > MAX_WORKERS) {
        num_workers = 4;  // Reasonable default
    }

    workers = calloc(num_workers, sizeof(worker_t));
    if (!workers) {
        perror("calloc workers");
        exit(EXIT_FAILURE);
    }

    setup_socket();

    for (int i = 0; i < num_workers; i++) {
        workers[i].worker_id = i;
        workers[i].epoll_fd = epoll_create1(0);
        if (workers[i].epoll_fd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }

        if (pthread_create(&workers[i].thread, NULL, worker_thread, &workers[i]) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    printf("Server started with %d workers\n", num_workers);

    int current_worker = 0;
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("accept");
            break;
        }

        // make client socket non-blocking
        if (make_socket_non_blocking(client_fd) == -1) {
            close(client_fd);
            continue;
        }

        struct epoll_event event = {
            .events = EPOLLIN | EPOLLET,  
            .data.fd = client_fd
        };

        if (epoll_ctl(workers[current_worker].epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
            perror("epoll_ctl");
            close(client_fd);
            continue;
        }

        printf("New connection from %s:%d assigned to worker %d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), current_worker);

        current_worker = (current_worker + 1) % num_workers;
    }

    printf("Shutting down server...\n");
    
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i].thread, NULL);
        close(workers[i].epoll_fd);
    }

    free(workers);
    printf("Server shutdown complete\n");
    return 0;
}
