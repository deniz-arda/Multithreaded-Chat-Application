#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "udp.h"
#include "thread_wrappers.h"

#define HISTORY_SIZE 15

// Circular buffer for message history
typedef struct {
    char messages[HISTORY_SIZE][BUFFER_SIZE];
    int count;  // Total messages added (may exceed HISTORY_SIZE)
    int head;   // Index where next message will be written
} MessageHistory;


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
    MessageHistory history;
    pthread_rwlock_t history_lock;
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

    //initialise mute arrays
    new_node -> muted_addresses = NULL;
    new_node -> muted_count = 0;

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

            //free muted addresses array if it exists
            if (current -> muted_addresses != NULL){
                free(current -> muted_addresses);
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


bool is_muted(ClientNode *client, struct sockaddr_in addr) {
    if (client == NULL || client -> muted_addresses == NULL){
        return false;
    }

    for (int i = 0; i < client->muted_count; i++) {
        if (memcmp(&client -> muted_addresses[i], &addr, sizeof(struct sockaddr_in)) == 0){
            return true;
        }
    }

    return false;

}

bool is_admin(struct sockaddr_in addr) {
    int port = ntohs(addr.sin_port);
    return port == 6666;
}


//new functions for proposed extension 1

// Initialize message history
void init_message_history(MessageHistory *history) {
    history->count = 0;
    history->head = 0;
    memset(history->messages, 0, sizeof(history->messages));
}

// Add a message to the circular buffer
void add_message_to_history(MessageHistory *history, const char *message) {
    strcpy(history->messages[history->head], message);
    history->head = (history->head + 1) % HISTORY_SIZE;
    history->count++;
}


// Send history to a client
void send_history_to_client(ServerState *server_state, struct sockaddr_in *client_addr) {
    pthread_rwlock_rdlock_w(&server_state->history_lock);
    
    int num_messages = (server_state->history.count < HISTORY_SIZE) ? 
                       server_state->history.count : HISTORY_SIZE;
    
    if (num_messages > 0) {
        char history_header[BUFFER_SIZE] = "--- Last 15 Messages ---";
        udp_socket_write(server_state->sd, client_addr, history_header, BUFFER_SIZE);
        
        // Calculate starting index in circular buffer
        int start_index;
        if (server_state->history.count < HISTORY_SIZE) {
            start_index = 0;
        } else {
            start_index = server_state->history.head;
        }
        
        // Send messages in chronological order
        for (int i = 0; i < num_messages; i++) {
            int index = (start_index + i) % HISTORY_SIZE;
            udp_socket_write(server_state->sd, client_addr, 
                           server_state->history.messages[index], BUFFER_SIZE);
        }
        
        char history_footer[BUFFER_SIZE] = "--- End of History ---";
        udp_socket_write(server_state->sd, client_addr, history_footer, BUFFER_SIZE);
    }
    
    pthread_rwlock_unlock_w(&server_state->history_lock);
}

void handle_connect(RequestInfo *args) {
    // parse for name (conn$ name)
    char *name = args->request + 6;
    
    // add new client to linked list
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);

    // Check if this address already exists and remove it (handle reconnection case)
    ClientNode *existing = find_by_address(args->server_state->client_list_head, args->client_addr);
    if (existing != NULL) {
        remove_client(&args->server_state->client_list_head, args->client_addr);
    }

    add_client(&args->server_state->client_list_head, name, args->client_addr);
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);

    // send response to client
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "Hi, %s you have successfully connected to the chat", name);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
    send_history_to_client(args->server_state, &args->client_addr);
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

    pthread_rwlock_wrlock_w(&args->server_state->history_lock);
    add_message_to_history(&args->server_state->history, broadcast);
    pthread_rwlock_unlock_w(&args->server_state->history_lock);
    

    // broadcast to everyone on the list
    ClientNode *current = args->server_state->client_list_head;

    while (current != NULL) {
        if(!is_muted(current, args -> requesting_client -> addr)){
            udp_socket_write(args->server_state->sd, &current->addr, broadcast, BUFFER_SIZE);
        }
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
    recipient_name[name_length] = '\0';

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
        return;
    }
    char private_msg[BUFFER_SIZE];
    snprintf(private_msg, BUFFER_SIZE, "%s: %s", args->requesting_client->name, message);
    udp_socket_write(args->server_state->sd, &recipient->addr, private_msg, BUFFER_SIZE);
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
}

void handle_mute(RequestInfo *args) {
    char *mute_target_name = args->request + 6; // skip "mute$ "
    
    if (args->requesting_client == NULL) {
        char error_msg[BUFFER_SIZE] = "Error: Client is not connected to the server";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    pthread_rwlock_rdlock_w(&args->server_state->client_list_lock);
    
    // Find the client to mute
    ClientNode *target = find_by_name(args->server_state->client_list_head, mute_target_name);
    
    if (target == NULL) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
        
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error: Could not find user %s", mute_target_name);
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    
    if (is_muted(args->requesting_client, target->addr)) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
        
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "User %s is already muted", mute_target_name);
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
    
    
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);
    
    
    args->requesting_client->muted_addresses = (struct sockaddr_in *)realloc(
        args->requesting_client->muted_addresses,
        (args->requesting_client->muted_count + 1) * sizeof(struct sockaddr_in)
    );
    
    // Add the target's address to muted list
    args->requesting_client->muted_addresses[args->requesting_client->muted_count] = target->addr;
    args->requesting_client->muted_count++;
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
    
    
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "You have muted %s", mute_target_name);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
}

