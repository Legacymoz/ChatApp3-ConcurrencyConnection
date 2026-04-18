#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define CLOSE_SOCKET close
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "auth.h"
#include "chat.h"
#include "utils.h"
#include "network_server.h"
#include "../shared/models.h"
#include "../shared/network_config.h"

#define USERS_FILE "../data/users.txt"
#define MESSAGES_FILE "../data/messages.txt"

// Global server socket for signal handler
SOCKET server_socket;
volatile int server_running = 1;

// Sends a response to the client
void send_response(int client_socket, char response[]) {
    int total_sent = 0;
    int response_len = strlen(response);
    
    // Add newline terminator for message framing
    char full_response[BUFFER_SIZE];
    snprintf(full_response, BUFFER_SIZE, "%s\n", response);
    response_len = strlen(full_response);
    
    while (total_sent < response_len) {
        int sent = send(client_socket, full_response + total_sent, response_len - total_sent, 0);
        if (sent == SOCKET_ERROR) {
            printf("[!] Error sending response\n");
            return;
        }
        total_sent += sent;
    }
}

// Handles a single client connection
void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    char fields[10][256];
    int field_count;
    
    // Receive request from client
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        printf("[!] Error receiving data or client disconnected\n");
        return;
    }
    buffer[bytes_received] = '\0';
    
    // Parse the request
    parse_request(buffer, fields, &field_count);
    
    if (field_count == 0) {
        send_response(client_socket, "ERROR|Empty request");
        return;
    }
    
    // Determine request type and delegate to appropriate module
    char *request_type = fields[0];
    
    // Log request (compact format, skip FETCH and CONTACTS to reduce noise)
    if (field_count >= 2 && strcmp(request_type, "FETCH") != 0 && strcmp(request_type, "CONTACTS") != 0) {
        printf("[%s] %s\n", request_type, fields[1]);
    }
    
    if (strcmp(request_type, "REGISTER") == 0) {
        if (field_count >= 3) {
            register_user(client_socket, fields[1], fields[2]);
        } else {
            send_response(client_socket, "ERROR|Invalid REGISTER request format");
        }
    }
    else if (strcmp(request_type, "LOGIN") == 0) {
        if (field_count >= 3) {
            authenticate_user(client_socket, fields[1], fields[2]);
        } else {
            send_response(client_socket, "ERROR|Invalid LOGIN request format");
        }
    }
    else if (strcmp(request_type, "SEARCH") == 0) {
        if (field_count >= 3) {
            search_user(client_socket, fields[1], fields[2]);
        } else {
            send_response(client_socket, "ERROR|Invalid SEARCH request format");
        }
    }
    else if (strcmp(request_type, "DEREGISTER") == 0) {
        if (field_count >= 2) {
            deregister_user(client_socket, fields[1]);
        } else {
            send_response(client_socket, "ERROR|Invalid DEREGISTER request format");
        }
    }
    else if (strcmp(request_type, "LOGOUT") == 0) {
        if (field_count >= 2) {
            logout_user(client_socket, fields[1]);
        } else {
            send_response(client_socket, "ERROR|Invalid LOGOUT request format");
        }
    }
    else if (strcmp(request_type, "SEND") == 0) {
        if (field_count >= 4) {
            send_message(client_socket, fields[1], fields[2], fields[3]);
        } else {
            send_response(client_socket, "ERROR|Invalid SEND request format");
        }
    }
    else if (strcmp(request_type, "FETCH") == 0) {
        if (field_count >= 3) {
            fetch_messages(client_socket, fields[1], fields[2]);
        } else {
            send_response(client_socket, "ERROR|Invalid FETCH request format");
        }
    }
    else if (strcmp(request_type, "CONTACTS") == 0) {
        if (field_count >= 2) {
            fetch_contacts(client_socket, fields[1]);
        } else {
            send_response(client_socket, "ERROR|Invalid CONTACTS request format");
        }
    }
    else {
        send_response(client_socket, "ERROR|Unknown request type");
    }
}

// Signal handler for clean shutdown
void shutdown_server(int signal) {
    printf("\n[+] Server shutting down. Goodbye.\n");
    server_running = 0;
    CLOSE_SOCKET(server_socket);
    cleanup_server_network();
    
    #ifdef _WIN32
        WSACleanup();
    #endif
    
    exit(0);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    SOCKET client_socket;
    int addr_len = sizeof(client_addr);
    SOCKET discovery_socket;
    fd_set read_fds;
    
    #ifdef _WIN32
        // Initialize Winsock
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            printf("[!] WSAStartup failed\n");
            return 1;
        }
    #endif
    
    // Set up signal handler for Ctrl+C
    signal(SIGINT, shutdown_server);
    
    // Create data files if they don't exist
    create_file_if_missing(USERS_FILE);
    create_file_if_missing(MESSAGES_FILE);
    
    if (!initialize_server_network()) {
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }

    discovery_socket = get_discovery_socket();

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("[!] Socket creation failed\n");
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }
    
    // Set socket options to allow address reuse
    #ifdef _WIN32
        char opt = 1;
    #else
        int opt = 1;
    #endif
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[!] Bind failed\n");
        CLOSE_SOCKET(server_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, 5) == SOCKET_ERROR) {
        printf("[!] Listen failed\n");
        CLOSE_SOCKET(server_socket);
        #ifdef _WIN32
            WSACleanup();
        #endif
        return 1;
    }
    
    printf("[+] Server started. Listening on port %d...\n\n", PORT);
    
    // Main server loop - accept connections one at a time (iterative server)
    while (server_running) {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);

        if (discovery_socket != INVALID_SOCKET) {
            FD_SET(discovery_socket, &read_fds);
        }

        if (select(0, &read_fds, NULL, NULL, NULL) == SOCKET_ERROR) {
            if (server_running) {
                printf("[!] Select failed\n");
            }
            continue;
        }

        if (discovery_socket != INVALID_SOCKET && FD_ISSET(discovery_socket, &read_fds)) {
            handle_discovery_request(discovery_socket);
        }

        if (FD_ISSET(server_socket, &read_fds)) {
            client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
            if (client_socket == INVALID_SOCKET) {
                if (server_running) {
                    printf("[!] Accept failed\n");
                }
                continue;
            }

            // Handle the client request
            handle_client(client_socket);

            // Close client connection after handling
            CLOSE_SOCKET(client_socket);
        }
    }
    
    // Cleanup
    CLOSE_SOCKET(server_socket);
    cleanup_server_network();
    #ifdef _WIN32
        WSACleanup();
    #endif
    
    return 0;
}
