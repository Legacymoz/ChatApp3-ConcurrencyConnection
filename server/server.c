#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "auth.h"
#include "chat.h"
#include "utils.h"
#include "network_server.h"
#include "../shared/models.h"
#include "../shared/network_config.h"

#define USERS_FILE    "../data/users.txt"
#define MESSAGES_FILE "../data/messages.txt"

#define CLOSE_SOCKET close
#define SOCKET       int
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1

/* Global server socket - needed by the signal handler */
SOCKET server_socket;
volatile int server_running = 1;

/* -----------------------------------------------------------------------
 * lock_users_file / unlock_users_file
 * lock_messages_file / unlock_messages_file
 *
 * With processes each child has its own memory space, so no mutex is
 * needed for in-memory data. However all children share the same files
 * on disk. We use flock() for advisory file locking.
 *
 * These are no-ops here because locking is done directly on the file
 * descriptor inside auth.c and chat.c via the helpers below.
 * We keep the function signatures so auth.c / chat.c extern declarations
 * still compile without changes.
 * --------------------------------------------------------------------- */
void lock_users_file()      { /* locking done per-fd in auth.c   */ }
void unlock_users_file()    { /* locking done per-fd in auth.c   */ }
void lock_messages_file()   { /* locking done per-fd in chat.c   */ }
void unlock_messages_file() { /* locking done per-fd in chat.c   */ }

/* -----------------------------------------------------------------------
 * send_response
 * Sends a newline-terminated response to the client.
 * --------------------------------------------------------------------- */
void send_response(int client_socket, char response[]) {
    char full_response[BUFFER_SIZE];
    int  response_len;
    int  total_sent = 0;

    snprintf(full_response, BUFFER_SIZE, "%s\n", response);
    response_len = (int)strlen(full_response);

    while (total_sent < response_len) {
        int sent = send(client_socket, full_response + total_sent,
                        response_len - total_sent, 0);
        if (sent == SOCKET_ERROR) {
            printf("[!] Error sending response\n");
            return;
        }
        total_sent += sent;
    }
}

/* -----------------------------------------------------------------------
 * handle_client
 * Runs the full persistent session loop for one connected client.
 * Called inside the child process after fork().
 * --------------------------------------------------------------------- */
void handle_client(SOCKET client_socket, const char *client_ip,
                   char *session_username) {
    char buffer[BUFFER_SIZE];
    char fields[10][256];
    int  field_count;

    session_username[0] = '\0';

    while (1) {
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received == 0) {
            printf("[Child PID %d][Disconnect] Client %s (%s) closed the connection.\n",
                   getpid(), client_ip,
                   strlen(session_username) ? session_username : "not logged in");
            break;
        }
        if (bytes_received < 0) {
            printf("[Child PID %d][!] recv() error on client %s - dropping connection.\n",
                   getpid(), client_ip);
            break;
        }

        buffer[bytes_received] = '\0';
        parse_request(buffer, fields, &field_count);

        if (field_count == 0) {
            printf("[Child PID %d][!] Empty request from %s - ignoring.\n", 
                   getpid(), client_ip);
            send_response(client_socket, "ERROR|Empty request");
            continue;
        }

        char *request_type = fields[0];

        if (strcmp(request_type, "REGISTER") == 0) {
            if (field_count >= 3) {
                printf("[Child PID %d][REGISTER] %s is attempting to register from %s\n",
                       getpid(), fields[1], client_ip);
                register_user(client_socket, fields[1], fields[2]);
            } else {
                printf("[Child PID %d][!] Malformed REGISTER request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid REGISTER format");
            }
        }
        else if (strcmp(request_type, "LOGIN") == 0) {
            if (field_count >= 3) {
                printf("[Child PID %d][LOGIN] %s is attempting to login from %s\n",
                       getpid(), fields[1], client_ip);
                int ok = authenticate_user(client_socket, fields[1], fields[2]);
                if (ok) {
                    strncpy(session_username, fields[1], MAX_USERNAME - 1);
                    session_username[MAX_USERNAME - 1] = '\0';
                }
            } else {
                printf("[Child PID %d][!] Malformed LOGIN request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid LOGIN format");
            }
        }
        else if (strcmp(request_type, "SEARCH") == 0) {
            if (field_count >= 3) {
                // Consistency check: Log the SEARCH attempt
                printf("[Child PID %d][SEARCH] %s searching for %s\n", 
                       getpid(), session_username, fields[1]);
                search_user(client_socket, fields[1], fields[2]);
            } else {
                printf("[Child PID %d][!] Malformed SEARCH request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid SEARCH format");
            }
        }
        else if (strcmp(request_type, "SEND") == 0) {
            if (field_count >= 4) {
                printf("[Child PID %d][SEND] Message from %s to %s\n", 
                       getpid(), fields[1], fields[2]);
                send_message(client_socket, fields[1], fields[2], fields[3]);
            } else {
                printf("[Child PID %d][!] Malformed SEND request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid SEND format");
            }
        }
        else if (strcmp(request_type, "FETCH") == 0) {
            if (field_count >= 4) {
                printf("[Child PID %d][FETCH] %s fetching messages from %s\n", 
                       getpid(), fields[1], fields[2]);
                int last_id = atoi(fields[3]);
                fetch_messages(client_socket, fields[1], fields[2], last_id);
            } else {
                printf("[Child PID %d][!] Malformed FETCH request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid FETCH format");
            }
        }
        else if (strcmp(request_type, "CONTACTS") == 0) {
            if (field_count >= 2) {
                printf("[Child PID %d][CONTACTS] %s fetching contact list\n", 
                       getpid(), fields[1]);
                fetch_contacts(client_socket, fields[1]);
            } else {
                printf("[Child PID %d][!] Malformed CONTACTS request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid CONTACTS format");
            }
        }
        else if (strcmp(request_type, "DEREGISTER") == 0) {
            if (field_count >= 2) {
                printf("[Child PID %d][DEREGISTER] %s is deleting their account from %s\n",
                       getpid(), fields[1], client_ip);
                deregister_user(client_socket, fields[1]);
            } else {
                printf("[Child PID %d][!] Malformed DEREGISTER request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid DEREGISTER format");
            }
            printf("[Child PID %d][Session] Closing session for %s after deregister.\n",
                   getpid(), client_ip);
            break;
        }
        else if (strcmp(request_type, "LOGOUT") == 0) {
            if (field_count >= 2) {
                printf("[Child PID %d][LOGOUT] %s logging out from %s\n",
                       getpid(), fields[1], client_ip);
                logout_user(client_socket, fields[1]);
            } else {
                printf("[Child PID %d][!] Malformed LOGOUT request from %s\n", 
                       getpid(), client_ip);
                send_response(client_socket, "ERROR|Invalid LOGOUT format");
            }
            printf("[Child PID %d][Session] Closing session for %s after logout.\n",
                   getpid(), client_ip);
            break;
        }
        else {
            printf("[Child PID %d][!] Unknown request type '%s' from %s\n",
                   getpid(), request_type, client_ip);
            send_response(client_socket, "ERROR|Unknown request type");
        }
    }
}

