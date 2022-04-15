#include <arpa/inet.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ctype.h>
#include <ifaddrs.h>

#include <errno.h>

#define BUFFSIZE 100
#define ERROR_RETURN_VALUE (-1)
#define SERVER_BACKLOG 1

#define MAX 512

#define MAX_CONCURRENT_CLIENTS 5
#define REFUSAL_MESSAGE "Maximum number of concurrent clients reached, closing the connection.\n\n"

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void* handle_connection(void* p_client_socket);
int handleErrors(int exp, const char* msg);

typedef struct sockaddr SA;

int current_connected_clients = 0;

int main(int argc, char** argv) {
    int server_socket, client_socket, addr_size;

    printf("Server pid: %d\n", getpid()); /* To monitor threads */

    /* List all Avaiable interfaces */

    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in* sa;
    char* addr;

    getifaddrs(&ifap);
    int interfaces_count = 0;
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in*)ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            interfaces_count++;
            printf("%d : ", interfaces_count);
            printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
        }
    }

    freeifaddrs(ifap);

    /* List all Avaiable interfaces */

    printf("Select an interface from the list above, enter the interface number to select it : ");

    int selected_interface_number;
    scanf("%d", &selected_interface_number);

    int validInterfaceSelected = 0;

    // printf("is digit : %d\n", isdigit(selected_interface_number));
    // printf("second cond : %d\n", selected_interface_number > 0 && selected_interface_number <= interfaces_count);

    char ipString[15];

    if (isdigit(selected_interface_number) == 0 && (selected_interface_number > 0 && selected_interface_number <= interfaces_count)) {
        // validInterfaceSelected = 1;
        getifaddrs(&ifap);
        int current_count = 0;
        for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                sa = (struct sockaddr_in*)ifa->ifa_addr;
                addr = inet_ntoa(sa->sin_addr);
                current_count++;
                if (current_count == selected_interface_number) {
                    printf("\nSelected Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
                    strcpy(ipString, addr);
                    break;
                }
            }
        }

        freeifaddrs(ifap);

    } else {
        printf("Invalid interface selected, exiting\n");
        exit(EXIT_FAILURE);
    }

    SA_IN server_addr, client_addr;

    int SERVERPORT;
    printf("Enter a port no to be binded : ");
    scanf("%d", &SERVERPORT);

    handleErrors((server_socket = socket(AF_INET, SOCK_STREAM, 0)), "Failed to creare socket");

    // Initialize the address struct
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ipString);
    server_addr.sin_port = htons(SERVERPORT);

    handleErrors(bind(server_socket, (SA*)&server_addr, sizeof(server_addr)),
                 "Bind Failed");

    printf("Bind Successful to %s:%d\n", inet_ntoa(server_addr.sin_addr), SERVERPORT);

    handleErrors(listen(server_socket, SERVER_BACKLOG), "Listen Failed!");

    while (true) {
        // wait for, and eventually accept an incoming connection
        addr_size = sizeof(SA_IN);
        handleErrors(client_socket = accept(server_socket, (SA*)&client_addr, (socklen_t*)&addr_size), "accept failed");

        // Spawn a new thread whenever a client comes in and execute handle_connection for every thread spawned
        pthread_t t;
        int* pclient = malloc((sizeof(int)));
        *pclient = client_socket;
        pthread_create(&t, NULL, handle_connection, pclient);
    }

    close(server_socket);

    return 0;
}

int handleErrors(int exp, const char* msg) {
    if (exp == ERROR_RETURN_VALUE) {
        perror(msg);
        exit(1);
    }
    return exp;
}

