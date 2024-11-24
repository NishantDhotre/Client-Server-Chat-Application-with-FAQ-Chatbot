#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define SHM_NAME "/chatbot_shm"
#define SHM_SIZE 4096
#define DATA_READY '1'
#define DATA_NOT_READY '0'
#define DATA_BEING_WRITTEN '2'
#define DATA_READY_FOR_SERVER '3'

// Client structure
typedef struct
{
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char uuid[37];         // UUID4 string length + 1 for '\0'
    int chatbot_active;    // 1 if chatbot is active, 0 otherwise
    int chatbot_v2_active; // 1 if GPT-2 chatbot is active, 0 otherwise
} client_t;

// client_t *clients[MAX_CLIENTS];
// Global client list
client_t *clients[MAX_CLIENTS] = {NULL};

// FAQ structure
typedef struct
{
    char *question;
    char *answer;
} FAQ;

FAQ *faqs = NULL;   // Global pointer to dynamically allocated FAQ array
int faqs_count = 0; // Count of loaded FAQs
char *filepath = "../FAQs.txt";

// Forward declaration of functions
void process_chatbot_command(client_t *client, char *message);

// Function to get the chatbot response from shared memory
void get_chatbot_response(char *response, size_t response_size, int shm_fd)
{
    // Wait for response to be ready for server to read
    char *shared_mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    while (shared_mem[0] != DATA_READY_FOR_SERVER)
    {
        sleep(1); // Wait for the flag to indicate the response is ready
    }

    // Read the response
    strncpy(response, shared_mem + 1, response_size - 1); // Offset by 1 to skip the status flag
    response[response_size - 1] = '\0';                   // Ensure null-termination

    // Reset the shared memory flag
    shared_mem[0] = DATA_NOT_READY;

    // Unmap the shared memory
    munmap(shared_mem, SHM_SIZE);
}

// Function to initialize shared memory
int init_shared_memory()
{
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(shm_fd, SHM_SIZE) == -1)
    {
        perror("ftruncate");
        close(shm_fd);
        return -1;
    }
    return shm_fd;
}

int shm_fd = -1;
// Start GPT_inference.py subprocess
void start_gpt2_inference_subprocess()
{
    pid_t pid = fork();
    if (pid == 0)
    {
        // Child process: Execute GPT_inference.py
        execlp("python3", "python3", "GPT_inference.py", (char *)NULL);
        // If execlp returns, an error occurred
        perror("execlp");
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        // Fork failed
        perror("fork");
        exit(EXIT_FAILURE);
    }
}

// Function to remove shared memory
void remove_shared_memory()
{
    shm_unlink(SHM_NAME);
}

void delete_all_txt_files(const char *directoryPath)
{
    DIR *dir;
    struct dirent *ent;
    char filePath[1024]; // Adjust as needed for maximum expected path length

    if ((dir = opendir(directoryPath)) != NULL)
    {
        // Iterate through all the files and directories within directory
        while ((ent = readdir(dir)) != NULL)
        {
            // Check if the file is a regular file and has a .txt extension
            if (ent->d_type == DT_REG && strstr(ent->d_name, ".txt") != NULL)
            {
                // Construct the full path for the file
                snprintf(filePath, sizeof(filePath), "%s/%s", directoryPath, ent->d_name);
                // Attempt to delete the file
                if (remove(filePath) == 0)
                {
                    // printf("Deleted file: %s\n", filePath);
                }
                else
                {
                    // perror("Failed to delete file");
                }
            }
        }
        closedir(dir);
    }
    else
    {
        // Could not open directory
        perror("Unable to open directory");
    }
}

// Function to load FAQs from a file
void load_faqs(const char *filepath)
{
    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        perror("Failed to open FAQ file");
        exit(EXIT_FAILURE);
    }

    char line[1024]; // Assuming each line is less than 1024 characters
    while (fgets(line, sizeof(line), file))
    {
        // Remove newline character if present
        line[strcspn(line, "\n")] = 0;

        // Split the line into question and answer
        char *separator = strstr(line, "|||");
        if (!separator)
            continue; // Skip if the separator is not found

        // Allocate memory for a new FAQ entry
        faqs = realloc(faqs, (faqs_count + 1) * sizeof(FAQ));
        if (!faqs)
        {
            perror("Failed to allocate memory for FAQs");
            fclose(file);
            exit(EXIT_FAILURE);
        }

        // Allocate and set question
        int question_len = separator - line - 1;
        faqs[faqs_count].question = (char *)malloc(question_len + 1);
        strncpy(faqs[faqs_count].question, line, question_len);
        faqs[faqs_count].question[question_len] = '\0';

        // Allocate and set answer
        char *answer_start = separator + 4; // Skip over "|||"
        faqs[faqs_count].answer = strdup(answer_start);

        faqs_count++;
    }

    fclose(file);
}