/* -----------------------------------------------------------------------
 * shutdown_server  (Ctrl+C signal handler)
 * Only runs in the parent process.
 * --------------------------------------------------------------------- */
void shutdown_server(int sig) {
    (void)sig;
    printf("\n[Parent PID %d] Server shutting down. Goodbye.\n", getpid());
    server_running = 0;
    CLOSE_SOCKET(server_socket);
    cleanup_server_network();
    exit(0);
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main() {
    struct sockaddr_in server_addr, client_addr;
    SOCKET client_socket;
    socklen_t addr_len = sizeof(client_addr);
    SOCKET discovery_socket;
    fd_set read_fds;

    signal(SIGINT,  shutdown_server);
    /* SIG_IGN tells the kernel to auto-reap child zombies - simplest approach */
    signal(SIGCHLD, SIG_IGN);

    create_file_if_missing(USERS_FILE);
    create_file_if_missing(MESSAGES_FILE);

    if (!initialize_server_network()) {
        return 1;
    }

    discovery_socket = get_discovery_socket();

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("[!] Socket creation failed\n");
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[!] Bind failed\n");
        CLOSE_SOCKET(server_socket);
        return 1;
    }

    if (listen(server_socket, 10) == SOCKET_ERROR) {
        printf("[!] Listen failed\n");
        CLOSE_SOCKET(server_socket);
        return 1;
    }

    printf("[Parent PID %d] Server started. Listening on port %d...\n\n",
           getpid(), PORT);

    while (server_running) {
        int max_fd;

        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        max_fd = server_socket;

        if (discovery_socket != INVALID_SOCKET) {
            FD_SET(discovery_socket, &read_fds);
            if (discovery_socket > max_fd) max_fd = discovery_socket;
        }

        /* On Linux select() first arg must be max_fd + 1 */
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (server_running)
                printf("[!] select() failed\n");
            continue;
        }

        if (discovery_socket != INVALID_SOCKET &&
            FD_ISSET(discovery_socket, &read_fds)) {
            handle_discovery_request(discovery_socket);
        }

        if (FD_ISSET(server_socket, &read_fds)) {
            client_socket = accept(server_socket,
                                   (struct sockaddr *)&client_addr,
                                   &addr_len);
            if (client_socket == INVALID_SOCKET) {
                if (server_running)
                    printf("[!] accept() failed\n");
                continue;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, INET_ADDRSTRLEN);

            /* -------------------------------------------------------
             * fork() — spawn a child process to handle this client.
             * Parent closes client_socket and loops back to accept().
             * Child closes server_socket and handles the session.
             * ----------------------------------------------------- */
            pid_t pid = fork();

            if (pid < 0) {
                printf("[!] fork() failed for client %s\n", client_ip);
                CLOSE_SOCKET(client_socket);
                continue;
            }

            if (pid == 0) {
                /* --- CHILD PROCESS --- */

                /* Child does not need the listening socket */
                CLOSE_SOCKET(server_socket);
                if (discovery_socket != INVALID_SOCKET)
                    CLOSE_SOCKET(discovery_socket);

                char session_username[MAX_USERNAME];
                session_username[0] = '\0';

                printf("[Child  PID %d] Session opened | IP: %s\n",
                       getpid(), client_ip);

                handle_client(client_socket, client_ip, session_username);

                CLOSE_SOCKET(client_socket);

                printf("[Child  PID %d] Session closed | IP: %-16s | User: %s\n",
                       getpid(), client_ip,
                       strlen(session_username) ? session_username : "none");

                exit(0); /* Child exits cleanly */
            }

            /* --- PARENT PROCESS --- */

            /* Parent does not need the client socket - child owns it */
            CLOSE_SOCKET(client_socket);

            printf("[Parent PID %d] Forked child PID %d for client %s\n",
                   getpid(), pid, client_ip);
        }
    }

    CLOSE_SOCKET(server_socket);
    cleanup_server_network();
    return 0;
}