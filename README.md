# Directory

debug.h: Contains utility macros and functions to aid in debugging the application. Includes facilities for logging and debug-level control.

httpserver.c: Implements the HTTP server, handling incoming requests, parsing them, and responding with appropriate resources or error messages.

Makefile: Provides build instructions for the project. Includes targets for compiling, cleaning, and running the application.

queue.h: Defines a thread-safe queue implementation, useful for managing tasks or data between threads.

rwlock.h: Contains the implementation of read-write locks to ensure synchronized access to shared resources across multiple threads.

# Summary 

This project implements a multithreaded HTTP server capable of handling client requests. It includes features like request parsing, thread-safe queuing for efficient task management, and synchronization mechanisms (e.g., read-write locks) to ensure safe concurrent access to shared resources. The server is designed to efficiently serve HTTP responses, making it suitable for use in environments requiring reliable and concurrent request handling.