void free_faqs()
{
    for (int i = 0; i < faqs_count; i++)
    {
        free(faqs[i].question);
        free(faqs[i].answer);
    }
    free(faqs);
    faqs_count = 0;
}

void send_chat_history_to_client(client_t *client, const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        char *errorMsg = "No chat history found.\n";
        send(client->sockfd, errorMsg, strlen(errorMsg), 0);
        return;
    }

    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        send(client->sockfd, line, strlen(line), 0);
    }
    fclose(file);
}

// Find client by UUID
client_t *get_client_by_uuid(char *uuid)
{
    for (int i = 0; i < MAX_CLIENTS; ++i)
    {
        if (clients[i] && strcmp(clients[i]->uuid, uuid) == 0)
        {
            return clients[i];
        }
    }
    return NULL; // Not found
}

void log_message(const char *sender_uuid, const char *recipient_uuid, const char *message)
{
    char filename_one[150];
    char filename_two[150];
    snprintf(filename_one, sizeof(filename_one), "./chatlogs/%s-%s.txt", sender_uuid, recipient_uuid);
    snprintf(filename_two, sizeof(filename_two), "./chatlogs/%s-%s.txt", recipient_uuid, sender_uuid);

    FILE *file1 = fopen(filename_one, "a+"); // Append mode
    FILE *file2 = fopen(filename_two, "a+"); // Append mode
    if (file1 && file2)
    {
        fprintf(file1, "%s: %s\n", sender_uuid, message);
        fclose(file1);
        if (strcmp(sender_uuid, recipient_uuid) != 0)
        {
            fprintf(file2, "%s: %s\n", sender_uuid, message);
            fclose(file2);
        }
    }
    else
    {
        perror("Failed to open chat history file");
    }
}

// Generate UUID4 -
void generate_uuid4(char *uuid)
{
    uuid_t binuuid;
    // Generate UUID4
    uuid_generate_random(binuuid);
    uuid_unparse_lower(binuuid, uuid);
}

// Function to add a new client
void add_new_client(int sockfd, struct sockaddr_in addr)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] == NULL)
        {
            clients[i] = (client_t *)malloc(sizeof(client_t));
            clients[i]->address = addr;
            clients[i]->sockfd = sockfd;
            clients[i]->uid = i; // Alternatively, generate a unique ID here
            generate_uuid4(clients[i]->uuid);
            clients[i]->chatbot_active = 0; // Initialize chatbot status
            clients[i] ->chatbot_v2_active = 0;
            char welcome[100];
            printf("New connection: socket %d, UUID %s\n", sockfd, clients[i]->uuid);
            // Send welcome message
            snprintf(welcome, sizeof(welcome), "Server: Welcome to the server, your ID is %s\n", clients[i]->uuid);
            send(sockfd, welcome, strlen(welcome), 0);
            return;
        }
    }
    char welcome[100];
    snprintf(welcome, sizeof(welcome), "Server: Maximum clients reached. Rejecting Your connection.\n");
    send(sockfd, welcome, strlen(welcome), 0);
    printf("Maximum clients reached. Rejecting new connection.\n");
}

// Function to remove a client
void remove_client(int sockfd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->sockfd == sockfd)
        {
            close(clients[i]->sockfd);
            free(clients[i]);
            clients[i] = NULL;
            printf("Client disconnected: socket %d\n", sockfd);
            return;
        }
    }
}

client_t *get_client_by_sockfd(int sockfd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->sockfd == sockfd)
        {
            return clients[i];
        }
    }
    return NULL; // Client not found
}

void delete_chat_history_file(client_t *client, const char *recipient_uuid)
{
    char filename[100];

    snprintf(filename, sizeof(filename), "./chatlogs/%s-%s.txt", client->uuid, recipient_uuid);

    // Attempt to delete the file
    if (remove(filename) == 0)
    {
        char *successMsg = "Chat history successfully deleted.\n";
        send(client->sockfd, successMsg, strlen(successMsg), 0);
    }
    else
    {
        perror("Error deleting chat history file");
        char *errorMsg = "Failed to delete chat history. It may not exist, or an error occurred.\n";
        send(client->sockfd, errorMsg, strlen(errorMsg), 0);
    }
}

