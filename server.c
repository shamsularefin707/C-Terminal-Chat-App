/*
    Chat App for Lab Final Project
    Modified Server Code
    Made by: Md. Shamsul Arefin
    BSSE: 1732
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h> // For dup()
#include "net_helper.c"

#define BUF 512
#define MAX_CLIENTS 100
#define SERVER_PORT 9000
#define SERVER_IP "127.0.0.5" // Needed for dummy connection
#define CHAT_HISTORY_FILE "chat_history.log"

// --- Global client list and mutexes ---
Connection *client_list[MAX_CLIENTS];
pthread_mutex_t client_list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t history_file_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- Global state for auto-shutdown ---
int g_active_clients = 0;
int g_total_clients_served = 0;
pthread_mutex_t g_client_count_mutex = PTHREAD_MUTEX_INITIALIZER;
int g_listenfd = -1; // Store listener FD globally
// ---

// --- Global color counter (1-6) ---
static int g_next_color_id = 1;
// ---

/**
 * @brief Adds a client to the global list.
 */
void add_client(Connection *c) {
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i] == NULL) {
            client_list[i] = c;
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

/**
 * @brief Removes a client from the global list.
 */
void remove_client(Connection *c) {
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i] == c) {
            client_list[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
}

/**
 * @brief Checks if a username is already taken.
 */
int is_username_taken(const char *username) {
    int taken = 0;
    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i] && strcmp(client_list[i]->username, username) == 0) {
            taken = 1;
            break;
        }
    }
    pthread_mutex_unlock(&client_list_mutex);
    return taken;
}

/**
 * @brief Writes a message to the chat history file with a timestamp.
 */
void log_message(const char *message) {
    pthread_mutex_lock(&history_file_mutex);

    FILE *f = fopen(CHAT_HISTORY_FILE, "a");
    if (f == NULL) {
        perror("fopen chat_history.log");
        pthread_mutex_unlock(&history_file_mutex);
        return;
    }

    time_t now = time(NULL);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "[%Y-%m-%d %H:%M:%S]", localtime(&now));

    // Log file does not need the network delimiter
    fprintf(f, "%s %s\n", time_buf, message);

    fclose(f);
    pthread_mutex_unlock(&history_file_mutex);
}

/**
 * @brief Writes a message to a client's FILE stream.
 * @param stream The FILE stream of the client (NULL for broadcast).
 * @param message The raw message to send (must end in \n).
 */
void write_to_client(FILE *stream, const char *message) {
    if (stream == NULL) {
        // Broadcast to all
        pthread_mutex_lock(&client_list_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_list[i] && client_list[i]->stream_write) {
                fprintf(client_list[i]->stream_write, "%s", message);
                fflush(client_list[i]->stream_write);
            }
        }
        pthread_mutex_unlock(&client_list_mutex);
    } else {
        // Send to one client
        fprintf(stream, "%s", message);
        fflush(stream);
    }
}


/**
 * @brief Broadcasts a message to all connected clients and logs it.
 * @param sender The connection object of the sender (NULL for server).
 * @param message The raw message to send.
 */
void broadcast_message(Connection *sender, const char *message) {
    char log_msg[BUF];
    char net_msg[BUF];
    int color_id;
    const char* sender_name;

    // --- Assign color and name based on sender ---
    if (sender) {
        color_id = sender->color_id;
        sender_name = sender->username;
    } 
  
    else {
        color_id = 10; // 10 = White (for Server, matches client's COLOR_PAIR_DEFAULT)
        sender_name = "Server";
    }
    // ---

    // Create log message (no color prefix)
    snprintf(log_msg, BUF, "[User] %s: %s", sender_name, message);
    log_message(log_msg);

    // Create network message (WITH color prefix and newline)
    snprintf(net_msg, BUF, "[User:%d] %s: %s\n", color_id, sender_name, message);
    
    write_to_client(NULL, net_msg);
}

/**
 * @brief Sends a private message from a sender to a recipient and logs it.
 * @param sender The connection object of the sender.
 */
