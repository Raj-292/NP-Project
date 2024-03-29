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

#include <sys/stat.h>

#define ERROR_RETURN_VALUE (-1)
#define SERVER_BACKLOG 1

#define MAX 512

#define MAX_CONCURRENT_CLIENTS 5
#define REFUSAL_MESSAGE "Maximum number of concurrent clients reached, closing the connection.\n\n"

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

void* handle_connection(void* p_client_socket);
int handleErrors(int exp, const char* msg);

int current_connected_clients = 0;

int main(int argc, char** argv) {
    int server_socket, client_socket, addr_size;

    printf("Server pid: %d\n\n", getpid()); /* To monitor number of threads using top/htop */

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

    printf("Select an interface from the list above, enter the interface number to select it : ");

    int selected_interface_number;
    scanf("%d", &selected_interface_number);

    char ipString[15];

    if (isdigit(selected_interface_number) == 0 && (selected_interface_number > 0 && selected_interface_number <= interfaces_count)) {
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

    /* Initialize the address struct */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ipString);
    server_addr.sin_port = htons(SERVERPORT);

    handleErrors(bind(server_socket, (SA*)&server_addr, sizeof(server_addr)),
                 "Bind Failed");

    printf("Bind Successful to %s:%d\n\n", inet_ntoa(server_addr.sin_addr), SERVERPORT);

    handleErrors(listen(server_socket, SERVER_BACKLOG), "Listen Failed!");

    while (true) {
        /* wait for, and eventually accept an incoming connection */
        addr_size = sizeof(SA_IN);
        handleErrors(client_socket = accept(server_socket, (SA*)&client_addr, (socklen_t*)&addr_size), "accept failed");

        /* Spawn a new thread whenever a client comes in and execute handle_connection for every thread spawned */
        pthread_t t;
        int* pclient = malloc((sizeof(int)));
        *pclient = client_socket;
        pthread_create(&t, NULL, handle_connection, pclient);
    }

    close(server_socket);
    return 0;
}

/* generic function to handle errors */
int handleErrors(int exp, const char* msg) {
    if (exp == ERROR_RETURN_VALUE) {
        perror(msg);
        exit(1);
    }
    return exp;
}

/* function which triggers on connection of each client */
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
    pthread_t tid = pthread_self();

    printf("[+] Connected with a client, [tid : %ld] , Total number of connected clients:%d\n\n", tid, current_connected_clients);

    /* Infinite loop for Server <-> Client communication */
    while (true) {
        /* Reset messageBuffer with 0s */
        memset(&messageBuffer, 0, MAX);

        /* Read the buffer from client and copy to local buffer. */
        read(client_socket, messageBuffer, sizeof(messageBuffer));

        /* print the thread id serving the client and the file name requested by the client */
        printf("[tid : %ld]: Requesting %s \n", tid, messageBuffer);

        /* Terminate connection if client buffer sends "quit" */
        if ((strncmp(messageBuffer, "quit", 4)) == 0) {
            /* Send quit message backt to client */
            write(client_socket, messageBuffer, sizeof(messageBuffer));
            printf("SERVER: Exiting...\n");
            break;
        }

        /* Attempt to open file named fileName */
        memcpy(fileName, messageBuffer, sizeof(messageBuffer));
        FILE* requestedFile = fopen(fileName, "r"); /* Open file in read mode, store as requestedFile */

        /* If file with name fileName exists */
        if (requestedFile != NULL) {
            /* Write OK */
            write(client_socket, "OK\0", sizeof("OK\0"));

            /* Reset messageBuffer with 0s, wait for client to acknowledge the OK message */
            memset(&messageBuffer, 0, sizeof(messageBuffer));
            read(client_socket, messageBuffer, sizeof(messageBuffer));

            /* If read message is OK send file */
            if ((strncmp(messageBuffer, "OK", 2)) == 0) {
                int blockSize; /* Size of single block */

                /* Reset messageBuffer with 0s */
                memset(&messageBuffer, 0, sizeof(messageBuffer));
                printf("Sending file %s to Client [tid : %ld] \n", fileName, tid);

                /* Continuously send file contents until EOF */
                while ((blockSize = fread(messageBuffer, sizeof(char), MAX, requestedFile)) > 0) {
                    if (send(client_socket, messageBuffer, blockSize, 0) < 0) {
                        printf("ERROR : Failed to send file %s. Closing connection. \n", fileName);
                        exit(0);
                    }

                    /* Reset Message Buffer with 0s */
                    memset(&messageBuffer, 0, MAX);
                }

                /* Reading file permissions */
                struct stat st;
                stat(fileName, &st);

                /* send permissions */
                char permissionsBuffer[MAX];
                memcpy(permissionsBuffer, &st, sizeof(st));
                handleErrors(send(client_socket, permissionsBuffer, sizeof(permissionsBuffer), 0), "Error while sending permissions\n");

                printf("File successfully sent to client [tid : %ld] \n", tid);
            }
        } else {
            /* If file does not exist, print error and send NULL back to client. Wait for next filename from client. */
            printf("ERROR : file %s not found on server. \n", fileName);
            write(client_socket, "NULL\0", sizeof("NULL\0"));
        }
    }

    current_connected_clients--; /* Decrement current connected clients */
    close(*((int*)p_client_socket));
    printf("[-] Client running on thread [tid : %ld] disconnected, Total number of connected clients: %d\n\n", tid, current_connected_clients);
}