void delete_all_chat_histories(client_t *client)
{
    DIR *dir;
    struct dirent *ent;
    char dirPath[] = "./chatlogs/"; // Directory containing chat history files
    char prefix[38];                // Adjust buffer size to 38 to accommodate the null terminator
    snprintf(prefix, sizeof(prefix), "%s-", client->uuid);

    if ((dir = opendir(dirPath)) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            // Check if the file name starts with the client's UUID
            if (strncmp(ent->d_name, prefix, strlen(prefix)) == 0)
            {
                char filePath[4096]; // Use a larger buffer to accommodate longer paths
                snprintf(filePath, sizeof(filePath), "%s%s", dirPath, ent->d_name);

                if (remove(filePath) == 0)
                {
                    printf("Deleted chat history file: %s\n", filePath);
                }
                else
                {
                    perror("Error deleting chat history file");
                }
            }
        }
        closedir(dir);
    }
    else
    {
        // Could not open directory
        perror("Unable to open directory");
    }
}

void process_client_message(int sockfd, char *buffer, fd_set *masterfds)
{
    client_t *client = get_client_by_sockfd(sockfd);
    if (!client)
        return; // Client not found
    if (strcmp(buffer, "/logout") == 0)
    {
        client->chatbot_active = 0;
        client->chatbot_v2_active = 0;
        char *byeMsg = "Server: Goodbye! Have a nice day.\n";
        send(sockfd, byeMsg, strlen(byeMsg), 0); // Send farewell message

        close(sockfd);             // Close the client's socket
        FD_CLR(sockfd, masterfds); // Remove from master fd_set
        remove_client(sockfd);     // Remove client from the list

        return; // Early return to skip further processing
    }
    else if (strcmp(buffer, "/chatbot_v2logout") == 0)
    {
        client->chatbot_v2_active = 0;
        char *logoutMsg = "gpt2bot> Bye! Have a nice day and hope you do not have any complaints about me\n";
        send(sockfd, logoutMsg, strlen(logoutMsg), 0);
    }
    else if (client->chatbot_v2_active)
    {
        char response[SHM_SIZE];

        // Write the client's message to shared memory for the Python script to process
        char *shared_mem = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        shared_mem[0] = DATA_READY; // Set the flag to indicate data is ready to process
        strncpy(shared_mem + 1, buffer, SHM_SIZE - 1);
        munmap(shared_mem, SHM_SIZE);

        // Wait for the Python script to process the message and write the response
        get_chatbot_response(response, sizeof(response), shm_fd);
        
        char combine[sizeof("gpt2bot>> ") + sizeof(response) + 1];
        snprintf(combine, sizeof(combine), "gpt2bot>> %s\n", response);
        send(client->sockfd, combine, strlen(combine), 0);
    }
    else if (strcmp(buffer, "/chatbot_v2login") == 0)
    {
        client->chatbot_v2_active = 1;
        char *loginMsg = "gpt2bot> Hi, I am updated bot, I am able to answer any question be it correct or incorrect\n";
        send(sockfd, loginMsg, strlen(loginMsg), 0);
    }
    else if (strcmp(buffer, "/chatbot logout") == 0)
    {
        // Deactivate chatbot for this client
        client->chatbot_active = 0;
        char *logoutMsg = "stupid bot>Bye! Have a nice day and do not complain about me\n";
        send(sockfd, logoutMsg, strlen(logoutMsg), 0);
    }
    if (client->chatbot_active)
    {
        process_chatbot_command(client, buffer);
    }
    else if (strcmp(buffer, "/chatbot login") == 0)
    {
        // Activate chatbot for this client
        client->chatbot_active = 1;
        char *loginMsg = "stupid bot>Hi, I am stupid bot, I am able to answer a limited set of your questions\n";
        send(sockfd, loginMsg, strlen(loginMsg), 0);
    }
    else if (strcmp(buffer, "/active") == 0)
    {
        // Handle listing active clients
        char activeClientsList[BUFFER_SIZE] = "Active clients:\n";
        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (clients[i])
            {
                strcat(activeClientsList, clients[i]->uuid);
                strcat(activeClientsList, "\n");
            }
        }
        send(sockfd, activeClientsList, strlen(activeClientsList), 0);
    }
    else if (strncmp(buffer, "/send", 5) == 0)
    {
        // Handle sending a message to another client
        char *recipient_uuid = strtok(buffer + 6, " ");
        char *message = strtok(NULL, "");
        if (recipient_uuid && message)
        {
            client_t *recipient = get_client_by_uuid(recipient_uuid);
            if (recipient)
            {
                char formatted_message[BUFFER_SIZE];
                snprintf(formatted_message, sizeof(formatted_message), "Message from %s: %s", client->uuid, message);
                send(recipient->sockfd, formatted_message, strlen(formatted_message), 0);
                log_message(client->uuid, recipient_uuid, message); // Log the message
            }
            else
            {
                char *errorMsg = "Recipient not found. maybe offline!\n";
                send(sockfd, errorMsg, strlen(errorMsg), 0);
            }
        }
    }
    else if (strncmp(buffer, "/history ", 9) == 0)
    {
        char *recipient_uuid = buffer + 9; // Get the recipient UUID part

        // Construct the filename
        char filename[100];

        snprintf(filename, sizeof(filename), "./chatlogs/%s-%s.txt", client->uuid, recipient_uuid);

        // Call the function with the constructed filename
        send_chat_history_to_client(client, filename);
    }
    else if (strncmp(buffer, "/history_delete ", 16) == 0)
    {
        char *recipient_uuid = buffer + 16; // Get the recipient UUID part
        delete_chat_history_file(client, recipient_uuid);
    }
    else if (strcmp(buffer, "/delete_all") == 0)
    {
        delete_all_chat_histories(client);
        char *confirmationMsg = "All your chat histories have been deleted.\n";
        send(sockfd, confirmationMsg, strlen(confirmationMsg), 0);
    }

    // Add more command handling as needed
}

