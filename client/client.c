#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h> //

#define SERVER_PORT 12345
#define BUFFER_SIZE 1024
int chatbot_active = 0; // Global flag to track chatbot status

void *receive_message_thread(void *sockfd_ptr)
{
    int sockfd = *(int *)sockfd_ptr;
    char buffer[BUFFER_SIZE];
    while (1)
    {
        memset(buffer, 0, sizeof(buffer)); // Clear buffer
        int receive = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (receive > 0)
        {
            printf("\n%s\n\n", buffer);
            if (chatbot_active)
            {
                printf("user>");
            }
            else
            {
                printf(">"); // Normal prompt when chatbot is not active
            }
            fflush(stdout); // Flush stdout to make sure prompt is displayed
        }
        else if (receive == 0)
        {
            printf("Server connection closed.\n");
            break;
        }
        else
        {
            // Error handling, might not want to break the loop
        }
    }
    return NULL;
}

int main()
{
    printf(",________________________________________________________,\n|_________________________menu___________________________|\n");
    printf("|○ /logout - to exit                                     |\n|○ /active - to list active clients                      |\n");
    printf("|○ /send <dest_id> <message> - to send message           |\n|○ /chatbot login- to avail the chatbot feature          |\n");
    printf("|○ /chatbot logout- to disable the chatbot feature       |\n|○ /history <recipient_id>- to retire conversation       |\n");
    printf("|○ /history_delete <recipient_id>- to delete conservation|\n|○ /delete_all- to delete all log record                 |\n|________________________________________________________|\n");

    struct sockaddr_in server_addr;
    int sockfd, ret;
    char buffer[BUFFER_SIZE];

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Connect to localhost

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }
    printf("Connected to the server.\n");

    // Receive and print the welcome message
    // memset(buffer, 0, BUFFER_SIZE);
    // if (recv(sockfd, buffer, BUFFER_SIZE, 0) > 0) {
    //     printf("Server: %s\n", buffer);
    // }

    // Start listening thread
    pthread_t recv_thread;
    if (pthread_create(&recv_thread, NULL, receive_message_thread, (void *)&sockfd) != 0)
    {
        perror("Could not create thread for receiving messages.");
        return EXIT_FAILURE;
    }

    // Communication loop
    while (1)
    {
        if (chatbot_active)
        {
            printf("user>");
        }
        else
        {
            printf(">"); // Normal prompt when chatbot is not active
        }
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline character

        // Check if the user wants to log out from the chatbot
        if (chatbot_active && strcmp(buffer, "/chatbot logout") == 0)
        {
            chatbot_active = 0; // Assume chatbot will be deactivated
        }
        // Check if the user wants to log in the chatbot
        if (chatbot_active == 0 && strcmp(buffer, "/chatbot login") == 0)
        {
            printf("hit login\n");
            chatbot_active = 1; // Assume chatbot will be activated
        }

        // Send message
        if (send(sockfd, buffer, strlen(buffer), 0) < 0)
        {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
        if (strcmp(buffer, "/logout") == 0)
        {
            // Receive farewell message
            memset(buffer, 0, BUFFER_SIZE); // Clear the buffer
            if (recv(sockfd, buffer, BUFFER_SIZE, 0) > 0)
            {
                printf("Server: %s\n", buffer);
            }
            break; // Exit loop
        }
        else if (strcmp(buffer, "/active") == 0)
        {
            // Wait and print the list of active clients
            memset(buffer, 0, BUFFER_SIZE); // Clear the buffer
            if (recv(sockfd, buffer, BUFFER_SIZE, 0) > 0)
            {
                printf("Server: %s\n", buffer);
            }
            // Note: The client does not break out of the loop here,
            // allowing for further interaction after listing active clients.
        }
    }

    // Close the socket and exit
    close(sockfd);
    printf("Disconnected from the server.\n");
    pthread_join(recv_thread, NULL); // Wait for the receive thread to finish

    return 0;
}
