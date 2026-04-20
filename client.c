/*
    Chat App for Lab Final Project
    Client Code
    Made by: Md. Shamsul Arefin
    BSSE: 1732
*/



// Define _POSIX_C_SOURCE for strdup
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h> 
#include <pthread.h> 
#include <unistd.h> // For dup() and isatty()
#include "net_helper.c" 

// A mutex to protect all ncurses calls
pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

#define BUF 512
#define PAD_HEIGHT 2000 // We can store 2000 lines of scrollback

// Hardcoded server info
#define SERVER_IP "127.0.0.5"
#define SERVER_PORT 9000

// Color Pair definitions
#define COLOR_PAIR_USER_1 1
#define COLOR_PAIR_USER_2 2
#define COLOR_PAIR_USER_3 3
#define COLOR_PAIR_USER_4 4
#define COLOR_PAIR_USER_5 5
#define COLOR_PAIR_USER_6 6
#define COLOR_PAIR_MY_NAME 7 // This will be RE-INITIALIZED
#define COLOR_PAIR_SERVER 8
#define COLOR_PAIR_CLIENT_INFO 9
#define COLOR_PAIR_DEFAULT 10

// ncurses window definitions
WINDOW *users_win, *input_win;
WINDOW *users_b, *history_b, *input_b; // Border windows
WINDOW *history_viewport; // The on-screen box
WINDOW *history_pad;      // The giant scrollable content

// Pad scrolling variables
int pad_origin = 0; // The top-most line of the pad to show
int pad_lines = 0;  // How many lines are in the pad

typedef struct {
    FILE *stream;
    char *my_name;
} ReceiverArgs;

void refresh_pad(int snap_to_bottom); // Forward declaration

/**
 * @brief Draws the ncurses window layout.
 */
void draw_windows() {
    int height, width;
    getmaxyx(stdscr, height, width);
    int users_width = width / 4;
    int input_height = 3;
    int history_height = height - input_height;
    int history_width = width - users_width;

    users_b = newwin(history_height, users_width, 0, 0);
    history_b = newwin(history_height, history_width, 0, users_width);
    input_b = newwin(input_height, width, height - input_height, 0);
    box(users_b, 0, 0); box(history_b, 0, 0); box(input_b, 0, 0);
    mvwprintw(users_b, 0, 2, " Users ");
    mvwprintw(history_b, 0, 2, " Chat History (Use Arrow Keys) ");
    mvwprintw(input_b, 0, 2, " Input ");

    users_win = derwin(users_b, history_height - 2, users_width - 2, 1, 1);
    input_win = derwin(input_b, 1, width - 4, 1, 2); 
    
    // Create viewport and pad
    history_viewport = derwin(history_b, history_height - 2, history_width - 2, 1, 1);
    history_pad = newpad(PAD_HEIGHT, history_width - 4); // Pad is virtual
    scrollok(history_pad, TRUE);
    
    scrollok(users_win, TRUE); 

    refresh();
    wrefresh(users_b); wrefresh(history_b); wrefresh(input_b);
    wrefresh(users_win); 
    refresh_pad(1); // Initial refresh of the empty pad
    wrefresh(input_win);

    keypad(input_win, TRUE); // Enable arrow keys
}

/**
 * @brief Refreshes the pad into the viewport. This is the core scrolling logic.
 */
void refresh_pad(int snap_to_bottom) {
    int v_h, v_w, v_y, v_x;
    getmaxyx(history_viewport, v_h, v_w);
    getbegyx(history_viewport, v_y, v_x);

    if (snap_to_bottom) {
        pad_origin = pad_lines - v_h;
    }
    
    if (pad_origin < 0) pad_origin = 0; // Can't scroll past the top
    if (pad_origin > pad_lines - v_h) { // Can't scroll past the bottom
         pad_origin = pad_lines - v_h;
         if (pad_origin < 0) pad_origin = 0;
    }
    
    // (pad_y, pad_x, screen_y1, screen_x1, screen_y2, screen_x2)
    prefresh(history_pad, pad_origin, 0, v_y, v_x, v_y + v_h - 1, v_x + v_w - 1);
}

/**
 * @brief Clears and repopulates the user window (thread-safe).
 */
