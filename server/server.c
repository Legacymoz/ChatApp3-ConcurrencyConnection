#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <process.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <pthread.h>
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

#define USERS_FILE    "../data/users.txt"
#define MESSAGES_FILE "../data/messages.txt"

/* Global server socket — needed by the signal handler */
SOCKET server_socket;
volatile int server_running = 1;

/* -----------------------------------------------------------------------
 * Active client counter
 * Incremented when a thread starts, decremented when it ends.
 * Protected by its own mutex so concurrent increments are safe.
 * Printed on every connect/disconnect so you always know how many
 * clients are live at any moment.
 * --------------------------------------------------------------------- */
static volatile int active_clients = 0;

#ifdef _WIN32
    HANDLE users_mutex;
    HANDLE messages_mutex;
    HANDLE counter_mutex;
#else
    pthread_mutex_t users_mutex    = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t messages_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t counter_mutex  = PTHREAD_MUTEX_INITIALIZER;
#endif

/* -----------------------------------------------------------------------
 * Lock / unlock helpers for file access
 * Called from auth.c and chat.c via extern declarations.
 * --------------------------------------------------------------------- */
void lock_users_file()      {
#ifdef _WIN32
    WaitForSingleObject(users_mutex, INFINITE);
#else
    pthread_mutex_lock(&users_mutex);
#endif
}

void unlock_users_file()    {
#ifdef _WIN32
    ReleaseMutex(users_mutex);
#else
    pthread_mutex_unlock(&users_mutex);
#endif
}

void lock_messages_file()   {
#ifdef _WIN32
    WaitForSingleObject(messages_mutex, INFINITE);
#else
    pthread_mutex_lock(&messages_mutex);
#endif
}

void unlock_messages_file() {
#ifdef _WIN32
    ReleaseMutex(messages_mutex);
#else
    pthread_mutex_unlock(&messages_mutex);
#endif
}

/* -----------------------------------------------------------------------
 * increment_clients / decrement_clients
 * Thread-safe counter updates. Returns the new count so the caller
 * can log it immediately without a separate read.
 * --------------------------------------------------------------------- */
static int increment_clients() {
#ifdef _WIN32
    WaitForSingleObject(counter_mutex, INFINITE);
    int count = ++active_clients;
    ReleaseMutex(counter_mutex);
#else
    pthread_mutex_lock(&counter_mutex);
    int count = ++active_clients;
    pthread_mutex_unlock(&counter_mutex);
#endif
    return count;
}

static int decrement_clients() {
#ifdef _WIN32
    WaitForSingleObject(counter_mutex, INFINITE);
    int count = --active_clients;
    ReleaseMutex(counter_mutex);
#else
    pthread_mutex_lock(&counter_mutex);
    int count = --active_clients;
    pthread_mutex_unlock(&counter_mutex);
#endif
    return count;
}

/* -----------------------------------------------------------------------
 * send_response
 * Sends a newline-terminated response to the client. Loops until the
 * entire buffer is flushed — handles partial sends gracefully.
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
 *
 * Tracks which user is logged in during the session so the thread can
 * log the username on disconnect, not just the IP address.
 *
 * Exits when:
 *   1. Client sends LOGOUT or DEREGISTER  (clean exit)
 *   2. recv() returns 0  (client closed connection)
 *   3. recv() returns error  (network failure)
 * --------------------------------------------------------------------- */