void send_private_message(Connection *sender, const char *recipient, const char *message) {
    char net_msg[BUF];
    char log_msg[BUF];
    Connection *recipient_conn = NULL;
    Connection *sender_conn = NULL; 

    int sender_color_id = sender->color_id;
    const char* sender_name = sender->username;

    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i]) {
            if (strcmp(client_list[i]->username, recipient) == 0) {
                recipient_conn = client_list[i];
            }
        }
    }
    sender_conn = sender; 
    pthread_mutex_unlock(&client_list_mutex);

    // Log the message (no color prefix)
    snprintf(log_msg, BUF, "[Private] %s -> %s: %s", sender_name, recipient, message);
    log_message(log_msg);

    if (recipient_conn) {
        // Send to recipient (WITH sender's color)
        snprintf(net_msg, BUF, "[Private:%d from %s]: %s\n", sender_color_id, sender_name, message);
        write_to_client(recipient_conn->stream_write, net_msg);
        
        // Send confirmation to sender (WITH recipient's color)
        if (sender_conn) {
            int recipient_color_id = recipient_conn->color_id;
            snprintf(net_msg, BUF, "[Private:%d to %s]: %s\n", recipient_color_id, recipient, message);
            write_to_client(sender_conn->stream_write, net_msg);
        }
    } else {
        // Send error back to sender (Server message, color 10 = White)
        if (sender_conn) {
            snprintf(net_msg, BUF, "[Server:10] User '%s' not found or is offline.\n", recipient);
            write_to_client(sender_conn->stream_write, net_msg);
        }
    }
}

/**
 * @brief MODIFIED: Reads history and sends to a client's FILE stream, WITH PRIVACY.
 */
void send_history_to_client(Connection *c, FILE *client_stream) {
    pthread_mutex_lock(&history_file_mutex);

    FILE *f = fopen(CHAT_HISTORY_FILE, "r");
    if (f == NULL) {
        pthread_mutex_unlock(&history_file_mutex);
        return;
    }

    char line_buf[BUF];
    char net_msg[BUF]; // For formatted messages

    while (fgets(line_buf, BUF, f)) {
        char *message_start = strstr(line_buf, "] [");
        if (message_start == NULL) continue; 
        message_start++; // Point to the '['

        if (strncmp(message_start, "[User]", 6) == 0) {
            int color_id = 10; 
            char sender_name[USERNAME_MAX];

            if (sscanf(message_start, "[User] %49[^:]:", sender_name) == 1) {
                if (strcmp(sender_name, "Server") != 0) {
                    color_id = 10; // Default to white for history
                }
            }

            char* content = strstr(message_start, ":");
            if (content) {
                snprintf(net_msg, BUF, "HISTORY:[User:%d] %s:%s", color_id, sender_name, content + 1);
                //newline and flush 
                fprintf(client_stream, "%s\n", net_msg); 
                fflush(client_stream);

            }
        }
        else if (strncmp(message_start, "[Private]", 9) == 0) {
            char sender[USERNAME_MAX];
            char recipient[USERNAME_MAX];

            if (sscanf(message_start, "[Private] %49s -> %49[^:]", sender, recipient) == 2) {
                
                //PRIVACY CHECK
                if (strcmp(c->username, sender) == 0 || strcmp(c->username, recipient) == 0) {
                    
                    int color_id = 10; // Default
                    char* relevant_name = sender;
                    char* format_str = "[Private:%d from %s]:%s";

                    if(strcmp(c->username, recipient) == 0) {
                         format_str = "[Private:%d from %s]:%s";
                         relevant_name = sender;
                    } 
                    else { 
                        format_str = "[Private:%d to %s]:%s";
                        relevant_name = recipient;
                    }
                    
                    char* content = strstr(message_start, ":");
                    if (content) {
                        snprintf(net_msg, BUF, "HISTORY:");
                        snprintf(net_msg + 8, BUF - 8, format_str, color_id, relevant_name, content + 1);
                        //newline and flush 
                        fprintf(client_stream, "%s\n", net_msg);
                        fflush(client_stream);
                      
                    }
                }
                // --- END PRIVACY CHECK ---
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&history_file_mutex);
}

/**
 * @brief Builds a user list string and sends to ONE client's stream.
 */
void send_user_list_to_client(Connection *c) {
    if (c == NULL || c->stream_write == NULL) return;

    char user_list_msg[BUF];
    int offset = 0;

    offset += snprintf(user_list_msg + offset, BUF - offset, "USERLIST:");
    int first_user = 1;

    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i]) {
            if (!first_user) {
                offset += snprintf(user_list_msg + offset, BUF - offset, ",");
            }
            offset += snprintf(user_list_msg + offset, BUF - offset, "%s", client_list[i]->username);
            first_user = 0;
        }
    }
    pthread_mutex_unlock(&client_list_mutex); 

    // Add newline for network
    offset += snprintf(user_list_msg + offset, BUF - offset, "\n");
    write_to_client(c->stream_write, user_list_msg);
}