void update_user_list_window(char *user_list_str, const char *my_name) {
    pthread_mutex_lock(&ncurses_mutex);
    
    int y, x;
    getyx(input_win, y, x); 
    
    wclear(users_win); 
    char *list_copy = strdup(user_list_str); 
    if (list_copy == NULL) {
        wprintw(users_win, "Error parsing list.\n");
    } else {
        char *username_in_list = strtok(list_copy, ",");
        while (username_in_list) {
            if (strcmp(username_in_list, my_name) == 0) {
                // Print in my chosen color.
                wattron(users_win, COLOR_PAIR(COLOR_PAIR_MY_NAME)); 
                wprintw(users_win, "%s (You)\n", username_in_list); 
                wattroff(users_win, COLOR_PAIR(COLOR_PAIR_MY_NAME));
            } else {
                // Print in default (white).
                wprintw(users_win, "%s\n", username_in_list); 
            }
            username_in_list = strtok(NULL, ",");
        }
        free(list_copy); 
    }
    wrefresh(users_win); 

    // Also refresh the pad viewport, then cursor
    refresh_pad(0); // 0 = don't snap
    wmove(input_win, y, x);
    wrefresh(input_win);
    
    pthread_mutex_unlock(&ncurses_mutex);
}

/**
 * @brief Thread function to continuously receive messages from the server.
 */
void *receiver_func(void *arg) {
    ReceiverArgs *args = (ReceiverArgs *)arg;
    char buf[BUF];

    while (fgets(buf, BUF, args->stream) != NULL) {
        buf[strcspn(buf, "\n")] = 0; 
        
        int color_pair = COLOR_PAIR_DEFAULT; 
        int user_color_id = 0;
        char *msg_text = buf; 

        if (strncmp(buf, "USERLIST:", 9) == 0) {
            update_user_list_window(buf + 9, args->my_name); 
            continue; 
        } 
        else if (strncmp(buf, "HISTORY:", 8) == 0) {
            msg_text = buf + 8; 
            if (sscanf(msg_text, "[User:%d]", &user_color_id) == 1) { 
                color_pair = user_color_id; 
            } else if (sscanf(msg_text, "[Private:%d", &user_color_id) == 1) { 
                color_pair = user_color_id; 
            } else {
                color_pair = COLOR_PAIR_MY_NAME; // Use My color for history
            }
        }
        else if (sscanf(buf, "[User:%d]", &user_color_id) == 1) { 
            color_pair = user_color_id;
        }
        else if (sscanf(buf, "[Private:%d", &user_color_id) == 1) { 
            color_pair = user_color_id; 
        }
        else if (sscanf(buf, "[Server:%d]", &user_color_id) == 1) { 
             color_pair = user_color_id;
        }
        
        pthread_mutex_lock(&ncurses_mutex);
        
        int y, x;
        getyx(input_win, y, x); 

        // Print to pad
        wattron(history_pad, COLOR_PAIR(color_pair));
        wprintw(history_pad, "%s\n", msg_text); // Print the full message
        wattroff(history_pad, COLOR_PAIR(color_pair));
        pad_lines++;
        
        refresh_pad(1); // Snap to bottom
        
        wmove(input_win, y, x);
        wrefresh(input_win); 
        
        pthread_mutex_unlock(&ncurses_mutex);
    }

    pthread_mutex_lock(&ncurses_mutex);
    wattron(history_pad, COLOR_PAIR(COLOR_PAIR_SERVER));
    wprintw(history_pad, "Server closed the connection.\n");
    wattroff(history_pad, COLOR_PAIR(COLOR_PAIR_SERVER));
    pad_lines++;
    refresh_pad(1);
    pthread_mutex_unlock(&ncurses_mutex);
    
    return NULL;
}

/**
 * @brief Runs the full ncurses client.
 */
