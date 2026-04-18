#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <conio.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/select.h>
    #include <termios.h>
    #include <fcntl.h>
    #define CLOSE_SOCKET close
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "ui.h"
#include "network_client.h"
#include "../shared/models.h"

// Global state
int is_logged_in = 0;
int app_running = 1;
char session_user[MAX_USERNAME];

// Function prototypes
int connect_to_server();
void send_request(SOCKET sock, char request[]);
void receive_response(SOCKET sock, char buffer[]);
void sanitize_input(char buffer[]);
void display_main_menu();
void display_dashboard();
void handle_register();
void handle_login();
void handle_search();
void handle_deregister();
void handle_logout();
void handle_inbox();
void handle_chat(char partner[]);
void send_chat_message(char partner[], char text[]);
void fetch_and_display_messages(char partner[]);
int input_available();

// Strips unwanted characters from input
void sanitize_input(char buffer[]) {
    char *newline = strchr(buffer, '\n');
    if (newline) *newline = '\0';
    
    char *cr = strchr(buffer, '\r');
    if (cr) *cr = '\0';
    
    // Remove pipe characters
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '|') {
            buffer[i] = ' ';
        }
    }
}

// Opens a TCP socket connection to the server
int connect_to_server() {
    SOCKET sock;
    struct sockaddr_in server_addr;
    
    #ifdef _WIN32
        // Initialize Winsock (if not already done)
        static int wsa_initialized = 0;
        if (!wsa_initialized) {
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
                ui_server_error();
                return -1;
            }
            wsa_initialized = 1;
        }
    #endif
    
    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        ui_server_error();
        return -1;
    }
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(get_server_ip());
    
    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        ui_server_error();
        CLOSE_SOCKET(sock);
        return -1;
    }
    
    return (int)sock;
}

// Sends a request to the server
void send_request(SOCKET sock, char request[]) {
    int total_sent = 0;
    int request_len = strlen(request);
    
    while (total_sent < request_len) {
        int sent = send(sock, request + total_sent, request_len - total_sent, 0);
        if (sent == SOCKET_ERROR) {
            return;
        }
        total_sent += sent;
    }
}

// Receives a response from the server
void receive_response(SOCKET sock, char buffer[]) {
    int total_received = 0;
    
    while (total_received < BUFFER_SIZE - 1) {
        int received = recv(sock, buffer + total_received, 1, 0);
        if (received <= 0) {
            break;
        }
        if (buffer[total_received] == '\n') {
            break;
        }
        total_received++;
    }
    buffer[total_received] = '\0';
}

// Checks if keyboard input is available (non-blocking)
int input_available() {
    #ifdef _WIN32
        return _kbhit();
    #else
        struct timeval tv = {0, 0};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
    #endif
}

// Handles the registration flow
void handle_register() {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    ui_display_register_screen();
    
    printf("  Enter desired username: ");
    fgets(username, MAX_USERNAME, stdin);
    sanitize_input(username);
    
    printf("  Enter desired password: ");
    fgets(password, MAX_PASSWORD, stdin);
    sanitize_input(password);
    
    // Connect and send request
    int sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "REGISTER|%s|%s\n", username, password);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
    
    // Parse response
    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "Account created");
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "Registration failed");
    }
    
    ui_wait_for_enter();
}

// Handles the login flow
void handle_login() {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    ui_display_login_screen();
    
    printf("  Enter username: ");
    fgets(username, MAX_USERNAME, stdin);
    sanitize_input(username);
    
    printf("  Enter password: ");
    fgets(password, MAX_PASSWORD, stdin);
    sanitize_input(password);
    
    // Connect and send request
    int sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "LOGIN|%s|%s\n", username, password);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
    
    // Parse response
    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "Login successful");
        strncpy(session_user, username, MAX_USERNAME - 1);
        session_user[MAX_USERNAME - 1] = '\0';
        is_logged_in = 1;
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "Login failed");
    }
    
    ui_wait_for_enter();
}

