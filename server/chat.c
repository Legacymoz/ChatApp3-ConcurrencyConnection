#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
#endif

#include "chat.h"
#include "auth.h"
#include "utils.h"
#include "../shared/models.h"

#define MESSAGES_FILE "../data/messages.txt"

extern void lock_messages_file();
extern void unlock_messages_file();
void send_response(int client_socket, char response[]);

/* -----------------------------------------------------------------------
 * get_last_message_id
 * Scans messages.txt for the highest existing message ID.
 * Caller must hold the messages mutex before calling this.
 * --------------------------------------------------------------------- */
static int get_last_message_id() {
    FILE *file = fopen(MESSAGES_FILE, "r");
    if (file == NULL) return 0;

    int  max_id = 0;
    char line[BUFFER_SIZE];

    while (fgets(line, sizeof(line), file)) {
        int id;
        if (sscanf(line, "%d|", &id) == 1 && id > max_id)
            max_id = id;
    }

    fclose(file);
    return max_id;
}

/* -----------------------------------------------------------------------
 * send_message
 * Stores a new message. Logs sender, receiver, and message text so the
 * server console shows all chat activity in real time.
 * Mutex locked for the ID fetch + file append to prevent duplicate IDs
 * from concurrent sends.
 * --------------------------------------------------------------------- */
void send_message(int client_socket, char sender[],
                  char receiver[], char text[]) {
    char response[BUFFER_SIZE];
    char timestamp[MAX_TIMESTAMP];

    sanitize_input(sender);
    sanitize_input(receiver);
    sanitize_input(text);

    if (validate_unique_user(receiver)) {
        printf("[SEND] FAILED - '%s' tried to message '%s' "
               "who does not exist\n", sender, receiver);
        snprintf(response, BUFFER_SIZE,
                 "ERROR|User %s not found", receiver);
        send_response(client_socket, response);
        return;
    }

    generate_timestamp(timestamp);

    lock_messages_file();

    int new_id = get_last_message_id() + 1;

    FILE *file = fopen(MESSAGES_FILE, "a");
    if (file == NULL) {
        unlock_messages_file();
        printf("[SEND] ERROR - cannot open messages file\n");
        send_response(client_socket,
                      "ERROR|Server error: Cannot access message database");
        return;
    }

    fprintf(file, "%d|%s|%s|%s|%s\n",
            new_id, sender, receiver, text, timestamp);
    fclose(file);
    unlock_messages_file();

    /* Log the full message so the server shows all chat traffic */
    printf("[SEND] #%d | %s - %s | \"%s\"\n",
           new_id, sender, receiver, text);

    send_response(client_socket, "SUCCESS|Message sent");
}

/* -----------------------------------------------------------------------
 * fetch_messages
 * Returns messages between sender and receiver with ID > last_known_id.
 *
 * last_known_id == 0  - initial load, return the last 20 messages
 * last_known_id  > 0  - poll, return only messages newer than that ID
 *
 * If nothing new is found during a poll, only END is sent - the client
 * detects this and skips the screen redraw entirely.
 *
 * Logs fetches only on initial load to avoid flooding the console with
 * poll noise every 2 seconds.
 * --------------------------------------------------------------------- */
