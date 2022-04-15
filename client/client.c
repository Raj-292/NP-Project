#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sys/stat.h>

#define MAX 512
#define ERROR_RETURN_VALUE (-1)
#define REFUSAL_MESSAGE "Connection Refused, maximum number of concurrent clients reached, Exiting\n"

void file_retrieve_attempt(int client_socket);
int handleErrors(int exp, const char* msg);

int main() {
    int client_socket; /* Client Socket file descriptor */

    handleErrors((client_socket = socket(AF_INET, SOCK_STREAM, 0)), "Failed while creating a socket.\n");

    /* Initialize socket addr */
    struct sockaddr_in svrAddr;
    memset(&svrAddr, 0, sizeof(svrAddr));

    svrAddr.sin_family = AF_INET;

    /* Accept Server IP and convert to IPv4 dotted decimal */
    char ipString[15]; /* IP in String format */
    printf("Enter Server IP Address : ");
    scanf("%s", ipString);
    svrAddr.sin_addr.s_addr = inet_addr(ipString);

    /* Accept Server Port no and convert to network byte order */
    int portInt; /* Port Number (Integer) */
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

    file_retrieve_attempt(client_socket);

    /* Close socket at this point */
    handleErrors(close(client_socket), "Error while closing socket.\n");

    return 0;
}

void file_retrieve_attempt(int client_socket) {
    char messageBuffer[MAX];
    char fileName[MAX];

    /* Infinite loop for Client <-> Server communication */
    while (true) {
        /* Set messageBuffer to contain 0s */
        memset(&messageBuffer, 0, sizeof(messageBuffer));

        /* Get user input for file name, send that to server */
        printf("Enter a filename to retrieved, or type 'quit' to close connection: ");
        scanf("%s", messageBuffer);
        memcpy(fileName, messageBuffer, sizeof(messageBuffer));
        write(client_socket, messageBuffer, sizeof(messageBuffer)); /* Write the fileName to the server buffer */

        /* If message sent is "quit" */
        if ((strncmp(messageBuffer, "quit", 4)) == 0) {
            /* Reset messageBuffer, read returned quit message from server */
            memset(&messageBuffer, 0, sizeof(messageBuffer));
            read(client_socket, messageBuffer, sizeof(messageBuffer)); /* Read response that server returned */

            /* If server responds with "quit", close the connection */
            if ((strncmp(messageBuffer, "quit", 4)) == 0) {
                printf("SERVER: Closing connection. \n");
                printf("Exiting...\n");
                break;
            } else {
                printf("ERROR : NO ack from Server. Closing connection without ack... \n");
                exit(0);
            }
        }

        /* Reset messageBuffer, wait for OK (if file exists on server) */
        memset(&messageBuffer, 0, sizeof(messageBuffer));

        read(client_socket, messageBuffer, sizeof(messageBuffer)); /* Read message returned from server */

        /* If file exists on server and server acknowledges with OK message, send an OK back and start receiving the file */
        if ((strncmp(messageBuffer, "OK", 2)) == 0) {
            /* Write OK to the buffer */
            write(client_socket, messageBuffer, sizeof(messageBuffer));

            printf("Receiveing %s from Server and saving it ... \n", fileName);

            /* Create a new file and open in write mode */
            FILE* newFile = fopen(fileName, "w");

            /* If the file is NULL show error*/
            if (newFile == NULL) {
                printf("ERROR : File %s cannot be opened. \n", fileName);
            } else {
                /* Reset Buffer, re-initialize block size */
                memset(&messageBuffer, 0, MAX);
                int blockSize = 0;

                /* While server keeps sending */
                while ((blockSize = recv(client_socket, messageBuffer, MAX, 0)) > 0) {
                    int fileWrite = fwrite(messageBuffer, sizeof(char), blockSize, newFile);

                    /* If the fileWrite size is less than the block size */
                    if (fileWrite < blockSize) {
                        printf("ERROR : File write failed.\n");
                    }

                    /* Reset buffer */
                    memset(&messageBuffer, 0, MAX);

                    /* If last block, break */
                    if (blockSize == 0 || blockSize != 512) {
                        break;
                    }
                }

                /* Receive permissions from server and set the permissions on created file */
                char permissionsBuffer[MAX];
                struct stat st;

                handleErrors(recv(client_socket, permissionsBuffer, sizeof(permissionsBuffer), 0), "Error while receiving file permissions\n");

                memcpy(&st, permissionsBuffer, sizeof(permissionsBuffer));
                /* Set permissions received from server */
                if (chmod(fileName, st.st_mode) == -1) {
                    printf("Error while setting permissions\n");
                } else {
                    printf("Set permissions successfully\n");
                }

                /* Display receipt message and close File */
                printf("File received from server! \n");

                fclose(newFile);
            }
        } else {
            /* No OK received hence file does not exist on server */
            printf("SERVER: File %s not found on server, please try another file. \n", fileName);
        }
    }
}

int handleErrors(int exp, const char* msg) {
    if (exp == ERROR_RETURN_VALUE) {
        perror(msg);
        exit(1);
    }
    return exp;
}