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

// Forward declaration
void send_response(int client_socket, char response[]);

// Helper function to get the highest message ID
int get_last_message_id() {
    FILE *file = fopen(MESSAGES_FILE, "r");
    if (file == NULL) {
        return 0;
    }
    
    int max_id = 0;
    char line[BUFFER_SIZE];
    
    while (fgets(line, sizeof(line), file)) {
        int id;
        if (sscanf(line, "%d|", &id) == 1) {
            if (id > max_id) {
                max_id = id;
            }
        }
    }
    
    fclose(file);
    return max_id;
}

// Processes a SEND request by storing a new message
void send_message(int client_socket, char sender[], char receiver[], char text[]) {
    char response[BUFFER_SIZE];
    char timestamp[MAX_TIMESTAMP];
    
    // Sanitize inputs
    sanitize_input(sender);
    sanitize_input(receiver);
    sanitize_input(text);
    
    // Check if receiver exists
    if (validate_unique_user(receiver)) {
        // User doesn't exist
        snprintf(response, BUFFER_SIZE, "ERROR|User %s not found", receiver);
        send_response(client_socket, response);
        return;
    }
    
    // Generate timestamp
    generate_timestamp(timestamp);
    
    // Get new message ID
    int new_id = get_last_message_id() + 1;
    
    // Append message to file
    FILE *file = fopen(MESSAGES_FILE, "a");
    if (file == NULL) {
        snprintf(response, BUFFER_SIZE, "ERROR|Server error: Cannot access message database");
        send_response(client_socket, response);
        return;
    }
    
    fprintf(file, "%d|%s|%s|%s|%s\n", new_id, sender, receiver, text, timestamp);
    fclose(file);
    
    snprintf(response, BUFFER_SIZE, "SUCCESS|Message sent");
    send_response(client_socket, response);
}

// Processes a FETCH request by retrieving conversation messages
void fetch_messages(int client_socket, char sender[], char receiver[]) {
    Message messages[MAX_MESSAGES];
    int msg_count = 0;
    char response[BUFFER_SIZE];
    
    FILE *file = fopen(MESSAGES_FILE, "r");
    if (file == NULL) {
        // No messages file, send END immediately
        send_response(client_socket, "END");
        return;
    }
    
    char line[BUFFER_SIZE];
    
    // Read all messages for this conversation pair
    while (fgets(line, sizeof(line), file) && msg_count < MAX_MESSAGES) {
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        Message msg;
        char *token;
        char temp[BUFFER_SIZE];
        strncpy(temp, line, BUFFER_SIZE - 1);
        temp[BUFFER_SIZE - 1] = '\0';
        
        // Parse: id|sender|receiver|text|timestamp
        token = strtok(temp, "|");
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
        
        // Filter: only messages between sender and receiver (in either direction)
        if ((strcmp(msg.sender, sender) == 0 && strcmp(msg.receiver, receiver) == 0) ||
            (strcmp(msg.sender, receiver) == 0 && strcmp(msg.receiver, sender) == 0)) {
            messages[msg_count++] = msg;
        }
    }
    fclose(file);
    
    // Determine starting index for last 20 messages
    int start = 0;
    if (msg_count > 20) {
        start = msg_count - 20;
    }
    
    // Send each message as a DATA line
    for (int i = start; i < msg_count; i++) {
        snprintf(response, BUFFER_SIZE, "DATA|%d|%s|%s|%s|%s",
                 messages[i].id,
                 messages[i].sender,
                 messages[i].receiver,
                 messages[i].text,
                 messages[i].timestamp);
        send_response(client_socket, response);
    }
    
    // Send END to signal completion
    send_response(client_socket, "END");
}

// Processes a CONTACTS request by retrieving list of chat partners
void fetch_contacts(int client_socket, char username[]) {
    char contacts[MAX_USERS][MAX_USERNAME];
    int contact_count = 0;
    char response[BUFFER_SIZE];
    
    FILE *file = fopen(MESSAGES_FILE, "r");
    if (file == NULL) {
        // No messages file, send END immediately
        send_response(client_socket, "END");
        return;
    }
    
    char line[BUFFER_SIZE];
    
    // Read all messages and extract unique contacts
    while (fgets(line, sizeof(line), file)) {
        char sender[MAX_USERNAME];
        char receiver[MAX_USERNAME];
        char temp[BUFFER_SIZE];
        strncpy(temp, line, BUFFER_SIZE - 1);
        temp[BUFFER_SIZE - 1] = '\0';
        
        // Parse: id|sender|receiver|...
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
        
        // Determine the contact (the other person in the conversation)
        char *contact = NULL;
        if (strcmp(sender, username) == 0) {
            contact = receiver;
        } else if (strcmp(receiver, username) == 0) {
            contact = sender;
        }
        
        if (contact != NULL) {
            // Check if contact already in list
            int found = 0;
            for (int i = 0; i < contact_count; i++) {
                if (strcmp(contacts[i], contact) == 0) {
                    found = 1;
                    break;
                }
            }
            
            // Add if not found and not at capacity
            if (!found && contact_count < MAX_USERS) {
                strncpy(contacts[contact_count], contact, MAX_USERNAME - 1);
                contacts[contact_count][MAX_USERNAME - 1] = '\0';
                contact_count++;
            }
        }
    }
    fclose(file);
    
    // Send each contact as a CONTACT line
    for (int i = 0; i < contact_count; i++) {
        snprintf(response, BUFFER_SIZE, "CONTACT|%s", contacts[i]);
        send_response(client_socket, response);
    }
    
    // Send END to signal completion
    send_response(client_socket, "END");
}
