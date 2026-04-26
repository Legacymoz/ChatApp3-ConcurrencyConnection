#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <conio.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    #define SLEEP_MS(ms) Sleep(ms)
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/select.h>
    #define CLOSE_SOCKET close
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

#include "ui.h"
#include "network_client.h"
#include "../shared/models.h"

/* -----------------------------------------------------------------------
 * ChatMessage
 * Local copy of a message stored in the client-side sliding window.
 * Only the fields needed for display are kept.
 * --------------------------------------------------------------------- */
typedef struct {
    int  id;
    char sender[MAX_USERNAME];
    char text[MAX_MESSAGE];
    char timestamp[MAX_TIMESTAMP];
} ChatMessage;

#define CHAT_WINDOW_SIZE 20

/* -----------------------------------------------------------------------
 * Global session state
 * session_sock is the one persistent socket for the entire app session.
 * --------------------------------------------------------------------- */
static SOCKET session_sock = INVALID_SOCKET;

int  is_logged_in = 0;
int  app_running  = 1;
char session_user[MAX_USERNAME];

/* Forward declarations */
void sanitize_input(char buffer[]);
void send_request(char request[]);
void receive_response(char buffer[]);
void close_session();
void handle_register();
void handle_login();
void handle_search();
void handle_deregister();
void handle_logout();
void handle_inbox();
void handle_chat(char partner[]);
int  input_available();

/* -----------------------------------------------------------------------
 * sanitize_input
 * --------------------------------------------------------------------- */
void sanitize_input(char buffer[]) {
    char *nl = strchr(buffer, '\n'); if (nl) *nl = '\0';
    char *cr = strchr(buffer, '\r'); if (cr) *cr = '\0';
    for (int i = 0; buffer[i]; i++)
        if (buffer[i] == '|') buffer[i] = ' ';
}

/* -----------------------------------------------------------------------
 * close_session
 * Closes the persistent socket. Safe to call multiple times.
 * --------------------------------------------------------------------- */
void close_session() {
    if (session_sock != INVALID_SOCKET) {
        CLOSE_SOCKET(session_sock);
        session_sock = INVALID_SOCKET;
    }
}

/* -----------------------------------------------------------------------
 * handle_sigint  (Ctrl+C)
 * --------------------------------------------------------------------- */
void handle_sigint(int sig) {
    (void)sig;
    printf("\n[Client] Interrupted. Closing connection...\n");
    close_session();
#ifdef _WIN32
    WSACleanup();
#endif
    exit(0);
}

/* -----------------------------------------------------------------------
 * open_connection
 * Creates a TCP socket and connects to the server. Called once at launch
 * and again after logout (to start a fresh session).
 * --------------------------------------------------------------------- */
static int open_connection() {
    SOCKET sock;
    struct sockaddr_in server_addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        ui_server_error();
        return -1;
    }

    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(get_server_ip());

    if (connect(sock, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == SOCKET_ERROR) {
        ui_server_error();
        CLOSE_SOCKET(sock);
        return -1;
    }

    printf("[Network] Connected to server at %s\n",
           inet_ntoa(server_addr.sin_addr));
    return (int)sock;
}

/* -----------------------------------------------------------------------
 * send_request
 * Sends a string over the persistent session socket.
 * --------------------------------------------------------------------- */
void send_request(char request[]) {
    int total_sent  = 0;
    int request_len = (int)strlen(request);

    while (total_sent < request_len) {
        int sent = send(session_sock,
                        request + total_sent,
                        request_len - total_sent, 0);
        if (sent == SOCKET_ERROR) {
            printf("[!] Send failed\n");
            return;
        }
        total_sent += sent;
    }
}

/* -----------------------------------------------------------------------
 * receive_response
 * Reads one newline-terminated response from the session socket.
 * --------------------------------------------------------------------- */
void receive_response(char buffer[]) {
    int total = 0;
    while (total < BUFFER_SIZE - 1) {
        int r = recv(session_sock, buffer + total, 1, 0);
        if (r <= 0) break;
        if (buffer[total] == '\n') break;
        total++;
    }
    buffer[total] = '\0';
}

/* -----------------------------------------------------------------------
 * input_available
 * Non-blocking keyboard check used in the chat polling loop.
 * --------------------------------------------------------------------- */
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

/* -----------------------------------------------------------------------
 * handle_register
 * --------------------------------------------------------------------- */
void handle_register() {
    char username[MAX_USERNAME], password[MAX_PASSWORD];
    char request[BUFFER_SIZE],   response[BUFFER_SIZE];

    ui_display_register_screen();

    printf("  Enter desired username: ");
    fgets(username, MAX_USERNAME, stdin);
    sanitize_input(username);

    printf("  Enter desired password: ");
    fgets(password, MAX_PASSWORD, stdin);
    sanitize_input(password);

    snprintf(request, BUFFER_SIZE, "REGISTER|%s|%s\n", username, password);
    send_request(request);
    receive_response(response);

    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "Account created");
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "Registration failed");
    }

    ui_wait_for_enter();
}

