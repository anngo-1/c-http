#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define SERVER_PORT 8080
#define NUM_PARALLEL_CLIENTS 10
#define NUM_REQUESTS_PER_CLIENT 100
#define BUFFER_SIZE 4096
#define RESPONSE_TIMEOUT_SEC 2

typedef struct {
    int successful_requests;
    int failed_requests;
    double total_time;
} test_stats_t;

static ssize_t read_response(int sockfd, char* buffer, size_t buffer_size) {
    size_t total_read = 0;
    struct timeval timeout;
    timeout.tv_sec = RESPONSE_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt timeout");
        return -1;
    }

    while (total_read < buffer_size - 1) {
        ssize_t bytes = recv(sockfd, buffer + total_read, buffer_size - total_read - 1, 0);
        if (bytes < 0) {
            if (total_read == 0) return -1;
            break;
        }
        if (bytes == 0) break;
        total_read += bytes;

        if (strstr(buffer, "\r\n\r\n")) break;
    }

    buffer[total_read] = '\0';
    return total_read;
}

static int make_request(const char* message, int print_response) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 0;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 0;
    }

    // Send request
    if (send(sockfd, message, strlen(message), 0) < 0) {
        perror("send");
        close(sockfd);
        return 0;
    }

    // Read response
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read_response(sockfd, buffer, sizeof(buffer));
    close(sockfd);

    if (bytes_read <= 0) {
        return 0;
    }

    if (print_response) {
        printf("\nResponse received:\n%s\n", buffer);
    }

    // for HTTP requests, verify response code
    if (strstr(message, "HTTP/1.1")) {
        return strstr(buffer, "HTTP/1.1 200 OK") != NULL;
    }

    // for malformed requests, just verify we got some response
    return bytes_read > 0;
}

void* client_thread(void* arg) {
    test_stats_t* stats = (test_stats_t*)arg;
    const char* request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";

    for (int i = 0; i < NUM_REQUESTS_PER_CLIENT; i++) {
        if (make_request(request, 0)) {
            __atomic_add_fetch(&stats->successful_requests, 1, __ATOMIC_SEQ_CST);
        } else {
            __atomic_add_fetch(&stats->failed_requests, 1, __ATOMIC_SEQ_CST);
        }
    }
    return NULL;
}

void run_test(const char* test_name, const char* request, int print_response) {
    printf("\nRunning %s...\n", test_name);
    if (make_request(request, print_response)) {
        printf("✓ %s passed\n", test_name);
    } else {
        printf("✗ %s failed\n", test_name);
    }
}

int main() {
    printf("Starting server tests...\n");
    printf("Note: Server should be running on port %d\n\n", SERVER_PORT);
    sleep(1); // give server time to start if just launched

    // Test 1: Basic HTTP request
    const char* basic_request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    run_test("Basic HTTP request test", basic_request, 1);

    // Test 2: Malformed request
    const char* malformed_request = "INVALID REQUEST\r\n\r\n";
    run_test("Malformed request test", malformed_request, 1);

    // Test 3: Large request
    printf("\nRunning large request test...\n");
    char* large_request = malloc(100000);
    snprintf(large_request, 100000, 
             "GET / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000\r\n\r\n%*c",
             1000, 'A');
    run_test("Large request test", large_request, 1);
    free(large_request);

    // Test 4: Parallel client test
    printf("\nRunning parallel clients test (%d clients, %d requests each)...\n", 
           NUM_PARALLEL_CLIENTS, NUM_REQUESTS_PER_CLIENT);

    pthread_t threads[NUM_PARALLEL_CLIENTS];
    test_stats_t stats = {0};
    struct timeval start, end;

    gettimeofday(&start, NULL);

    for (int i = 0; i < NUM_PARALLEL_CLIENTS; i++) {
        if (pthread_create(&threads[i], NULL, client_thread, &stats) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < NUM_PARALLEL_CLIENTS; i++) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, NULL);
    stats.total_time = (end.tv_sec - start.tv_sec) + 
                      (end.tv_usec - start.tv_usec) / 1000000.0;

    printf("\nParallel test results:\n");
    printf("Total requests: %d\n", NUM_PARALLEL_CLIENTS * NUM_REQUESTS_PER_CLIENT);
    printf("Successful requests: %d\n", stats.successful_requests);
    printf("Failed requests: %d\n", stats.failed_requests);
    printf("Total time: %.2f seconds\n", stats.total_time);
    printf("Requests per second: %.2f\n", 
           (stats.successful_requests + stats.failed_requests) / stats.total_time);

    return 0;
}