void* handle_connection(void* p_client_socket) {
    char connection_buffer[7]; /* To check if incoming client can send data or not (if max clients limit is reached), will contain ACCEPT/REJECT */

    if (current_connected_clients >= MAX_CONCURRENT_CLIENTS) {
        printf(REFUSAL_MESSAGE);
        memset(&connection_buffer, 0, 7);
        strcpy(connection_buffer, "REJECT");
        write(*((int*)p_client_socket), connection_buffer, sizeof(connection_buffer));
        handleErrors(close(*((int*)p_client_socket)), "close failed\n");
        return NULL;
    } else {
        memset(&connection_buffer, 0, 7);
        strcpy(connection_buffer, "ACCEPT");
        write(*((int*)p_client_socket), connection_buffer, sizeof(connection_buffer));
    }

    current_connected_clients++; /* Increment current connected clients */

    char messageBuffer[MAX];
    char fileName[MAX];

    int client_socket = *((int*)p_client_socket);
    free(p_client_socket);  // No longer needed
    char buffer[BUFFSIZE];
    memset(&buffer, 0, BUFFSIZE);
    int flags = 0;
    size_t bytes_read;
    size_t bytes_write;
    pthread_t tid = pthread_self();

    printf("[+] Connected with a client, [tid : %ld] , Total number of connected clients:%d\n\n", tid, current_connected_clients);

    // Infinite loop for communication between client and
    while (1) {
        // Reallocate buffer with MAX buffer size
        bzero(messageBuffer, MAX);

        // Read the message sent from CLIENT, copy to local buffer.
        read(client_socket, messageBuffer, sizeof(messageBuffer));

        // print buffer which contains the client contents
        printf("CLIENT: Requesting %s \n", messageBuffer);

        // Terminate connection if client requests to exit.
        if ((strncmp(messageBuffer, "exit", 4)) == 0) {
            // Send exit message backt to client
            write(client_socket, messageBuffer, sizeof(messageBuffer));
            printf("SERVER: Exiting...\n");
            break;
        }

        // Attempt to open specified file
        memcpy(fileName, messageBuffer, sizeof(messageBuffer));  // Keep local copy of (potential) filename
        FILE* serverFile = fopen(fileName, "r");                 // Open file in read mode, store as serverFile

        // If the file exists on the server
        if (serverFile != NULL) {
            // Inform client that file is on server via OK message
            write(client_socket, "OK\0", sizeof("OK\0"));

            // Reset messageBuffer, wait for client to acknowledge the OK message
            bzero(messageBuffer, sizeof(messageBuffer));
            read(client_socket, messageBuffer, sizeof(messageBuffer));

            // If read message is OK send file
            if ((strncmp(messageBuffer, "OK", 2)) == 0) {
                int blockSize;  // Size of each block being sent

                // Reset messageBuffer
                bzero(messageBuffer, sizeof(messageBuffer));
                printf("Server: Sending file %s to Client... \n", fileName);

                // Continuously send file contents until EOF
                while ((blockSize = fread(messageBuffer, sizeof(char), MAX, serverFile)) > 0) {
                    if (send(client_socket, messageBuffer, blockSize, 0) < 0) {
                        printf("SERVER: ERROR Failed to send file %s. Closing connection. \n", fileName);
                        exit(0);
                    }

                    // Reset Message Buffer
                    bzero(messageBuffer, MAX);
                }
                printf("SERVER: File successfully sent to client! \n");
            }
        } else {
            // Else if file not on server -- print error, send NULL back to client. Wait for next filename from client.
            printf("SERVER: ERROR file %s not found on server. \n", fileName);
            write(client_socket, "NULL\0", sizeof("NULL\0"));
        }
    }

    /*
    while ((bytes_read = recv(client_socket, buffer, BUFFSIZE - 1, flags)) != 0) {
        if (bytes_read < 0) {
            perror("[x] recv failed\n");
            exit(EXIT_FAILURE);
        }

        if ((strcmp(buffer, "")) != 0) {
            if (strcmp(buffer, "\0") == 0) {
                printf("ye hai zero wala \n");
            }

            if (strcmp(buffer, "\n") == 0) {
                printf("ye hai newline wala \n");
                continue;
            }

            printf("[tid : %ld] --->  %s\n", tid, buffer);
            char fileName[100];
            memset(&fileName, 0, 100);
            strcpy(fileName, buffer);
            // memcpy(fileName, buffer, strlen(buffer));
            // strncpy(fileName, buffer, strlen(buffer));

            int len = strlen(fileName);
            printf("Len : %d\n", len);

            fileName[strlen(fileName) - 1] = '\0';

            if (strcmp(fileName, "") != 0) {
                printf("The client requested for file : %s\n", fileName);
                FILE* filePointer;

                if ((filePointer = fopen(fileName, "r")) != NULL) {
                    printf("File exists!\n");
                } else {
                    // perror("File DNE!\n");
                    fprintf(stderr, "can't open %s: %s\n", fileName, strerror(errno));
                }

                while (true) {
                    unsigned char buff[1024] = {0};
                    printf("Created buff\n");
                    int nread = fread(buff, 1, 1024, filePointer);
                    printf("nread : %d\n", nread);

                    if (nread > 0) {
                        write(client_socket, buff, nread);
                        printf("nread was greater than zero so wrote\n");
                    }

                    if (nread < 1024) {
                        printf("nread was smaller than 1024\n");
                        if (feof(filePointer)) {
                            printf("End of file\n");
                            printf("File transfer completed!");
                        }

                        if (ferror(filePointer))
                            printf("Error Reading!\n");
                        break;
                    }
                }
            }
        }

        memset(&buffer, 0, bytes_read);
    }

    */

    current_connected_clients--; /* Decrement current connected clients */
    close(*((int*)p_client_socket));
    printf("[-] Client running on thread [tid : %ld] disconnected, Total number of connected clients: %d\n\n", tid, current_connected_clients);
}