/* -----------------------------------------------------------------------
 * handle_login
 * --------------------------------------------------------------------- */
void handle_login() {
    char username[MAX_USERNAME], password[MAX_PASSWORD];
    char request[BUFFER_SIZE],   response[BUFFER_SIZE];

    ui_display_login_screen();

    printf("  Enter username: ");
    fgets(username, MAX_USERNAME, stdin);
    sanitize_input(username);

    printf("  Enter password: ");
    fgets(password, MAX_PASSWORD, stdin);
    sanitize_input(password);

    snprintf(request, BUFFER_SIZE, "LOGIN|%s|%s\n", username, password);
    send_request(request);
    receive_response(response);

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

/* -----------------------------------------------------------------------
 * handle_search
 * --------------------------------------------------------------------- */
void handle_search() {
    char target[MAX_USERNAME];
    char request[BUFFER_SIZE], response[BUFFER_SIZE];

    ui_display_search_screen();

    printf("  Enter username to search: ");
    fgets(target, MAX_USERNAME, stdin);
    sanitize_input(target);

    snprintf(request, BUFFER_SIZE, "SEARCH|%s|%s\n", session_user, target);
    send_request(request);
    receive_response(response);

    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "User found");
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "User not found");
    }

    ui_wait_for_enter();
}

/* -----------------------------------------------------------------------
 * handle_deregister
 * --------------------------------------------------------------------- */
void handle_deregister() {
    char request[BUFFER_SIZE], response[BUFFER_SIZE];

    ui_display_deregister_warning();
    char confirm = getchar();
    int c; while ((c = getchar()) != '\n' && c != EOF);

    if (confirm != 'y' && confirm != 'Y') return;

    snprintf(request, BUFFER_SIZE, "DEREGISTER|%s\n", session_user);
    send_request(request);
    receive_response(response);

    if (strncmp(response, "SUCCESS", 7) == 0) {
        char *msg = strchr(response, '|');
        ui_success(msg ? msg + 1 : "Account deleted");
        close_session();
        is_logged_in = 0;
        app_running  = 0;
    } else {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "Deregister failed");
    }

    ui_wait_for_enter();
}

/* -----------------------------------------------------------------------
 * handle_logout
 * Sends LOGOUT, closes the socket (server terminates its session loop),
 * then opens a fresh connection for the next user session.
 * --------------------------------------------------------------------- */
void handle_logout() {
    char request[BUFFER_SIZE], response[BUFFER_SIZE];

    snprintf(request, BUFFER_SIZE, "LOGOUT|%s\n", session_user);
    send_request(request);
    receive_response(response);

    close_session();
    is_logged_in = 0;

    /* Reconnect for next session — user goes back to main menu */
    int sock = open_connection();
    if (sock == -1) {
        app_running = 0;
        return;
    }
    session_sock = (SOCKET)sock;

    char *msg = strchr(response, '|');
    ui_success(msg ? msg + 1 : "Logged out");
    ui_wait_for_enter();
}

/* -----------------------------------------------------------------------
 * redraw_chat
 * Clears the screen and redraws the full chat window from the local
 * message array. Called only when the array has actually changed.
 * --------------------------------------------------------------------- */
static void redraw_chat(const char *partner,
                        ChatMessage window[], int count) {
    ui_display_chat_screen(session_user, partner);
    for (int i = 0; i < count; i++)
        ui_display_message(window[i].sender,
                           window[i].timestamp,
                           window[i].text);
}

/* -----------------------------------------------------------------------
 * load_initial_messages
 * Sends FETCH with last_known_id = 0 (initial load).
 * Fills the window array with up to CHAT_WINDOW_SIZE messages.
 * Returns the highest message ID received, or 0 if none.
 * --------------------------------------------------------------------- */