void handle_unmute(RequestInfo *args) {
    char *unmute_target_name = args->request + 8; // skip "unmute$ "
    
    if (args->requesting_client == NULL) {
        char error_msg[BUFFER_SIZE] = "Error: Client is not connected to the server";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    pthread_rwlock_rdlock_w(&args->server_state->client_list_lock);
    
    // Find the client to unmute
    ClientNode *target = find_by_name(args->server_state->client_list_head, unmute_target_name);
    
    if (target == NULL) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
        
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error: Could not find user %s", unmute_target_name);
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    // Check if actually muted
    if (!is_muted(args->requesting_client, target->addr)) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
        
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "User %s is not muted", unmute_target_name);
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
    
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);
    

    int found_index = -1;
    for (int i = 0; i < args->requesting_client->muted_count; i++) {
        if (memcmp(&args->requesting_client->muted_addresses[i], &target->addr, sizeof(struct sockaddr_in)) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index != -1) {
        // Shift remaining elements down
        for (int i = found_index; i < args->requesting_client->muted_count - 1; i++) {
            args->requesting_client->muted_addresses[i] = args->requesting_client->muted_addresses[i + 1];
        }
        
        args->requesting_client->muted_count--;
        
        // Reallocate to smaller size (or free if count is 0)
        if (args->requesting_client->muted_count == 0) {
            free(args->requesting_client->muted_addresses);
            args->requesting_client->muted_addresses = NULL;
        } else {
            args->requesting_client->muted_addresses = (struct sockaddr_in *)realloc(
                args->requesting_client->muted_addresses,
                args->requesting_client->muted_count * sizeof(struct sockaddr_in)
            );
        }
    }
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
    
    // Send confirmation 
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "You have unmuted %s", unmute_target_name);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
}

void handle_kick(RequestInfo *args) {
    char *kick_target_name = args->request + 6; // skip "kick$ "
    
    // Check if requester is connected
    if (args->requesting_client == NULL) {
        char error_msg[BUFFER_SIZE] = "Error: Client is not connected to the server";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    // Check if requester is admin (port 6666)
    if (!is_admin(args->client_addr)) {
        char error_msg[BUFFER_SIZE] = "Error: Only admin can kick users";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    pthread_rwlock_rdlock_w(&args->server_state->client_list_lock);
    
    // Find the client to kick
    ClientNode *target = find_by_name(args->server_state->client_list_head, kick_target_name);
    
    if (target == NULL) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
        
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, BUFFER_SIZE, "Error: Could not find user %s", kick_target_name);
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    // Don't allow admin to kick themselves
    if (memcmp(&target->addr, &args->client_addr, sizeof(struct sockaddr_in)) == 0) {
        pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
        
        char error_msg[BUFFER_SIZE] = "Error: Cannot kick yourself";
        udp_socket_write(args->server_state->sd, &args->client_addr, error_msg, BUFFER_SIZE);
        return;
    }
    
    // Store target info before removing (need it for notifications)
    struct sockaddr_in target_addr = target->addr;
    char target_name[BUFFER_SIZE];
    strcpy(target_name, target->name);
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
    
    // Send notification to the kicked client
    char kick_msg[BUFFER_SIZE] = "You have been removed from the chat";
    udp_socket_write(args->server_state->sd, &target_addr, kick_msg, BUFFER_SIZE);
    
    
    pthread_rwlock_wrlock_w(&args->server_state->client_list_lock);
    
    
    remove_client(&args->server_state->client_list_head, target_addr);
    
    // Broadcast removal notification to all remaining clients
    char broadcast[BUFFER_SIZE];
    snprintf(broadcast, BUFFER_SIZE, "%s has been removed from the chat", target_name);
    
    ClientNode *current = args->server_state->client_list_head;
    while (current != NULL) {
        if(memcmp(&current->addr, &args->client_addr, sizeof(struct sockaddr_in)) != 0){

            udp_socket_write(args->server_state->sd, &current->addr, broadcast, BUFFER_SIZE);
        } 
        current = current->next;
    }
    
    pthread_rwlock_unlock_w(&args->server_state->client_list_lock);
    
    // Confirm to admin
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "User %s has been kicked from the chat", target_name);
    udp_socket_write(args->server_state->sd, &args->client_addr, response, BUFFER_SIZE);
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
        handle_mute(args);
    }
    else if (strncmp(args->request, "unmute$", 7) == 0) {
        handle_unmute(args);
    }
    else if (strncmp(args->request, "kick$", 5) == 0) {
        handle_kick(args);
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

    init_message_history(&server_state.history);
    pthread_rwlock_init_w(&server_state.history_lock, NULL);
    
    // Start the listener thread
    pthread_t listener;
    pthread_create_w(&listener, NULL, listener_thread, &server_state);
    
    // Wait forever (listener never exits)
    pthread_join_w(listener, NULL);
    
    // Cleanup (never reached)
    pthread_rwlock_destroy_w(&server_state.client_list_lock);
    pthread_rwlock_destroy_w(&server_state.history_lock);
    close(sd);
    
    return 0;
}