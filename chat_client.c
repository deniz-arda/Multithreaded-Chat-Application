#include <stdio.h>
#include "udp.h"
#include "thread_wrappers.h"

#define CLIENT_PORT 10000

typedef struct {
    int sd;
    int exit;
    struct sockaddr_in server_addr;
} ClientState;

// Reads user input and sends to server
void* sender_thread(void *arg) {
    ClientState *state = (ClientState *)arg;
    char input[BUFFER_SIZE];
    
    while (1) {
        fgets(input, BUFFER_SIZE, stdin);
        input[strcspn(input, "\n")] = 0;

        udp_socket_write(state->sd, &state->server_addr, input, BUFFER_SIZE);
        
        // handle disconnecting
        if (strncmp(input, "disconn$", 8) == 0) {
            state->exit = 1;
            break;
        }
    }
    return NULL;
}

// Listens for messages from the server and displays them
void* listener_thread(void *arg) {
    ClientState *state = (ClientState *)arg; 
    char response[BUFFER_SIZE];
    struct sockaddr_in responder_addr;
    
    while (!state->exit) {
        int rc = udp_socket_read(state->sd, &responder_addr, response, BUFFER_SIZE);

        if (rc > 0) {
            printf("%s\n", response);
        }
    }
    return NULL;
}

// client code
int main(int argc, char *argv[])
{
    // Open UDP socket on CLIENT_PORT
    int sd = udp_socket_open(CLIENT_PORT);

    // Set up server address
    struct sockaddr_in server_addr;
    int rc = set_socket_addr(&server_addr, "127.0.0.1", SERVER_PORT);

    // Initialize client state
    ClientState state;
    state.sd = sd;
    state.exit = 0;
    state.server_addr = server_addr;

    // Create sender and listener threads
    pthread_t sender, listener;
    pthread_create_w(&sender, NULL, sender_thread, &state);
    pthread_create_w(&listener, NULL, listener_thread, &state);

    // Wait for threads to finish
    pthread_join_w(sender, NULL);
    pthread_join_w(listener, NULL);

    // Close socket
    close(sd);

    return 0;
}