static int load_initial_messages(const char *partner,
                                 ChatMessage window[], int *count) {
    char request[BUFFER_SIZE], response[BUFFER_SIZE];
    int  last_id = 0;

    *count = 0;

    /* last_known_id = 0 tells the server: initial load, send last 20 */
    snprintf(request, BUFFER_SIZE, "FETCH|%s|%s|0\n",
             session_user, partner);
    send_request(request);

    while (1) {
        receive_response(response);
        if (strcmp(response, "END") == 0 || strlen(response) == 0) break;

        if (strncmp(response, "DATA|", 5) == 0) {
            /* Parse: DATA|id|sender|receiver|text|timestamp */
            char  temp[BUFFER_SIZE];
            strncpy(temp, response, BUFFER_SIZE - 1);
            temp[BUFFER_SIZE - 1] = '\0';

            char *fields[7];
            int   fc = 0;
            char *tok = strtok(temp, "|");
            while (tok && fc < 7) { fields[fc++] = tok; tok = strtok(NULL, "|"); }

            if (fc >= 6) {
                int msg_id = atoi(fields[1]);

                /* Slide the window if full — drop the oldest message */
                if (*count == CHAT_WINDOW_SIZE) {
                    memmove(&window[0], &window[1],
                            sizeof(ChatMessage) * (CHAT_WINDOW_SIZE - 1));
                    (*count)--;
                }

                ChatMessage *m = &window[(*count)++];
                m->id = msg_id;
                strncpy(m->sender,    fields[2], MAX_USERNAME  - 1);
                strncpy(m->text,      fields[4], MAX_MESSAGE   - 1);
                strncpy(m->timestamp, fields[5], MAX_TIMESTAMP - 1);
                m->sender[MAX_USERNAME  - 1] = '\0';
                m->text[MAX_MESSAGE     - 1] = '\0';
                m->timestamp[MAX_TIMESTAMP - 1] = '\0';

                if (msg_id > last_id) last_id = msg_id;
            }
        }
    }

    return last_id;
}

/* -----------------------------------------------------------------------
 * poll_new_messages
 * Sends FETCH with last_known_id > 0 (poll mode).
 * Appends any new messages to the window array.
 * Returns 1 if new messages arrived (caller should redraw), 0 if not.
 * --------------------------------------------------------------------- */
static int poll_new_messages(const char *partner,
                             ChatMessage window[], int *count,
                             int *last_known_id) {
    char request[BUFFER_SIZE], response[BUFFER_SIZE];
    int  got_new = 0;

    snprintf(request, BUFFER_SIZE, "FETCH|%s|%s|%d\n",
             session_user, partner, *last_known_id);
    send_request(request);

    while (1) {
        receive_response(response);
        if (strcmp(response, "END") == 0 || strlen(response) == 0) break;

        if (strncmp(response, "DATA|", 5) == 0) {
            char  temp[BUFFER_SIZE];
            strncpy(temp, response, BUFFER_SIZE - 1);
            temp[BUFFER_SIZE - 1] = '\0';

            char *fields[7];
            int   fc = 0;
            char *tok = strtok(temp, "|");
            while (tok && fc < 7) { fields[fc++] = tok; tok = strtok(NULL, "|"); }

            if (fc >= 6) {
                int msg_id = atoi(fields[1]);

                /* Slide the window if full */
                if (*count == CHAT_WINDOW_SIZE) {
                    memmove(&window[0], &window[1],
                            sizeof(ChatMessage) * (CHAT_WINDOW_SIZE - 1));
                    (*count)--;
                }

                ChatMessage *m = &window[(*count)++];
                m->id = msg_id;
                strncpy(m->sender,    fields[2], MAX_USERNAME  - 1);
                strncpy(m->text,      fields[4], MAX_MESSAGE   - 1);
                strncpy(m->timestamp, fields[5], MAX_TIMESTAMP - 1);
                m->sender[MAX_USERNAME  - 1] = '\0';
                m->text[MAX_MESSAGE     - 1] = '\0';
                m->timestamp[MAX_TIMESTAMP - 1] = '\0';

                if (msg_id > *last_known_id) *last_known_id = msg_id;
                got_new = 1;
            }
        }
    }

    return got_new;
}

/* -----------------------------------------------------------------------
 * handle_chat
 * Active chat session with a partner.
 *
 * Maintains a local ChatMessage window[CHAT_WINDOW_SIZE] array.
 * Screen is redrawn ONLY when the array changes — no flicker on idle.
 *
 * Flow:
 *   1. Initial load — fill array, draw screen
 *   2. Every 2 seconds — poll for new messages
 *      → new messages found: update array, redraw
 *      → nothing new: do nothing
 *   3. User types → send → poll immediately → redraw if needed
 *   4. /q → exit chat
 * --------------------------------------------------------------------- */
