// Standard library headers for basic utilities, file operations, and error handling
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

// Networking headers for socket programming
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

// Threading and synchronization headers
#include <stdbool.h>
#include <ctype.h>
#include <regex.h>
#include <pthread.h>
#include <semaphore.h>

// Given header files
#include "asgn2_helper_funcs.h"
#include "queue.h"
#include "protocol.h"
#include "rwlock.h"
#include "debug.h"

// Constants
#define BUF_SIZE             500000
#define DEFAULT_THREAD_COUNT 4;
#define OPTIONS              "t:"
#define REQLINE "^([A-Za-z]{1,8}) /([A-Za-z0-9_.-]{2,64}) (HTTP/[0-9]\\.[0-9])\r\n\0$"
#define HEAD    "^([A-Za-z0-9_.-]{1,28}:) ([ -~]{1,128})\r\n$"
#define HEAD2   "^(([A-Za-z0-9_.-]{1,28}:) ([ -~]{1,128})\r\n)*\0$"

// Global variables for synchronization
queue_t *q; 
pthread_mutex_t mutex;
pthread_mutex_t mutex2;
pthread_mutex_t mutex3;
pthread_mutex_t wrt;
sem_t workers;
int readCount;
char **fileLog;
int logId;
int threadCount;

//structure defined for requests, keeps track of stats
typedef struct Request {
    int sock;
    char method[20];
    char uri[100];
    char version[20];
    char hdr[100];
    unsigned int val;
    unsigned int id;
    bool found;
    int threadId;
    bool logLock;
} Request;


void log_error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void get_output(int connn, char *b, size_t len) {
    size_t num_written = 0;
    write_n_bytes(connn, b + num_written, len);
}

//Process PUT request
void put_req(Request *req, char *bufm, int conny, int signal) {
    //Lock mutex
    pthread_mutex_lock(&mutex);
    pthread_mutex_lock(&wrt);
    
    // Prepares buffer to store the response message
    char response[150] = { 0 };
    //Check if the HTTP version is not 1.1
    if ((strcmp(req->version, "HTTP/1.1") != 0) || signal == 1) {
        // Writes a bad request response
        sprintf(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: %d\r\n\r\nBad Request\n",
            req->val);

        //Log the requests as a 400 error
        if (req->found == false) {
            fprintf(stderr, "GET,/%s,400,%d\n", req->uri, 0);
        } else {
            fprintf(stderr, "PUT,/%s,400,%d\n", req->uri, req->id);
        }

        get_output(conny, response, strlen(response));
        //Unlock the mutexes and return since request is invalid
        pthread_mutex_unlock(&wrt);
        pthread_mutex_unlock(&mutex);
        return;
    }

    // open requested URI file with write-only access
    int fd = open(req->uri, O_WRONLY | O_TRUNC, 0666);
    if (fd == -1) {
        //if th doesn't exist, create it
        fd = open(req->uri, O_WRONLY | O_TRUNC | O_CREAT, 0666);
        if (fd == -1) {
            // if the file still can't be made, print an error message
            printf("creating error");
        }
        sprintf(response, "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n");
        //logs request which indicates if it's new
        if (req->found == false) {
            fprintf(stderr, "GET,/%s,201,%d\n", req->uri, 0);
        } else {
            fprintf(stderr, "PUT,/%s,201,%d\n", req->uri, req->id);
        }
        //send 201 response to client
        get_output(conny, response, strlen(response));
    } else {
        sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n");
        if (req->found == false) {
            fprintf(stderr, "GET,/%s,200,%d\n", req->uri, 0);
        } else {
            fprintf(stderr, "PUT,/%s,200,%d\n", req->uri, req->id);
        }
        //send 200 response to client
        get_output(conny, response, strlen(response));
    }

    // initalize variable to track number of bytes written to file
    size_t num_written = 0;
    write_n_bytes(fd, bufm + num_written, (size_t) req->val);

    pthread_mutex_unlock(&mutex);
    pthread_mutex_unlock(&wrt);
    close(fd);
    return;
}