void handle_client(SOCKET client_socket, const char *client_ip,
                   char *session_username) {
    char buffer[BUFFER_SIZE];
    char fields[10][256];
    int  field_count;

    /* session_username is written here so client_thread can log it
     * after this function returns */
    session_username[0] = '\0';

    while (1) {
        int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received == 0) {
            printf("[Disconnect] Client %s (%s) closed the connection.\n",
                   client_ip,
                   strlen(session_username) ? session_username : "not logged in");
            break;
        }
        if (bytes_received < 0) {
            printf("[!] recv() error on client %s — dropping connection.\n",
                   client_ip);
            break;
        }

        buffer[bytes_received] = '\0';
        parse_request(buffer, fields, &field_count);

        if (field_count == 0) {
            printf("[!] Empty request from %s — ignoring.\n", client_ip);
            send_response(client_socket, "ERROR|Empty request");
            continue;
        }

        char *request_type = fields[0];

        /* ---- Dispatch ---- */

        if (strcmp(request_type, "REGISTER") == 0) {
            if (field_count >= 3) {
                printf("[REGISTER] %s is attempting to register "
                       "from %s\n", fields[1], client_ip);
                register_user(client_socket, fields[1], fields[2]);
            } else {
                printf("[!] Malformed REGISTER request from %s\n", client_ip);
                send_response(client_socket,
                              "ERROR|Invalid REGISTER format");
            }
        }
        else if (strcmp(request_type, "LOGIN") == 0) {
            if (field_count >= 3) {
                printf("[LOGIN] %s is attempting to login "
                       "from %s\n", fields[1], client_ip);
                int ok = authenticate_user(client_socket,
                                           fields[1], fields[2]);
                if (ok) {
                    /* Store username so disconnect log shows who it was */
                    strncpy(session_username, fields[1], MAX_USERNAME - 1);
                    session_username[MAX_USERNAME - 1] = '\0';
                }
            } else {
                printf("[!] Malformed LOGIN request from %s\n", client_ip);
                send_response(client_socket, "ERROR|Invalid LOGIN format");
            }
        }
        else if (strcmp(request_type, "SEARCH") == 0) {
            if (field_count >= 3) {
                search_user(client_socket, fields[1], fields[2]);
            } else {
                printf("[!] Malformed SEARCH request from %s\n", client_ip);
                send_response(client_socket, "ERROR|Invalid SEARCH format");
            }
        }
        else if (strcmp(request_type, "SEND") == 0) {
            if (field_count >= 4) {
                send_message(client_socket, fields[1],
                             fields[2], fields[3]);
            } else {
                printf("[!] Malformed SEND request from %s\n", client_ip);
                send_response(client_socket, "ERROR|Invalid SEND format");
            }
        }
        else if (strcmp(request_type, "FETCH") == 0) {
            if (field_count >= 4) {
                /* fields[3] is the last known message ID from the client.
                 * 0 means initial load — send everything.
                 * >0 means poll — send only messages newer than that ID. */
                int last_id = atoi(fields[3]);
                fetch_messages(client_socket, fields[1],
                               fields[2], last_id);
            } else {
                printf("[!] Malformed FETCH request from %s\n", client_ip);
                send_response(client_socket, "ERROR|Invalid FETCH format");
            }
        }
        else if (strcmp(request_type, "CONTACTS") == 0) {
            if (field_count >= 2) {
                fetch_contacts(client_socket, fields[1]);
            } else {
                printf("[!] Malformed CONTACTS request from %s\n", client_ip);
                send_response(client_socket,
                              "ERROR|Invalid CONTACTS format");
            }
        }
        else if (strcmp(request_type, "DEREGISTER") == 0) {
            if (field_count >= 2) {
                printf("[DEREGISTER] %s is deleting their account "
                       "from %s\n", fields[1], client_ip);
                deregister_user(client_socket, fields[1]);
            } else {
                printf("[!] Malformed DEREGISTER request from %s\n",
                       client_ip);
                send_response(client_socket,
                              "ERROR|Invalid DEREGISTER format");
            }
            /* Session ends after deregister regardless of outcome */
            printf("[Session] Closing session for %s after deregister.\n",
                   client_ip);
            break;
        }
        else if (strcmp(request_type, "LOGOUT") == 0) {
            if (field_count >= 2) {
                printf("[LOGOUT] %s logging out from %s\n",
                       fields[1], client_ip);
                logout_user(client_socket, fields[1]);
            } else {
                printf("[!] Malformed LOGOUT request from %s\n", client_ip);
                send_response(client_socket, "ERROR|Invalid LOGOUT format");
            }
            /* Session ends after logout */
            printf("[Session] Closing session for %s after logout.\n",
                   client_ip);
            break;
        }
        else {
            printf("[!] Unknown request type '%s' from %s\n",
                   request_type, client_ip);
            send_response(client_socket, "ERROR|Unknown request type");
        }
    }
}

/* -----------------------------------------------------------------------
 * ClientArgs
 * Heap-allocated by main, passed to the worker thread, freed by the
 * worker when the session ends.
 * --------------------------------------------------------------------- */
typedef struct {
    SOCKET client_socket;
    char   client_ip[INET_ADDRSTRLEN];
} ClientArgs;

/* -----------------------------------------------------------------------
 * client_thread
 * Entry point for each worker thread. Manages the full lifetime of one
 * client session — connect, serve, disconnect, cleanup.
 * --------------------------------------------------------------------- */
