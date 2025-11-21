#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "udp.h"
#include "thread_wrappers.h"

// Linked list structure to store information on clients
typedef struct ClientNode {
    struct sockaddr_in addr; // IP and port information
    char name[BUFFER_SIZE];
    struct ClientNode *next;
    struct sockaddr_in *muted_addresses; // addresses of the muted clients
    int muted_count; // number of clients it has muted
} ClientNode;

typedef struct {
    int sd;
    ClientNode *client_list_head;
    pthread_rwlock_t client_list_lock;
} ServerState;

typedef struct {
    ServerState *server_state; // pointer to server state
    ClientNode *requesting_client; // pointer to node of the client requesting
    struct sockaddr_in client_addr; // address of the client giving the request
    char request[BUFFER_SIZE]; // request from the client
} RequestInfo;

void add_client(ClientNode **head, const char *name, struct sockaddr_in addr) {
    // allocate memory for new node
    ClientNode *new_node = (ClientNode *)malloc(sizeof(ClientNode));
    strcpy(new_node->name, name);
    new_node->addr = addr;
    new_node->next = *head;
    *head = new_node;
}

void remove_client(ClientNode **head, struct sockaddr_in addr) {
    ClientNode *current = *head;
    ClientNode *previous = NULL;

    while (current != NULL) {
        // Compare IP and port
        if (memcmp(&current->addr, &addr, sizeof(struct sockaddr_in)) == 0) {
            if (previous == NULL) {
                *head = current->next;
            }
            else {
                previous->next = current->next;
            }
            free(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}

ClientNode* find_by_address(ClientNode *head, struct sockaddr_in addr) {
    ClientNode *current = head;
    
    while (current != NULL) {
        // Check if IP and port match
        if (memcmp(&current->addr, &addr, sizeof(struct sockaddr_in)) == 0) {
            return current; 
        }
        current = current->next;
    }

    return NULL;
}

ClientNode* find_by_name(ClientNode *head, const char *name) {
    ClientNode *current = head;
    
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

void handle_connect(RequestInfo *args) {
    // parse for name (conn$ name)
    char *name = args->request + 6;
    
    // add new client to linked list
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);
    add_client(&args->server_state->client_list_head, name, args->client_addr);
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);

    // send response to client
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "Hi, %s you have successfully connected to the chat", name);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
}

void handle_disconnect(RequestInfo *args) {
    char response[BUFFER_SIZE] = "Disconnected. Bye!";
    
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
    remove_client(&args->server_state->client_list_head, args->client_addr);
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
}

void handle_say(RequestInfo *args) {
    char *message = args->request + 5;

    if (args->requesting_client == NULL) {
        // Client not connected, don't broadcast
        char error_msg[BUFFER_SIZE] = "Error: Client is not connected to the server";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    pthread_rwlock_rdlock_w(&args->server_state->client_list_lock);
    
    char broadcast[BUFFER_SIZE];
    snprintf(broadcast, BUFFER_SIZE, "%s: %s", args->requesting_client->name, message);
    
    // broadcast to everyone on the list
    ClientNode *current = args->server_state->client_list_head;

    while (current != NULL) {
        udp_socket_write(args->server_state->sd, &current->addr, broadcast, BUFFER_SIZE);
        current = current->next;
    }

    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
}

void handle_rename(RequestInfo *args) {
    char *new_name = args->request + 8;

    if (args->requesting_client == NULL) {
        char error_msg[BUFFER_SIZE] = "Error: Client does not exist";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);
    strcpy(args->requesting_client->name, new_name);
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);

    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "You are now known as %s", new_name);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
}

void handle_sayto(RequestInfo *args) {
    char *unparsed_message = args->request + 7;
    char recipient_name[BUFFER_SIZE]; // person receiving the message
    char *pos = strchr(unparsed_message, ' '); // finding the space in the message to parse it, (eg. sayto$ Bob Hello)

    int name_length = pos - unparsed_message; // get the name of the person receiving the message 
    strncpy(recipient_name, unparsed_message, name_length);

    char *message = pos + 1; // get the message

    if (args->requesting_client == NULL) {
        return;
    }
    pthread_rwlock_rdlock_w(&args->server_state->client_list_lock);

    ClientNode *recipient = find_by_name(args->server_state->client_list_head, recipient_name);
    
    if (recipient == NULL) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);

        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error: could not find user %s", recipient_name);
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
    }
    char private_msg[BUFFER_SIZE];
    snprintf(private_msg, BUFFER_SIZE, "%s: %s", args->requesting_client->name, message);
    udp_socket_write(args->server_state->sd, &recipient->addr, private_msg, BUFFER_SIZE);
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
}

void handle_mute(RequestInfo *args) {
    // implement
}

void handle_unmute(RequestInfo *args) {
    // implement
}

void* request_handler_thread(void *arg) {
    RequestInfo *args = (RequestInfo *)arg;

    if (strncmp(args->request, "conn$", 5) == 0) {
        handle_connect(args);
    }
    else if (strncmp(args->request, "disconn$", 8) == 0) {
        handle_disconnect(args);
    }
    else if (strncmp(args->request, "say$", 4) == 0) {
        handle_say(args);
    }
    else if (strncmp(args->request, "rename$", 7) == 0) {
        handle_rename(args);
    }
    else if (strncmp(args->request, "sayto$", 6) == 0) {
        handle_sayto(args);
    }
    else if (strncmp(args->request, "mute$", 5) == 0) {
        // handle mute
    }
    else if (strncmp(args->request, "unmute$", 7) == 0) {
        // handle unmute
    }
    free(args);
    return NULL;
}


void* listener_thread(void *arg) {
    ServerState *state = (ServerState *)arg;

    while (1) {
        RequestInfo *handler_args = malloc(sizeof(RequestInfo));
        handler_args->server_state = state;

        int rc = udp_socket_read(state->sd, &handler_args->client_addr, handler_args->request, BUFFER_SIZE);
        
        if (rc <= 0) {
            free(handler_args);
            continue;
        }
        // spawn a thread for the appropriate function for an incoming client depending upon their request type
        if (strncmp(handler_args->request, "conn$", 5) == 0) {
            handler_args->requesting_client = NULL;
        } else {
            pthread_rwlock_rdlock_w(&state->client_list_lock);
            handler_args->requesting_client = find_by_address(state->client_list_head, handler_args->client_addr);
            pthread_rwlock_unlock_w(&state->client_list_lock);
        }

        pthread_t handler;
        pthread_create_w(&handler, NULL, request_handler_thread, handler_args);
        pthread_detach(handler);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    // Open the UDP socket
    int sd = udp_socket_open(SERVER_PORT);
    assert(sd > -1);

    // Set up server state
    ServerState server_state;
    server_state.sd = sd;
    server_state.client_list_head = NULL;  // Empty list
    pthread_rwlock_init_w(&server_state.client_list_lock, NULL); 
    
    // Start the listener thread
    pthread_t listener;
    pthread_create_w(&listener, NULL, listener_thread, &server_state);
    
    // Wait forever (listener never exits)
    pthread_join_w(listener, NULL);
    
    // Cleanup (never reached)
    pthread_rwlock_destroy_w(&server_state.client_list_lock);
    close(sd);
    
    return 0;
}