void handle_chat(char partner[]) {
    ChatMessage window[CHAT_WINDOW_SIZE];
    int         count        = 0;
    int         last_id      = 0;
    time_t      last_fetch   = 0;
    char        input[MAX_MESSAGE];

    /* Step 1 — initial load */
    last_id = load_initial_messages(partner, window, &count);
    redraw_chat(partner, window, count);
    ui_display_chat_input_prompt();

    while (1) {
        time_t now = time(NULL);

        /* Step 2 — poll every 2 seconds */
        if (now - last_fetch >= 2) {
            if (poll_new_messages(partner, window, &count, &last_id)) {
                /* Only redraw if something actually changed */
                redraw_chat(partner, window, count);
                ui_display_chat_input_prompt();
            }
            last_fetch = now;
        }

        /* Step 3 — check for user input */
#ifdef _WIN32
        if (_kbhit()) {
#else
        if (input_available()) {
#endif
            fgets(input, MAX_MESSAGE, stdin);
            sanitize_input(input);

            /* Step 4 — /q exits the chat */
            if (strcmp(input, "/q") == 0) break;

            if (strlen(input) > 0) {
                /* Send the message */
                char request[BUFFER_SIZE], response[BUFFER_SIZE];
                snprintf(request, BUFFER_SIZE, "SEND|%s|%s|%s\n",
                         session_user, partner, input);
                send_request(request);
                receive_response(response); /* consume the ack */

                /* Immediately poll so the sent message appears */
                if (poll_new_messages(partner, window,
                                      &count, &last_id)) {
                    redraw_chat(partner, window, count);
                    ui_display_chat_input_prompt();
                }
                last_fetch = time(NULL);
            }
        }

        SLEEP_MS(100);
    }
}

/* -----------------------------------------------------------------------
 * handle_inbox
 * --------------------------------------------------------------------- */
void handle_inbox() {
    char contacts[MAX_USERS][MAX_USERNAME];
    int  contact_count = 0;
    char request[BUFFER_SIZE], response[BUFFER_SIZE];
    char input[MAX_USERNAME],  partner[MAX_USERNAME];

    snprintf(request, BUFFER_SIZE, "CONTACTS|%s\n", session_user);
    send_request(request);

    while (1) {
        receive_response(response);
        if (strcmp(response, "END") == 0 || strlen(response) == 0) break;
        if (strncmp(response, "CONTACT|", 8) == 0 &&
            contact_count < MAX_USERS) {
            strncpy(contacts[contact_count], response + 8, MAX_USERNAME - 1);
            contacts[contact_count][MAX_USERNAME - 1] = '\0';
            contact_count++;
        }
    }

    ui_display_inbox(contacts, contact_count);

    printf("  > ");
    fgets(input, MAX_USERNAME, stdin);
    sanitize_input(input);

    int selection = atoi(input);
    if (selection > 0 && selection <= contact_count)
        strncpy(partner, contacts[selection - 1], MAX_USERNAME - 1);
    else
        strncpy(partner, input, MAX_USERNAME - 1);
    partner[MAX_USERNAME - 1] = '\0';

    if (strlen(partner) == 0) return;

    snprintf(request, BUFFER_SIZE, "SEARCH|%s|%s\n", session_user, partner);
    send_request(request);
    receive_response(response);

    if (strncmp(response, "ERROR", 5) == 0) {
        char *msg = strchr(response, '|');
        ui_error(msg ? msg + 1 : "User not found");
        ui_wait_for_enter();
        return;
    }

    handle_chat(partner);
}

/* -----------------------------------------------------------------------
 * display_main_menu / display_dashboard
 * --------------------------------------------------------------------- */
void display_main_menu() {
    int choice;
    ui_display_main_menu();
    if (scanf("%d", &choice) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        return;
    }
    while (getchar() != '\n');
    switch (choice) {
        case 1: handle_login();    break;
        case 2: handle_register(); break;
        case 3: app_running = 0;   break;
        default: break;
    }
}

void display_dashboard() {
    int choice;
    ui_display_dashboard(session_user);
    if (scanf("%d", &choice) != 1) {
        int c; while ((c = getchar()) != '\n' && c != EOF);
        return;
    }
    while (getchar() != '\n');
    switch (choice) {
        case 1: handle_inbox();      break;
        case 2: handle_search();     break;
        case 3: handle_deregister(); break;
        case 4: handle_logout();     break;
        default: break;
    }
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }
#endif

    int mode = 0;
    while (mode != 1 && mode != 2 && mode != 3) {
        printf("\nSelect connection mode:\n"
               "[1] Tailnet\n[2] Same LAN\n[3] Manual IP\nChoice: ");
        char input[16];
        if (!fgets(input, sizeof(input), stdin)) continue;
        mode = atoi(input);
        if (mode != 1 && mode != 2 && mode != 3)
            printf("Invalid choice. Please enter 1, 2, or 3.\n");
    }

    if (!initialize_server_address(mode)) {
        printf("Failed to determine server address.\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /* Open the one persistent connection for the entire app session */
    int sock = open_connection();
    if (sock == -1) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    session_sock = (SOCKET)sock;

    signal(SIGINT, handle_sigint);

    while (app_running) {
        if (is_logged_in)
            display_dashboard();
        else
            display_main_menu();
    }

    ui_clear_screen();
    printf("\n  Goodbye!\n\n");

    close_session();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}