// Handles the user search flow
void handle_search() {
    char target[MAX_USERNAME];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    ui_display_search_screen();
    
    printf("  Enter username to search: ");
    fgets(target, MAX_USERNAME, stdin);
    sanitize_input(target);
    
    // Connect and send request
    int sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "SEARCH|%s|%s\n", session_user, target);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
    
    // Parse response
    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "User found");
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "User not found");
    }
    
    ui_wait_for_enter();
}

// Handles the deregistration flow
void handle_deregister() {
    char confirm;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    ui_display_deregister_warning();
    confirm = getchar();
    
    // Clear input buffer
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    
    if (confirm != 'y' && confirm != 'Y') {
        return;
    }
    
    // Connect and send request
    int sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "DEREGISTER|%s\n", session_user);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
    
    // Parse response
    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "Account deleted");
        memset(session_user, 0, sizeof(session_user));
        is_logged_in = 0;
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "Deregistration failed");
    }
    
    ui_wait_for_enter();
}

// Handles the logout flow
void handle_logout() {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    // Connect and send request
    int sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "LOGOUT|%s\n", session_user);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
    
    // Parse response and logout regardless
    char *msg = strchr(response, '|');
    ui_success(msg ? msg + 1 : "Logged out");
    
    memset(session_user, 0, sizeof(session_user));
    is_logged_in = 0;
    
    ui_wait_for_enter();
}

// Handles the inbox/chat partner selection
void handle_inbox() {
    char partner[MAX_USERNAME];
    char input[MAX_USERNAME];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char contacts[50][50];
    int contact_count = 0;
    
    // Fetch contacts list
    int sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "CONTACTS|%s\n", session_user);
    send_request(sock, request);
    
    // Receive contacts until END
    while (contact_count < 50) {
        receive_response(sock, response);
        
        if (strcmp(response, "END") == 0 || strlen(response) == 0) {
            break;
        }
        
        if (strncmp(response, "CONTACT|", 8) == 0) {
            strncpy(contacts[contact_count], response + 8, 49);
            contacts[contact_count][49] = '\0';
            contact_count++;
        }
    }
    CLOSE_SOCKET(sock);
    
    // Display inbox with contacts
    ui_display_inbox(contacts, contact_count);
    
    printf("  > ");
    fgets(input, MAX_USERNAME, stdin);
    sanitize_input(input);
    
    // Check if input is a number (contact selection)
    int selection = atoi(input);
    if (selection > 0 && selection <= contact_count) {
        strncpy(partner, contacts[selection - 1], MAX_USERNAME - 1);
        partner[MAX_USERNAME - 1] = '\0';
    } else {
        strncpy(partner, input, MAX_USERNAME - 1);
        partner[MAX_USERNAME - 1] = '\0';
    }
    
    if (strlen(partner) == 0) {
        return;
    }
    
    // Validate partner exists
    sock = connect_to_server();
    if (sock == -1) {
        ui_wait_for_enter();
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "SEARCH|%s|%s\n", session_user, partner);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
    
    if (strncmp(response, "ERROR", 5) == 0) {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "User not found");
        ui_wait_for_enter();
        return;
    }
    
    // Launch chat with validated partner
    handle_chat(partner);
}

// Fetches and displays messages for a conversation
void fetch_and_display_messages(char partner[]) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    int sock = connect_to_server();
    if (sock == -1) {
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "FETCH|%s|%s\n", session_user, partner);
    send_request(sock, request);
    
    // Display chat header
    ui_display_chat_screen(session_user, partner);
    
    // Receive and display messages until END
    while (1) {
        receive_response(sock, response);
        
        if (strcmp(response, "END") == 0 || strlen(response) == 0) {
            break;
        }
        
        if (strncmp(response, "DATA", 4) == 0) {
            // Parse: DATA|id|sender|receiver|text|timestamp
            char *fields[6];
            char temp[BUFFER_SIZE];
            strncpy(temp, response, BUFFER_SIZE - 1);
            temp[BUFFER_SIZE - 1] = '\0';
            
            int field_count = 0;
            char *token = strtok(temp, "|");
            while (token && field_count < 6) {
                fields[field_count++] = token;
                token = strtok(NULL, "|");
            }
            
            if (field_count >= 6) {
                ui_display_message(fields[2], fields[5], fields[4]);
            }
        }
    }
    
    CLOSE_SOCKET(sock);
}

