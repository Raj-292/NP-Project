#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/stat.h>

#define BUFFSIZE 100
#define ERROR_RETURN_VALUE (-1)
#define REFUSAL_MESSAGE "Connection Refused, maximum number of concurrent clients reached, Exiting\n"

#define MAX 512

void writeToSocket(int client_socket);
int handleErrors(int exp, const char* msg);

int main() {
    int client_socket; /* Client Socket file descriptor */

    handleErrors((client_socket = socket(AF_INET, SOCK_STREAM, 0)), "Failed while creating a socket.\n");

    /* Initialize socket addr */
    struct sockaddr_in svrAddr;
    memset(&svrAddr, 0, sizeof(svrAddr));

    svrAddr.sin_family = AF_INET;

    /* Accept Server IP and convert to IPv4 dotted decimal */
    char ipString[15];  // IP in String format
    printf("Enter Server IP Address : ");
    scanf("%s", ipString);
    svrAddr.sin_addr.s_addr = inet_addr(ipString);

    /* Accept Server Port no and convert to network byte order */
    int portInt;  // Port Number (Integer)
    printf("Enter Server Port number : ");
    scanf("%d", &portInt);
    getchar(); /* Consume the stray newline character */
    svrAddr.sin_port = htons(portInt);

    int connectionStatus;
    handleErrors((connectionStatus = connect(client_socket, (struct sockaddr*)&svrAddr, sizeof(svrAddr))), "Connection with the server failed .. \n");

    char connection_buffer[7];
    memset(&connection_buffer, 0, 7);

    read(client_socket, connection_buffer, sizeof(connection_buffer));

    if (strcmp(connection_buffer, "REJECT") == 0) {
        printf(REFUSAL_MESSAGE);
        exit(0);
    } else
        printf("Connected to the server ... \n");

    writeToSocket(client_socket);

    handleErrors(close(client_socket), "Error while closing socket.\n");

    return 0;
}

void writeToSocket(int client_socket) {
    char messageBuffer[MAX];
    char fileName[MAX];

    // Infinite loop for communication between client and server
    while (1) {
        // Reset messageBuffer
        bzero(messageBuffer, sizeof(messageBuffer));

        // Get user input for file name, send input to server
        printf("Enter a filename to retrieved, or type 'quit' to close connection: ");
        scanf("%s", messageBuffer);
        memcpy(fileName, messageBuffer, sizeof(messageBuffer));      // Keep local copy of (potential) filename
        write(client_socket, messageBuffer, sizeof(messageBuffer));  // Write message to server

        // If message sent is "exit" wait for server to respond with exit message.
        if ((strncmp(messageBuffer, "quit", 4)) == 0) {
            // Reset messageBuffer, read returned exit message from server
            bzero(messageBuffer, sizeof(messageBuffer));
            read(client_socket, messageBuffer, sizeof(messageBuffer));  // Read message returned from server

            // If server message is "exit", close connection
            if ((strncmp(messageBuffer, "quit", 4)) == 0) {
                printf("SERVER: Closing connection. \n");
                printf("CLIENT: Exiting...\n");
                break;
            } else {
                printf("CLIENT: ERROR Server did not acknowledge exit. Force closing connection... \n");
                exit(0);
            }
        }

        // Reset messageBuffer, wait for OK message to confirm file existance.
        bzero(messageBuffer, sizeof(messageBuffer));
        read(client_socket, messageBuffer, sizeof(messageBuffer));  // Read message returned from server

        // If server finds file and sends OK message, send an OK back and start receiving the file
        if ((strncmp(messageBuffer, "OK", 2)) == 0) {
            // Send ok message back to client
            write(client_socket, messageBuffer, sizeof(messageBuffer));

            // Receive File from Server
            printf("CLIENT: Receiveing %s from Server and saving it. \n", fileName);

            // Create file in write mode
            FILE* clientFile = fopen(fileName, "w");

            // If the file is null something went wrong, else download file from server
            if (clientFile == NULL) {
                printf("CLIENT: ERROR File %s cannot be opened. \n", fileName);
            } else {
                // Reset Buffer, init block size
                bzero(messageBuffer, MAX);
                int blockSize = 0;

                // While still receiving (downloading) the file
                while ((blockSize = recv(client_socket, messageBuffer, MAX, 0)) > 0) {
                    int fileWrite = fwrite(messageBuffer, sizeof(char), blockSize, clientFile);

                    // If the fileWrite size is less than the block size
                    if (fileWrite < blockSize) {
                        printf("CLIENT: ERROR File write failed.\n");
                    }

                    // Reset buffer
                    bzero(messageBuffer, MAX);

                    // Break out of loop if last block.
                    if (blockSize == 0 || blockSize != 512) {
                        break;
                    }
                }

                // Display success message and close File
                printf("CLIENT: File received from server! \n");

                char permissionsBuffer[MAX];
                struct stat st;

                handleErrors(recv(client_socket, permissionsBuffer, sizeof(permissionsBuffer), 0), "Error while receiving file permissions\n");

                memcpy(&st, permissionsBuffer, sizeof(permissionsBuffer));
                // Set permissions
                if (chmod(fileName, st.st_mode) == -1) {
                    printf("Error while setting permissions\n");
                } else {
                    printf("Set permissions successfully\n");
                }

                fclose(clientFile);
            }
        } else {
            // Else if no OK is received file is not on server.
            printf("SERVER: File %s not found on server, please try another file. \n", fileName);
        }
    }
    // Close Connection
    // close(client_socket);
}

int handleErrors(int exp, const char* msg) {
    if (exp == ERROR_RETURN_VALUE) {
        perror(msg);
        exit(1);
    }
    return exp;
}