void process_chatbot_command(client_t *client, char *message)
{
    int faq_matched = 0;
    for (int i = 0; i < faqs_count; i++)
    {
        if (strcmp(message, faqs[i].question) == 0)
        {
            // Send the corresponding answer back to the client
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "stupid bot> %s\n", faqs[i].answer);
            send(client->sockfd, response, strlen(response), 0);
            faq_matched = 1;
            break;
        }
    }

    if (!faq_matched)
    {
        // If no matching FAQ is found, send a default response
        char *defaultResponse = "stupid bot> I'm sorry, I don't understand that question.\n";
        send(client->sockfd, defaultResponse, strlen(defaultResponse), 0);
    }
}

int main()
{
    delete_all_txt_files("./chatlogs/");
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    fd_set readfds, masterfds;
    int fdmax;

    shm_fd = init_shared_memory(); // Assign the actual value
    if (shm_fd < 0)
    {
        perror("Failed to initialize shared memory");
        return EXIT_FAILURE;
    }

    load_faqs(filepath);

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(12345);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, 10) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Set listenfd to be non-blocking
    int flags = fcntl(listenfd, F_GETFL);
    fcntl(listenfd, F_SETFL, flags | O_NONBLOCK);

    FD_ZERO(&masterfds);
    FD_SET(listenfd, &masterfds);
    fdmax = listenfd;

    printf("=== WELCOME TO THE SERVER ===\n");
    // Start the GPT-2 inference subprocess
    start_gpt2_inference_subprocess();

    while (1)
    {
        readfds = masterfds;
        if (select(fdmax + 1, &readfds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &readfds))
            {
                if (i == listenfd)
                {
                    struct sockaddr_in cli_addr;
                    socklen_t clilen = sizeof(cli_addr);
                    connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
                    if (connfd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {
                        if (fcntl(connfd, F_SETFL, O_NONBLOCK) == -1)
                        { // Make client socket non-blocking
                            perror("fcntl");
                            close(connfd);
                        }
                        else
                        {
                            FD_SET(connfd, &masterfds); // Add new socket to master set
                            if (connfd > fdmax)
                            {
                                fdmax = connfd;
                            }
                            add_new_client(connfd, cli_addr); // Add client
                        }
                    }
                }
                else
                {
                    // Handle data from clients
                    char buffer[BUFFER_SIZE];
                    memset(buffer, 0, BUFFER_SIZE);
                    int nbytes = recv(i, buffer, BUFFER_SIZE, 0);

                    if (nbytes <= 0)
                    {
                        if (nbytes == 0)
                        {
                            // Connection closed
                            printf("Socket %d hung up\n", i);
                        }
                        else
                        {
                            perror("recv");
                        }
                        close(i);              // Ensure the socket is closed
                        FD_CLR(i, &masterfds); // Remove from master set
                        remove_client(i);      // Cleanup client structure
                    }
                    else
                    {
                        // Process received message
                        process_client_message(i, buffer, &masterfds);
                    };
                }
            }
        }
    }

    close(listenfd);
    return 0;
}