#ifdef _WIN32
unsigned __stdcall client_thread(void *arg)
#else
void *client_thread(void *arg)
#endif
{
    ClientArgs *ca = (ClientArgs *)arg;

    /* Track which user logged in during this session for the end log */
    char session_username[MAX_USERNAME];
    session_username[0] = '\0';

    int count = increment_clients();
    printf("[+] Session opened  | IP: %-16s | Active clients: %d\n",
           ca->client_ip, count);

    handle_client(ca->client_socket, ca->client_ip, session_username);

    CLOSE_SOCKET(ca->client_socket);

    count = decrement_clients();
    printf("[-] Session closed  | IP: %-16s | User: %-16s | "
           "Active clients: %d\n",
           ca->client_ip,
           strlen(session_username) ? session_username : "none",
           count);

    free(ca);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* -----------------------------------------------------------------------
 * spawn_client_thread
 * Allocates a ClientArgs, fills it, launches a detached worker thread.
 * Returns 1 on success, 0 on failure.
 * --------------------------------------------------------------------- */
static int spawn_client_thread(SOCKET client_socket, const char *client_ip) {
    ClientArgs *ca = (ClientArgs *)malloc(sizeof(ClientArgs));
    if (!ca) {
        printf("[!] Out of memory — cannot spawn thread for %s\n", client_ip);
        return 0;
    }

    ca->client_socket = client_socket;
    strncpy(ca->client_ip, client_ip, INET_ADDRSTRLEN - 1);
    ca->client_ip[INET_ADDRSTRLEN - 1] = '\0';

#ifdef _WIN32
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, client_thread, ca, 0, NULL);
    if (h == 0) {
        printf("[!] Failed to create thread for %s\n", client_ip);
        free(ca);
        return 0;
    }
    CloseHandle(h);
#else
    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, client_thread, ca) != 0) {
        printf("[!] Failed to create thread for %s\n", client_ip);
        pthread_attr_destroy(&attr);
        free(ca);
        return 0;
    }
    pthread_attr_destroy(&attr);
#endif

    return 1;
}

/* -----------------------------------------------------------------------
 * shutdown_server  (Ctrl+C signal handler)
 * --------------------------------------------------------------------- */
void shutdown_server(int sig) {
    (void)sig;
    printf("\n[+] Server shutting down. Active clients: %d\n", active_clients);
    printf("[+] Goodbye.\n");
    server_running = 0;
    CLOSE_SOCKET(server_socket);
    cleanup_server_network();
#ifdef _WIN32
    CloseHandle(users_mutex);
    CloseHandle(messages_mutex);
    CloseHandle(counter_mutex);
    WSACleanup();
#endif
    exit(0);
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main() {
    struct sockaddr_in server_addr, client_addr;
    SOCKET client_socket;
    int    addr_len = sizeof(client_addr);
    SOCKET discovery_socket;
    fd_set read_fds;

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("[!] WSAStartup failed\n");
        return 1;
    }
    users_mutex    = CreateMutex(NULL, FALSE, NULL);
    messages_mutex = CreateMutex(NULL, FALSE, NULL);
    counter_mutex  = CreateMutex(NULL, FALSE, NULL);
    if (!users_mutex || !messages_mutex || !counter_mutex) {
        printf("[!] Failed to create mutexes\n");
        return 1;
    }
#endif

    signal(SIGINT, shutdown_server);

    create_file_if_missing(USERS_FILE);
    create_file_if_missing(MESSAGES_FILE);

    if (!initialize_server_network()) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    discovery_socket = get_discovery_socket();

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("[!] Socket creation failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

#ifdef _WIN32
    char opt = 1;
#else
    int opt = 1;
#endif
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("[!] Bind failed\n");
        CLOSE_SOCKET(server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_socket, 10) == SOCKET_ERROR) {
        printf("[!] Listen failed\n");
        CLOSE_SOCKET(server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("[+] Concurrent server started. Listening on port %d...\n\n",
           PORT);

    while (server_running) {
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        if (discovery_socket != INVALID_SOCKET)
            FD_SET(discovery_socket, &read_fds);

        if (select(0, &read_fds, NULL, NULL, NULL) == SOCKET_ERROR) {
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
#ifdef _WIN32
            strncpy(client_ip, inet_ntoa(client_addr.sin_addr),
                    INET_ADDRSTRLEN - 1);
#else
            inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, INET_ADDRSTRLEN);
#endif

            if (!spawn_client_thread(client_socket, client_ip)) {
                CLOSE_SOCKET(client_socket);
            }
        }
    }

    CLOSE_SOCKET(server_socket);
    cleanup_server_network();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}