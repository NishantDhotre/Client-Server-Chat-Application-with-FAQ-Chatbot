# Chat Server & Client Application

This project implements a simple chat server and client application using TCP sockets, enabling multiple clients to connect to the server, exchange messages, manage chat history, and interact with a chatbot for FAQs.

## Directory Structure

- `./server`
  - Contains `server.c`, the server-side code.
  - `./chatlog` folder, used to store all chat logs between clients.
  - `FAQs.txt`, a text file containing all FAQ data (questions and answers).
- `./client`
  - Contains `client.c`, the client-side code.

## Dependencies

- GCC compiler for C.
- POSIX-compliant system (Linux, macOS) for server-side.
- C Standard Library.
- `uuid` library for generating UUIDs (server-side).

## Features

- **Multi-client support:** Handles multiple clients simultaneously.
- **Chatbot integration:** Clients can toggle a chatbot on or off, receiving automated responses to FAQs.
- **Chat history management:** Clients can request, delete specific chat histories, or delete all their chat histories.
- **UUID identification:** Each client is identified uniquely using a UUID.

## Compilation Instructions

### Server
navigate to the `./server` directory.
Compile the server application with the following command:

```bash
gcc -o server server.c -pthread -luuid
```

This compiles the `server.c` file into an executable named `server`. The `-luuid` is for UUID generation.

### Client
navigate to the `./client` directory.
Compile the client application with the following command:

```bash
gcc -o client client.c
```

This compiles the `client.c` file into an executable named `client`.

## Running the Application

### Server
To run the server, navigate to the `./server` directory and execute:

```bash
./server
```

The server will start and wait for client connections. Ensure the `./chatlog` directory exists or is created by the server on startup.

### Client

To run a client, navigate to the `./client` directory and execute:

```bash
./client
```

Upon starting, the client will attempt to connect to the server. Follow the prompts to interact with the server and other clients.

## Additional Features/Improvements

- **Chat history files:** Each chat session between clients is logged in the `./chatlog` directory and can be managed through commands.
- **Dynamic FAQ handling:** FAQs can be dynamically loaded from `FAQs.txt`, allowing easy updates to the chatbot's knowledge base.
- **Security considerations:** Implements basic measures for secure chat history management. Further security enhancements are recommended for production use.

## Notes

- Start the server before initiating any client instances.
- This application is developed and tested on a POSIX-compliant system. Compatibility with non-POSIX systems may require modifications.

---
 