/**
 * @brief Builds a user list string and sends it to all clients.
 */
void broadcast_user_list() {
    char user_list_msg[BUF];
    int offset = 0;

    offset += snprintf(user_list_msg + offset, BUF - offset, "USERLIST:");
    int first_user = 1;

    pthread_mutex_lock(&client_list_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_list[i]) {
            if (!first_user) {
                offset += snprintf(user_list_msg + offset, BUF - offset, ",");
            }
            offset += snprintf(user_list_msg + offset, BUF - offset, "%s", client_list[i]->username);
            first_user = 0;
        }
    }
    pthread_mutex_unlock(&client_list_mutex); 
    
    // Add newline for network
    offset += snprintf(user_list_msg + offset, BUF - offset, "\n");

    write_to_client(NULL, user_list_msg);
   
}


void *client_handler(void *arg) {
    Connection *c = (Connection*)arg;
    char line_buf[BUF]; // Buffer for reading lines

    // --- Create FILE streams for client ---
    int client_fd = c->fd;
    FILE *client_stream_read = fdopen(dup(client_fd), "r");
    FILE *client_stream_write = fdopen(dup(client_fd), "w");
    if (client_stream_read == NULL || client_stream_write == NULL) {
        perror("fdopen in handler");
        goto cleanup;
    }
    // Store stream in connection struct
    c->stream_read = client_stream_read;
    c->stream_write = client_stream_write;
    // ---

    // --- Read username using fgets ---
    if (fgets(c->username, USERNAME_MAX - 1, client_stream_read) == NULL) {
        goto cleanup;
    }
    c->username[strcspn(c->username, "\n")] = 0; 
  

    if (is_username_taken(c->username)) {
        fprintf(client_stream_write, "ERROR:Username taken\n");
        fflush(client_stream_write);
        goto cleanup;
    }
    
    // --- Read color choice using fgets ---
    if (fgets(line_buf, 15, client_stream_read) == NULL) {
        goto cleanup;
    }
    c->color_id = atoi(line_buf);
    if (c->color_id < 1 || c->color_id > 6) {
        c->color_id = 1; // Default to 1 (Red) if invalid
    }
    // ---

    //Send OK, *then* send history ---
    fprintf(client_stream_write, "OK\n");
    fflush(client_stream_write); 
    
    // The client will now start its receiver thread.
    
    printf("Sending history to %s...\n", c->username);
    send_history_to_client(c, client_stream_write); // This function will flush each line
    // ---
    
    add_client(c);
    printf("Client connected: %s as %s (Color %d)\n", c->peer_ip, c->username, c->color_id);
    
    char join_msg[100];
    snprintf(join_msg, 100, "User %s has joined.", c->username);
    broadcast_message(NULL, join_msg); // NULL sender = Server

    broadcast_user_list(); 
    
    // --- Message Handling Loop using fgets ---
    while (fgets(line_buf, BUF, client_stream_read) != NULL) {
        line_buf[strcspn(line_buf, "\n")] = 0;

        if (strncmp(line_buf, ":q", 2) == 0) {
            printf("%s requested to quit.\n", c->username);
            goto cleanup;
        }
        if (strncmp(line_buf, "PRIVATE:", 8) == 0) {
            char *recipient = line_buf + 8; 
            char *message = strchr(recipient, ':'); 
            if (message) {
                *message = '\0'; 
                message++;       
                printf("%s (private to %s): %s\n", c->username, recipient, message);
                send_private_message(c, recipient, message);
            } else {
                write_to_client(c->stream_write, "[Server:10] Malformed PRIVATE command.\n");
            }
        } 
        else if (strncmp(line_buf, "USER:", 5) == 0) {
            printf("%s (user): %s\n", c->username, line_buf + 5);
            broadcast_message(c, line_buf + 5);
        }
        else if (strncmp(line_buf, "GETLIST", 7) == 0) {
            printf("Client %s resized, sending user list.\n", c->username);
            send_user_list_to_client(c);
        }
        else {
            printf("%s (default user): %s\n", c->username, line_buf);
            broadcast_message(c, line_buf);
        }
    }

cleanup:
    printf("%s disconnected.\n", c->username[0] ? c->username : c->peer_ip);
    
    if (c->username[0]) {
        char departure_msg[100];
        snprintf(departure_msg, 100, "User %s has left.", c->username);
        broadcast_message(NULL, departure_msg); 
    }

    remove_client(c); 
    
    if (c->username[0]) { 
        broadcast_user_list();
    }

    // --- Auto-shutdown logic ---
    int should_shutdown = 0;
    pthread_mutex_lock(&g_client_count_mutex);
    g_active_clients--;
    printf("Client disconnected. %d active clients remaining.\n", g_active_clients);
    if (g_total_clients_served > 0 && g_active_clients == 0) {
        should_shutdown = 1;
    }
    pthread_mutex_unlock(&g_client_count_mutex);

    if (should_shutdown) {
        printf("Last client disconnected. Sending shutdown signal to server...\n");
        Connection *dummy = client_connect("127.0.0.1", SERVER_PORT);
        if (dummy) {
            conn_close(dummy);
        } else {
            printf("Dummy connection failed. Forcing exit.\n");
            listener_close(g_listenfd);
            exit(0);
        }
    }
    // --- 

    // Close streams
    if(c->stream_read) fclose(c->stream_read);
    if(c->stream_write) fclose(c->stream_write);
    
    conn_close(c); // This just frees the struct now
    pthread_exit(NULL);
}