void fetch_messages(int client_socket, char sender[],
                    char receiver[], int last_known_id) {
    Message messages[MAX_MESSAGES];
    int     msg_count = 0;
    char    response[BUFFER_SIZE];
    char    line[BUFFER_SIZE];

    /* Only log initial loads - polls are too frequent to log every one */
    if (last_known_id == 0) {
        printf("[FETCH] '%s' opened conversation with '%s'\n",
               sender, receiver);
    }

    lock_messages_file();

    FILE *file = fopen(MESSAGES_FILE, "r");
    if (file == NULL) {
        unlock_messages_file();
        send_response(client_socket, "END");
        return;
    }

    while (fgets(line, sizeof(line), file) && msg_count < MAX_MESSAGES) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        Message msg;
        char    temp[BUFFER_SIZE];
        strncpy(temp, line, BUFFER_SIZE - 1);
        temp[BUFFER_SIZE - 1] = '\0';

        char *token = strtok(temp, "|");
        if (!token) continue;
        msg.id = atoi(token);

        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(msg.sender, token, MAX_USERNAME - 1);
        msg.sender[MAX_USERNAME - 1] = '\0';

        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(msg.receiver, token, MAX_USERNAME - 1);
        msg.receiver[MAX_USERNAME - 1] = '\0';

        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(msg.text, token, MAX_MESSAGE - 1);
        msg.text[MAX_MESSAGE - 1] = '\0';

        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(msg.timestamp, token, MAX_TIMESTAMP - 1);
        msg.timestamp[MAX_TIMESTAMP - 1] = '\0';

        /* Must be part of this conversation */
        int in_conversation =
            (strcmp(msg.sender,   sender)   == 0 &&
             strcmp(msg.receiver, receiver) == 0) ||
            (strcmp(msg.sender,   receiver) == 0 &&
             strcmp(msg.receiver, sender)   == 0);

        if (!in_conversation) continue;

        /* For polls: skip messages the client already has */
        if (last_known_id > 0 && msg.id <= last_known_id) continue;

        messages[msg_count++] = msg;
    }

    fclose(file);
    unlock_messages_file();

    /* Initial load: cap at last 20 messages */
    int start = 0;
    if (last_known_id == 0 && msg_count > 20)
        start = msg_count - 20;

    if (last_known_id == 0) {
        printf("[FETCH] '%s' opened conversation with '%s'\n",sender, receiver);
    } else {
        printf("[FETCH] POLL - %d new message(s) for '%s' from '%s'\n",msg_count, receiver, sender);
    }
    for (int i = start; i < msg_count; i++) {
        snprintf(response, BUFFER_SIZE, "DATA|%d|%s|%s|%s|%s",
                 messages[i].id,
                 messages[i].sender,
                 messages[i].receiver,
                 messages[i].text,
                 messages[i].timestamp);
        send_response(client_socket, response);
    }

    send_response(client_socket, "END");
}

/* -----------------------------------------------------------------------
 * fetch_contacts
 * Returns unique contacts for a user. No per-call logging - this is
 * called from the inbox screen and is not chat traffic.
 * --------------------------------------------------------------------- */
void fetch_contacts(int client_socket, char username[]) {
    char contacts[MAX_USERS][MAX_USERNAME];
    int  contact_count = 0;
    char response[BUFFER_SIZE];
    char line[BUFFER_SIZE];

    lock_messages_file();

    FILE *file = fopen(MESSAGES_FILE, "r");
    if (file == NULL) {
        unlock_messages_file();
        send_response(client_socket, "END");
        return;
    }

    while (fgets(line, sizeof(line), file)) {
        char sender[MAX_USERNAME];
        char receiver[MAX_USERNAME];
        char temp[BUFFER_SIZE];
        strncpy(temp, line, BUFFER_SIZE - 1);
        temp[BUFFER_SIZE - 1] = '\0';

        char *token = strtok(temp, "|");
        if (!token) continue;

        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(sender, token, MAX_USERNAME - 1);
        sender[MAX_USERNAME - 1] = '\0';

        token = strtok(NULL, "|");
        if (!token) continue;
        strncpy(receiver, token, MAX_USERNAME - 1);
        receiver[MAX_USERNAME - 1] = '\0';

        char *contact = NULL;
        if (strcmp(sender,   username) == 0) contact = receiver;
        else if (strcmp(receiver, username) == 0) contact = sender;

        if (contact != NULL) {
            int found = 0;
            for (int i = 0; i < contact_count; i++) {
                if (strcmp(contacts[i], contact) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && contact_count < MAX_USERS) {
                strncpy(contacts[contact_count], contact, MAX_USERNAME - 1);
                contacts[contact_count][MAX_USERNAME - 1] = '\0';
                contact_count++;
            }
        }
    }

    fclose(file);
    unlock_messages_file();

    for (int i = 0; i < contact_count; i++) {
        snprintf(response, BUFFER_SIZE, "CONTACT|%s", contacts[i]);
        send_response(client_socket, response);
    }

    send_response(client_socket, "END");
}