void get_req(Request *req, int conny, int signal) {
    //determines if logging should be locked based on URI and thread ID
    if (readCount >= 1) {
        for (int i = 0; i < logId; i++) {
            if ((req->uri != fileLog[i]) && (req->threadId != i))
                req->logLock = true;
            else {
                req->logLock = false;
                break;
            }
        }
    } else
        req->logLock = false;

    // Lock the mutex for reading
    if (req->logLock == false)
        pthread_mutex_lock(&mutex);
    pthread_mutex_lock(&mutex2);
    readCount++;
    if (readCount == 1) {
        pthread_mutex_lock(&wrt);
    }
    pthread_mutex_unlock(&mutex2);

    // Prepare and send the response based on request validation
    char response[100] = { 0 };
    if ((strcmp(req->version, "HTTP/1.1") != 0) || signal == 1) {
        sprintf(response, "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion "
                          "Not Supported\n");
        if (req->found == false) {
            fprintf(stderr, "GET,/%s,505,%d\n", req->uri, 0);
        } else {
            fprintf(stderr, "GET,/%s,505,%d\n", req->uri, req->id);
        }
        get_output(conny, response, strlen(response));
        pthread_mutex_lock(&mutex2);
        readCount--;
        if (readCount == 0) {
            pthread_mutex_unlock(&wrt);
        }
        pthread_mutex_unlock(&mutex2);
        pthread_mutex_unlock(&mutex);
        return;
    }

    if (access(req->uri, X_OK) == 0) {
        sprintf(response, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n");
        if (req->found == false) {
            fprintf(stderr, "GET,/%s,403,%d\n", req->uri, 0);
        } else {
            fprintf(stderr, "GET,/%s,403,%d\n", req->uri, req->id);
        }
        get_output(conny, response, strlen(response));
        pthread_mutex_lock(&mutex2);
        readCount--;
        if (readCount == 0) {
            pthread_mutex_unlock(&wrt);
        }
        pthread_mutex_unlock(&mutex2);
        pthread_mutex_unlock(&mutex);
        return;
    }

    // Attempts to open the file for reading
    int fd = open(req->uri, O_RDONLY, 0666);
    if (fd == -1) {
        sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n");
        if (req->found == false)
            fprintf(stderr, "GET,/%s,404,%d\n", req->uri, 0);
        else
            fprintf(stderr, "GET,/%s,404,%d\n", req->uri, req->id);
        get_output(conny, response, strlen(response));
        close(fd);
        pthread_mutex_lock(&mutex2);
        readCount--;
        if (readCount == 0) {
            pthread_mutex_unlock(&wrt);
        }
        pthread_mutex_unlock(&mutex2);
        pthread_mutex_unlock(&mutex);
        return;
    }

    char buf[BUF_SIZE] = { 0 };
    size_t free_space = sizeof(buf);
    size_t num_read = 0;

    struct stat finfo;
    fstat(fd, &finfo);
    off_t fileSize = finfo.st_size;

    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", (size_t) fileSize);
    if (req->found == false) {
        fprintf(stderr, "GET,/%s,200,%d\n", req->uri, 0);
    } else {
        fprintf(stderr, "GET,/%s,200,%d\n", req->uri, req->id);
    }
    get_output(conny, response, strlen(response));

    size_t bytez = 0;

    for (;;) {
        bytez = read(fd, buf + num_read, free_space);
        if (bytez < 0)
            printf("bytez error");
        if (bytez == 0)
            break;
        num_read += bytez;
        free_space -= bytez;
        if (free_space == 0) {
            get_output(conny, buf + 0, num_read);
            free_space = sizeof(buf);
            num_read = 0;
        } else {
            get_output(conny, buf + 0, num_read);
        }
    }

    close(fd);
    pthread_mutex_lock(&mutex2);
    readCount--;
    if (readCount == 0) {
        pthread_mutex_unlock(&wrt);
    }
    pthread_mutex_unlock(&mutex2);
    pthread_mutex_unlock(&mutex);
    return;
}

//Process client requests by matching against predefined regex patterns
void process_req(Request *req, char *bufa, int con) {
    pthread_mutex_lock(&mutex3); // Locking to ensure thread-safe access to shared resources
    regex_t preg = { 0 };
    regmatch_t p[4] = { 0 };
    char response[100];

    // Compile regex pattern for request line validation. On failure, respond with 400 bad request
    if ((regcomp(&preg, REQLINE, REG_EXTENDED)) != 0) {
        sprintf(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        get_output(con, response, strlen(response));
        pthread_mutex_unlock(&mutex3);
        return;
    }

    //If regex doesn't match, respond with 400 bad request
    if ((regexec(&preg, bufa, (size_t) 4, p, 0)) != 0) {
        sprintf(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        get_output(con, response, strlen(response));
        pthread_mutex_unlock(&mutex3);
        return;
    } else {
        memcpy(req->method, &bufa[p[1].rm_so], p[1].rm_eo);
        req->method[p[1].rm_eo] = '\0';

        int d = p[2].rm_eo - p[2].rm_so;
        memcpy(req->uri, &bufa[p[2].rm_so], d);
        req->uri[d] = '\0';

        fileLog[logId] = req->uri;
        req->threadId = logId;
        logId++;

        int k = p[3].rm_eo - p[3].rm_so;
        memcpy(req->version, &bufa[p[3].rm_so], k);
        req->version[k] = '\0';
    }

    bufa += p[0].rm_eo;
    if (bufa[3] == '\0') {
        req->found = false;
        if (strcmp(req->method, "GET") == 0) {
            get_req(req, con, 0);
            pthread_mutex_unlock(&mutex3);
            return;
        } else if (strcmp(req->method, "PUT") == 0) {
            put_req(req, bufa, con, 1);
            pthread_mutex_unlock(&mutex3);
            return;
        } else {
            get_output(con, response, strlen(response));
            pthread_mutex_unlock(&mutex3);
            return;
        }
    }

    regex_t preg2 = { 0 };
    regmatch_t p2[4] = { 0 };
    if ((regcomp(&preg2, HEAD2, REG_EXTENDED)) != 0)
        log_error("regcomp error");
    if ((regexec(&preg2, bufa, (size_t) 4, p2, 0)) != 0) {
        get_req(req, con, 0);
    }

    memcpy(req->hdr, &bufa[p2[0].rm_so], p2[0].rm_eo);

    req->hdr[p2[0].rm_eo] = '\0';
    req->val = atol(&bufa[p2[3].rm_so]);
    req->id = atol(&bufa[12]);

    bufa += p2[0].rm_eo;
    if (bufa[3] == '\0') {
        req->found = true;
        if (strcmp(req->method, "GET") == 0) {
            get_req(req, con, 0);
            pthread_mutex_unlock(&mutex3);
            return;
        } else if (strcmp(req->method, "PUT") == 0) {
            put_req(req, bufa, con, 1);
            pthread_mutex_unlock(&mutex3);
            return;
        } else {
            pthread_mutex_unlock(&mutex3);
            return;
        }
    } else {
        req->found = true;
        if (strcmp(req->method, "GET") == 0) {
            get_req(req, con, 1);
            pthread_mutex_unlock(&mutex3);
            return;
        } else if (strcmp(req->method, "PUT") == 0) {
            bufa += 2;
            put_req(req, bufa, con, 0);
            pthread_mutex_unlock(&mutex3);
            return;
        } else {
            pthread_mutex_unlock(&mutex3);
            return;
        }
    }
}

void handle_connection(int connfd) {
    char buffer[BUF_SIZE] = { '\0' };
    ssize_t bytes = 0;
    Request req = { 0 };

    // handles incoming connections data and processing requests
    while ((bytes = read_until(connfd, buffer, BUF_SIZE, NULL)) > 0) {
        process_req(&req, buffer, connfd);
    }

    // if no data is received or an error occurs, send a 400 bad request response
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        char response[70];
        sprintf(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n");
        get_output(connfd, response, strlen(response));
    }

    (void) connfd;
    close(connfd);
    return;
}

static void usage(char *exec) {
    fprintf(stderr, "usage: %s [-t threads] <port>\n", exec);
}

void *worker_thread() {
    //pops connections from queue to handle
    for (;;) {
        uintptr_t connfd3 = -1;
        queue_pop(q, (void **) &connfd3);
        handle_connection(connfd3);
        close(connfd3);
    }
    return NULL;
}

//processes command line arguements and initalizes server resources
int main(int argc, char *argv[]) {
    int opt = 0;
    int threads = DEFAULT_THREAD_COUNT;

    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't':
            threads = strtol(optarg, NULL, 10);
            break;
            return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        warnx("wrong number of args");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    int64_t port = atol(argv[optind]);
    // validate port number and initialize signal handling
    if (port == 0) {
        errx(EXIT_FAILURE, "bad port number: %s", argv[1]);
    }

    signal(SIGPIPE, SIG_IGN);

    q = queue_new(threads);

    threadCount = threads;

    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&mutex2, NULL);
    pthread_mutex_init(&mutex3, NULL);
    pthread_mutex_init(&wrt, NULL);
    sem_init(&workers, 0, 0);

    // allocates memory for global variables and create worker threads
    fileLog = malloc(100 * sizeof(char *));
    logId = 0;
    for (int i = 0; i < threads; i++) {
        fileLog[i] = (char *) malloc(64 + 1);
    }

    pthread_t thread_pool[threads];
    for (int i = 0; i < threads; i++) {
        pthread_create(&(thread_pool[i]), NULL, worker_thread, NULL);
    }

    Listener_Socket listenfd;

    if ((listener_init(&listenfd, port)) == -1)
        errx(EXIT_FAILURE, "not listening");

    errno = 0;
    //accepts connections and pushes them to queue
    while (1) {
        int connfd = listener_accept(&listenfd);
        if (connfd < 0) {
            warn("accept error");
            continue;
        }
        int64_t con = connfd;
        queue_push(q, (void *) (intptr_t) con);
    }
    free(fileLog);
    free(q);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mutex2);
    pthread_mutex_destroy(&mutex3);
    pthread_mutex_destroy(&wrt);
    sem_destroy(&workers);
    return EXIT_SUCCESS;
}