void run_interactive_client(FILE *srv_stream_read, FILE *srv_stream_write, char *username, int my_color_id) {
    // --- ncurses Init ---
    initscr();            
    mousemask(0, NULL); 
    cbreak();             
    noecho(); // We are handling echo MANUALLY
    start_color();        
    
    init_pair(COLOR_PAIR_USER_1, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_PAIR_USER_2, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_PAIR_USER_3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_PAIR_USER_4, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_PAIR_USER_5, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_PAIR_USER_6, COLOR_CYAN, COLOR_BLACK);
    
    // --- Use the chosen color for "MY_NAME" ---
    init_pair(COLOR_PAIR_MY_NAME, my_color_id, COLOR_BLACK); 
    // ---
    
    init_pair(COLOR_PAIR_SERVER, COLOR_RED, COLOR_BLACK);      
    init_pair(COLOR_PAIR_CLIENT_INFO, COLOR_MAGENTA, COLOR_BLACK); 
    init_pair(COLOR_PAIR_DEFAULT, COLOR_WHITE, COLOR_BLACK);     
    
    draw_windows(); 
    // ---

    // --- Start receiver thread ---
    pthread_t tid;
    ReceiverArgs args;
    args.stream = srv_stream_read; 
    args.my_name = username; 
    if (pthread_create(&tid, NULL, receiver_func, &args) != 0) {
        perror("pthread_create");
        endwin(); 
        return;
    }
    // ---

    // --- Main input loop (using wgetch) ---
    char buf[BUF]; 
    int buf_pos = 0;
    char out_buf[BUF]; 

    pthread_mutex_lock(&ncurses_mutex);
    wattron(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
    wprintw(history_pad, "Connected as %s. Type ':q' to quit.\n", username);
    wprintw(history_pad, "Usage: @user <message> (for private) or <message> (for group)\n");
    wprintw(history_pad, "--- Use Arrow Up/Down to scroll history ---\n");
    wprintw(history_pad, "--- End of History ---\n");
    wattroff(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
    pad_lines += 4;
    refresh_pad(1);
    
    wmove(input_win, 0, 0); // Move cursor to start
    wrefresh(input_win);
    pthread_mutex_unlock(&ncurses_mutex);
    // ---

    while (1) {
        int ch = wgetch(input_win); // Read one keypress

        if (ch == KEY_RESIZE) {
            pthread_mutex_lock(&ncurses_mutex);
            delwin(users_win); delwin(history_viewport); delwin(input_win);
            delwin(users_b); delwin(history_b); delwin(input_b);
            delwin(history_pad); 
            
            clear(); refresh(); 
            
            draw_windows(); 
            pad_lines = 0;  
            pad_origin = 0; 
            
            noecho();
            
            fprintf(srv_stream_write, "GETLIST\n"); 
            fflush(srv_stream_write); // Flush for resize
            
            wattron(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
            wprintw(history_pad, "[Client] Window resized. (History is cleared)\n");
            wattroff(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
            pad_lines++;
            refresh_pad(1);
            
            wclear(input_win);
            wprintw(input_win, "%s", buf);
            wrefresh(input_win);
            
            pthread_mutex_unlock(&ncurses_mutex);
            continue; 
        }

        pthread_mutex_lock(&ncurses_mutex);

        switch (ch) {
            case KEY_UP: 
                pad_origin--;
                refresh_pad(0); 
                wrefresh(input_win); 
                break;
                
            case KEY_DOWN: 
                pad_origin++;
                refresh_pad(0); 
                wrefresh(input_win); 
                break;

            case '\n': // ENTER KEY
                if (buf_pos == 0) break; 
                
                buf[buf_pos] = '\0'; 
                
                if (strcmp(buf, ":q") == 0) {
                    fprintf(srv_stream_write, ":q\n"); 
                    fflush(srv_stream_write);
                    pthread_mutex_unlock(&ncurses_mutex); 
                    goto cleanup_interactive; 
                }

                if (buf[0] == '@') {
                    char buf_copy[BUF];
                    strncpy(buf_copy, buf, BUF);

                    char *recipient = strtok(buf_copy + 1, " "); 
                    if (recipient) {
                        char *message = strtok(NULL, ""); 
                        if (message && strlen(message) > 0) {
                            while (*message == ' ') message++;
                            if (strlen(message) > 0) {
                                snprintf(out_buf, BUF, "PRIVATE:%s:%s", recipient, message);
                            } else {
                                wattron(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
                                wprintw(history_pad, "[Client] Error: No message after username.\n");
                                pad_lines++;
                                wattroff(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
                                break; 
                            }
                        } else {
                            wattron(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
                            wprintw(history_pad, "[Client] Error: No message after username.\n");
                            pad_lines++;
                            wattroff(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
                            break; 
                        }
                    } else {
                         wattron(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
                         wprintw(history_pad, "[Client] Error: Invalid format. Use: @username <message>\n");
                         pad_lines++;
                         wattroff(history_pad, COLOR_PAIR(COLOR_PAIR_CLIENT_INFO));
                         break; 
                    }
                } else {
                    snprintf(out_buf, BUF, "USER:%s", buf);
                }

                fprintf(srv_stream_write, "%s\n", out_buf); 
                fflush(srv_stream_write);
                
                buf_pos = 0;
                memset(buf, 0, BUF);
                wclear(input_win);
                
                refresh_pad(1); 
                wrefresh(input_win);
                break;

            case KEY_BACKSPACE:
            case 127: 
            case 8:   
                if (buf_pos > 0) {
                    buf_pos--;
                    buf[buf_pos] = '\0';
                    wclear(input_win);
                    wprintw(input_win, "%s", buf); 
                    wrefresh(input_win);
                }
                break;
            
            default: // Any other character
                if (ch >= 32 && ch <= 126 && buf_pos < BUF - 1) { 
                    buf[buf_pos] = (char)ch;
                    buf_pos++;
                    waddch(input_win, ch);
                    wrefresh(input_win);
                }
                break;
        }
        pthread_mutex_unlock(&ncurses_mutex);
    }

cleanup_interactive:
    pthread_cancel(tid); 
    endwin(); 
}

/**
 * @brief Runs the simple, non-interactive client for file redirection.
 */
void run_redirected_client(FILE *srv_stream_read, FILE *srv_stream_write, char *username) {
    char line[BUF];
    char out_buf[BUF];

    printf("Client running in non-interactive (redirected) mode...\n");
    printf("Sending commands from input file.\n");

    // Loop through stdin (which is the file)
    while (fgets(line, BUF, stdin) != NULL) {
        line[strcspn(line, "\n")] = 0; // Remove newline

        if (strlen(line) == 0) continue; // Skip empty lines

        // Format the line just like the ncurses client
        if (line[0] == '@') {
            char *recipient = strtok(line + 1, " "); 
            if (recipient) {
                char *message = strtok(NULL, ""); 
                if (message && strlen(message) > 0) {
                    while (*message == ' ') message++;
                    if (strlen(message) > 0) {
                        snprintf(out_buf, BUF, "PRIVATE:%s:%s", recipient, message);
                    } else continue; // Skip bad command
                } else continue; // Skip bad command
            } else continue; // Skip bad command
        } else {
            snprintf(out_buf, BUF, "USER:%s", line);
        }

        // Send the formatted command to the server
        printf("Sending: %s\n", out_buf);
        fprintf(srv_stream_write, "%s\n", out_buf);
        fflush(srv_stream_write); // Flush after each send
    }
    
    // When file ends, send quit command
    printf("End of file. Sending quit command.\n");
    fprintf(srv_stream_write, ":q\n");
    fflush(srv_stream_write);
}


int main(int argc, char *argv[]) {
    const char *server_ip = SERVER_IP;
    unsigned int port = SERVER_PORT;
    Connection *srv = client_connect(server_ip, port);
    if (!srv) {
        perror("client_connect");
        return 1;
    }

    // --- Wrap socket FD in FILE* streams ---
    int srv_fd = srv->fd;
    FILE *srv_stream_read = fdopen(dup(srv_fd), "r");
    FILE *srv_stream_write = fdopen(dup(srv_fd), "w");
    if (srv_stream_read == NULL || srv_stream_write == NULL) {
        perror("fdopen");
        conn_close(srv); 
        return 1;
    }
    
    free(srv); 
    // ---

    // --- Username and Color prompt ---
    char username[USERNAME_MAX];
    char srv_reply[BUF];
    char color_choice_str[16];
    int my_color_id = 1; // Default to 1 (Red)
    
    if (isatty(STDIN_FILENO)) {
        printf("Enter your username: "); 
    }
    
    if (!fgets(username, USERNAME_MAX, stdin)) { 
        fprintf(stderr, "Error reading username.\n");
        return 1;
    }
    // Note: We leave the \n on username for now
    
    if (isatty(STDIN_FILENO)) {
        // Interactive mode: ask the user
        printf("Choose your color (1-6):\n");
        printf("  1. Red\n  2. Green\n  3. Yellow\n  4. Blue\n  5. Magenta\n  6. Cyan\n");
        printf("Enter number (1-6): ");
        if (!fgets(color_choice_str, 15, stdin)) {
            strncpy(color_choice_str, "1\n", 3); // Default on error
        }
        my_color_id = atoi(color_choice_str);
        if (my_color_id < 1 || my_color_id > 6) {
            my_color_id = 1; // Validate and default
            sprintf(color_choice_str, "%d\n", my_color_id);
        }
    } else {
        // Redirected mode: read color from file (or use default)
        if (!fgets(color_choice_str, 15, stdin)) {
            // If file is *only* username, send default color
            strncpy(color_choice_str, "1\n", 3); 
        }
        my_color_id = atoi(color_choice_str);
        if (my_color_id < 1 || my_color_id > 6) my_color_id = 1;
    }
    
    // Send username AND color choice
    fprintf(srv_stream_write, "%s", username); 
    fprintf(srv_stream_write, "%s", color_choice_str); 
    fflush(srv_stream_write); // Force send
    
    if (fgets(srv_reply, BUF - 1, srv_stream_read) == NULL) { 
        fprintf(stderr, "Server disconnected or error during login.\n"); 
        return 1;
    }
    srv_reply[strcspn(srv_reply, "\n")] = 0; 
    
    if (strcmp(srv_reply, "OK") != 0) { 
        fprintf(stderr, "Server error: %s\n", srv_reply);
        return 1;
    }
    // --- END LOGIN ---

    // --- Main dispatcher ---
    username[strcspn(username, "\n")] = 0; //remove newline for internal use
    
    if (isatty(STDIN_FILENO)) {
        // We are on a TTY (keyboard). Run the full ncurses UI.
        run_interactive_client(srv_stream_read, srv_stream_write, username, my_color_id);
    } else {
        // We are not on a TTY (file redirection). Run the simple test client.
        run_redirected_client(srv_stream_read, srv_stream_write, username);
    }
    // ---

    // --- Cleanup ---
    fclose(srv_stream_read);
    fclose(srv_stream_write);
    
    printf("Disconnected from server.\n");
    return 0;
}