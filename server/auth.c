#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
#endif

#include "auth.h"
#include "utils.h"
#include "../shared/models.h"

#define USERS_FILE "../data/users.txt"

// Forward declaration
void send_response(int client_socket, char response[]);

// Checks if a username already exists in the registry
int validate_unique_user(char username[]) {
    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        return 1; // File doesn't exist, username is unique
    }
    
    char line[256];
    char stored_username[MAX_USERNAME];
    
    while (fgets(line, sizeof(line), file)) {
        // Parse username from line (format: username|password)
        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int len = delimiter - line;
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';
            
            if (strcmp(stored_username, username) == 0) {
                fclose(file);
                return 0; // Username exists, not unique
            }
        }
    }
    
    fclose(file);
    return 1; // Username is unique
}

// Processes a REGISTER request by creating a new user account
void register_user(int client_socket, char username[], char password[]) {
    char response[BUFFER_SIZE];
    
    // Sanitize inputs
    sanitize_input(username);
    sanitize_input(password);
    
    // Check for empty inputs
    if (strlen(username) == 0 || strlen(password) == 0) {
        snprintf(response, BUFFER_SIZE, "ERROR|Username and password cannot be empty");
        send_response(client_socket, response);
        return;
    }
    
    // Check if username already exists
    if (!validate_unique_user(username)) {
        snprintf(response, BUFFER_SIZE, "ERROR|Username already taken");
        send_response(client_socket, response);
        return;
    }
    
    // Append new user to file
    FILE *file = fopen(USERS_FILE, "a");
    if (file == NULL) {
        snprintf(response, BUFFER_SIZE, "ERROR|Server error: Cannot access user database");
        send_response(client_socket, response);
        return;
    }
    
    fprintf(file, "%s|%s\n", username, password);
    fclose(file);
    
    snprintf(response, BUFFER_SIZE, "SUCCESS|Account created for %s", username);
    send_response(client_socket, response);
}

// Processes a LOGIN request by verifying credentials
int authenticate_user(int client_socket, char username[], char password[]) {
    char response[BUFFER_SIZE];
    
    // Sanitize inputs
    sanitize_input(username);
    sanitize_input(password);
    
    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        snprintf(response, BUFFER_SIZE, "ERROR|Invalid username or password");
        send_response(client_socket, response);
        return 0;
    }
    
    char line[256];
    char stored_username[MAX_USERNAME];
    char stored_password[MAX_PASSWORD];
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // Parse username and password (format: username|password)
        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int len = delimiter - line;
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';
            strncpy(stored_password, delimiter + 1, MAX_PASSWORD - 1);
            stored_password[MAX_PASSWORD - 1] = '\0';
            
            if (strcmp(stored_username, username) == 0 && 
                strcmp(stored_password, password) == 0) {
                fclose(file);
                snprintf(response, BUFFER_SIZE, "SUCCESS|Welcome back %s", username);
                send_response(client_socket, response);
                return 1;
            }
        }
    }
    
    fclose(file);
    snprintf(response, BUFFER_SIZE, "ERROR|Invalid username or password");
    send_response(client_socket, response);
    return 0;
}

// Processes a DEREGISTER request by removing a user account
void deregister_user(int client_socket, char username[]) {
    char response[BUFFER_SIZE];
    User users[MAX_USERS];
    int user_count = 0;
    
    sanitize_input(username);
    
    // Read all users except the one to delete
    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        snprintf(response, BUFFER_SIZE, "ERROR|Server error: Cannot access user database");
        send_response(client_socket, response);
        return;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file) && user_count < MAX_USERS) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int len = delimiter - line;
            char stored_username[MAX_USERNAME];
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';
            
            // Skip the user being deleted
            if (strcmp(stored_username, username) != 0) {
                strncpy(users[user_count].username, stored_username, MAX_USERNAME - 1);
                strncpy(users[user_count].password, delimiter + 1, MAX_PASSWORD - 1);
                users[user_count].username[MAX_USERNAME - 1] = '\0';
                users[user_count].password[MAX_PASSWORD - 1] = '\0';
                user_count++;
            }
        }
    }
    fclose(file);
    
    // Rewrite the file without the deleted user
    file = fopen(USERS_FILE, "w");
    if (file == NULL) {
        snprintf(response, BUFFER_SIZE, "ERROR|Server error: Cannot update user database");
        send_response(client_socket, response);
        return;
    }
    
    for (int i = 0; i < user_count; i++) {
        fprintf(file, "%s|%s\n", users[i].username, users[i].password);
    }
    fclose(file);
    
    snprintf(response, BUFFER_SIZE, "SUCCESS|Account permanently deleted");
    send_response(client_socket, response);
}

// Processes a SEARCH request by looking up a target username
void search_user(int client_socket, char username[], char target[]) {
    char response[BUFFER_SIZE];
    
    sanitize_input(username);
    sanitize_input(target);
    
    // Check for self-search
    if (strcmp(username, target) == 0) {
        snprintf(response, BUFFER_SIZE, "ERROR|You cannot search for yourself");
        send_response(client_socket, response);
        return;
    }
    
    // Check if target exists
    if (!validate_unique_user(target)) {
        // User exists (validate_unique_user returns 0 if exists)
        snprintf(response, BUFFER_SIZE, "SUCCESS|User %s is registered and available for chat", target);
    } else {
        snprintf(response, BUFFER_SIZE, "ERROR|User %s not found", target);
    }
    
    send_response(client_socket, response);
}

// Processes a LOGOUT request
void logout_user(int client_socket, char username[]) {
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "SUCCESS|Goodbye %s", username);
    send_response(client_socket, response);
}