int main(int argc, char *argv[]) {
    unsigned int port = SERVER_PORT;
    printf("Server starting on port %u...\n", port);

    g_listenfd = listener_create(NULL, port); 
    if (g_listenfd < 0) {
        perror("listener_create");
        return 1;
    }
    
    printf("Server listening on port %u...\n", port);
    printf("Chat history will be saved to %s\n", CHAT_HISTORY_FILE); 

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_list[i] = NULL;
    }

    while (1) {
        Connection *client = listener_accept(g_listenfd);
        if (!client) {
            break; 
        }

        pthread_mutex_lock(&g_client_count_mutex);
        if (g_total_clients_served > 0 && g_active_clients == 0) {
            pthread_mutex_unlock(&g_client_count_mutex);
            conn_close(client); // Close the "dummy" client
            break; 
        }
        
        g_active_clients++;
        g_total_clients_served++;
        printf("New client accepted. %d active clients.\n", g_active_clients);
        pthread_mutex_unlock(&g_client_count_mutex);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, client) != 0) {
            perror("pthread_create");
            pthread_mutex_lock(&g_client_count_mutex);
            g_active_clients--;
            pthread_mutex_unlock(&g_client_count_mutex);
            conn_close(client);
        } else {
            pthread_detach(tid);
        }
    }

    printf("All clients disconnected. Server shutting down.\n");
    listener_close(g_listenfd);

    return 0;
}