// Sends a chat message
void send_chat_message(char partner[], char text[]) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    sanitize_input(text);
    
    if (strlen(text) == 0) {
        return;
    }
    
    int sock = connect_to_server();
    if (sock == -1) {
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "SEND|%s|%s|%s\n", session_user, partner, text);
    send_request(sock, request);
    receive_response(sock, response);
    CLOSE_SOCKET(sock);
}

// Handles the active chat session
void handle_chat(char partner[]) {
    char input[MAX_MESSAGE];
    time_t last_fetch = 0;
    
    // Initial message fetch and display
    fetch_and_display_messages(partner);
    ui_display_chat_input_prompt();
    
    while (1) {
        // Auto-refresh every 2 seconds
        time_t now = time(NULL);
        if (now - last_fetch >= 2) {
            fetch_and_display_messages(partner);
            ui_display_chat_input_prompt();
            last_fetch = now;
        }
        
        // Check for user input (non-blocking approach for Windows)
        #ifdef _WIN32
            if (_kbhit()) {
                // Input is available, read the full line
                fgets(input, MAX_MESSAGE, stdin);
                sanitize_input(input);
                
                // Check for quit command
                if (strcmp(input, "/q") == 0) {
                    break;
                }
                
                // Send message
                if (strlen(input) > 0) {
                    send_chat_message(partner, input);
                    fetch_and_display_messages(partner);
                    ui_display_chat_input_prompt();
                    last_fetch = time(NULL);
                }
            }
        #else
            if (input_available()) {
                fgets(input, MAX_MESSAGE, stdin);
                sanitize_input(input);
                
                if (strcmp(input, "/q") == 0) {
                    break;
                }
                
                if (strlen(input) > 0) {
                    send_chat_message(partner, input);
                    fetch_and_display_messages(partner);
                    ui_display_chat_input_prompt();
                    last_fetch = time(NULL);
                }
            }
        #endif
        
        // Small delay to prevent CPU spinning
        #ifdef _WIN32
            Sleep(100);
        #else
            usleep(100000);
        #endif
    }
}

// Displays the main menu and handles selection
void display_main_menu() {
    int choice;
    
    ui_display_main_menu();
    
    if (scanf("%d", &choice) != 1) {
        // Clear invalid input
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        return;
    }
    // Clear the newline
    while (getchar() != '\n');
    
    switch (choice) {
        case 1:
            handle_login();
            break;
        case 2:
            handle_register();
            break;
        case 3:
            app_running = 0;
            break;
        default:
            break;
    }
}

// Displays the dashboard and handles selection
void display_dashboard() {
    int choice;
    
    ui_display_dashboard(session_user);
    
    if (scanf("%d", &choice) != 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        return;
    }
    while (getchar() != '\n');
    
    switch (choice) {
        case 1:
            handle_inbox();
            break;
        case 2:
            handle_search();
            break;
        case 3:
            handle_deregister();
            break;
        case 4:
            handle_logout();
            break;
        default:
            break;
    }
}

int main() {
    #ifdef _WIN32
        // Initialize Winsock
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            printf("Failed to initialize Winsock\n");
            return 1;
        }
    #endif
    
    if (!initialize_server_address()) {
        printf("Failed to determine server address.\n");
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    // Main application loop
    while (app_running) {
        if (is_logged_in) {
            display_dashboard();
        } else {
            display_main_menu();
        }
    }
    
    ui_clear_screen();
    printf("\n  Goodbye!\n\n");
    
    #ifdef _WIN32
        WSACleanup();
    #endif
    
    return 0;
}
