#include <stdio.h>
#include <stdlib.h>
#include "udp.h"
#include "thread_wrappers.h"

// Linked list structure to store information on clients
typedef struct ClientNode {
    struct sockaddr_in addr;
    char name[BUFFER_SIZE];
    struct ClientNode *next; 
} ClientNode;

ClientNode *client_list = NULL;
pthread_mutex_t client_list_mutex; 

void add_client(struct sockaddr_in addr, char *name) {
    ClientNode *new_client = (ClientNode *)malloc(sizeof(ClientNode));
    new_client->addr = addr;
    strcpy(new_client->name, name);
    new_client->next = NULL;

    pthread_mutex_lock_w(&client_list_mutex);

    if (client_list == NULL) {
        client_list = new_client;
    }
    else {
        ClientNode *current = client_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_client;
    }
    pthread_mutex_unlock_w(&client_list_mutex);
}

void* listener_thread(void *arg) {
    // will implement listener thread
}

int main(int argc, char *argv[])
{

    // This function opens a UDP socket,
    // binding it to all IP interfaces of this machine,
    // and port number SERVER_PORT
    // (See details of the function in udp.h)
    int sd = udp_socket_open(SERVER_PORT);

    assert(sd > -1);

    // Server main loop
    while (1) 
    {
        // Storage for request and response messages
        char client_request[BUFFER_SIZE], server_response[BUFFER_SIZE];

        // Demo code (remove later)
        printf("Server is listening on port %d\n", SERVER_PORT);

        // Variable to store incoming client's IP address and port
        struct sockaddr_in client_address;
    
        // This function reads incoming client request from
        // the socket at sd.
        // (See details of the function in udp.h)
        int rc = udp_socket_read(sd, &client_address, client_request, BUFFER_SIZE);

        // Successfully received an incoming request
        if (rc > 0)
        {
            // Demo code (remove later)
            strcpy(server_response, "Hi, the server has received: ");
            strcat(server_response, client_request);
            strcat(server_response, "\n");

            // This function writes back to the incoming client,
            // whose address is now available in client_address, 
            // through the socket at sd.
            // (See details of the function in udp.h)
            rc = udp_socket_write(sd, &client_address, server_response, BUFFER_SIZE);

            // Demo code (remove later)
            printf("Request served...\n");
        }
    }

    return 0;
}