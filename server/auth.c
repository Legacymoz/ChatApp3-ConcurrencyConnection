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

extern void lock_users_file();
extern void unlock_users_file();
void send_response(int client_socket, char response[]);

/* -----------------------------------------------------------------------
 * validate_unique_user
 * Returns 1 if username is free, 0 if it already exists.
 * Acquires the users mutex internally for the full file scan.
 * --------------------------------------------------------------------- */
int validate_unique_user(char username[]) {
    char line[256];
    char stored_username[MAX_USERNAME];

    lock_users_file();

    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        unlock_users_file();
        return 1;
    }

    while (fgets(line, sizeof(line), file)) {
        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int len = (int)(delimiter - line);
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';
            if (strcmp(stored_username, username) == 0) {
                fclose(file);
                unlock_users_file();
                return 0;
            }
        }
    }

    fclose(file);
    unlock_users_file();
    return 1;
}

/* -----------------------------------------------------------------------
 * register_user
 * Validates inputs, checks uniqueness, appends to users file.
 * Logs the outcome (success or reason for failure) to the server console.
 * --------------------------------------------------------------------- */
void register_user(int client_socket, char username[], char password[]) {
    char response[BUFFER_SIZE];

    sanitize_input(username);
    sanitize_input(password);

    if (strlen(username) == 0 || strlen(password) == 0) {
        printf("[REGISTER] FAILED - empty username or password\n");
        send_response(client_socket,
                      "ERROR|Username and password cannot be empty");
        return;
    }

    if (!validate_unique_user(username)) {
        printf("[REGISTER] FAILED - username '%s' is already taken\n",
               username);
        send_response(client_socket, "ERROR|Username already taken");
        return;
    }

    lock_users_file();

    FILE *file = fopen(USERS_FILE, "a");
    if (file == NULL) {
        unlock_users_file();
        printf("[REGISTER] ERROR - cannot open users file for writing\n");
        send_response(client_socket,
                      "ERROR|Server error: Cannot access user database");
        return;
    }

    fprintf(file, "%s|%s\n", username, password);
    fclose(file);
    unlock_users_file();

    printf("[REGISTER] SUCCESS - new account created for '%s'\n", username);
    snprintf(response, BUFFER_SIZE, "SUCCESS|Account created for %s",
             username);
    send_response(client_socket, response);
}

/* -----------------------------------------------------------------------
 * authenticate_user
 * Verifies credentials against users file.
 * Logs success (with username) or failure (bad credentials / file error).
 * --------------------------------------------------------------------- */
int authenticate_user(int client_socket, char username[], char password[]) {
    char response[BUFFER_SIZE];
    char line[256];
    char stored_username[MAX_USERNAME];
    char stored_password[MAX_PASSWORD];

    sanitize_input(username);
    sanitize_input(password);

    lock_users_file();

    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        unlock_users_file();
        printf("[LOGIN] ERROR - cannot open users file for '%s'\n", username);
        send_response(client_socket, "ERROR|Invalid username or password");
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int len = (int)(delimiter - line);
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';
            strncpy(stored_password, delimiter + 1, MAX_PASSWORD - 1);
            stored_password[MAX_PASSWORD - 1] = '\0';

            if (strcmp(stored_username, username) == 0 &&
                strcmp(stored_password, password) == 0) {
                fclose(file);
                unlock_users_file();
                printf("[LOGIN] SUCCESS - '%s' is now logged in\n", username);
                snprintf(response, BUFFER_SIZE,
                         "SUCCESS|Welcome back %s", username);
                send_response(client_socket, response);
                return 1;
            }
        }
    }

    fclose(file);
    unlock_users_file();

    printf("[LOGIN] FAILED - bad credentials for '%s'\n", username);
    send_response(client_socket, "ERROR|Invalid username or password");
    return 0;
}

/* -----------------------------------------------------------------------
 * deregister_user
 * Reads all users except the target into memory, rewrites the file.
 * Logs whether the user was actually found and deleted.
 * Mutex held for the full read-then-write to prevent partial file states.
 * --------------------------------------------------------------------- */
void deregister_user(int client_socket, char username[]) {
    char response[BUFFER_SIZE];
    User users[MAX_USERS];
    int  user_count  = 0;
    int  user_found  = 0;
    char line[256];

    sanitize_input(username);

    lock_users_file();

    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        unlock_users_file();
        printf("[DEREGISTER] ERROR - cannot open users file for '%s'\n",
               username);
        send_response(client_socket,
                      "ERROR|Server error: Cannot access user database");
        return;
    }

    while (fgets(line, sizeof(line), file) && user_count < MAX_USERS) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';

        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int  len = (int)(delimiter - line);
            char stored_username[MAX_USERNAME];
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';

            if (strcmp(stored_username, username) == 0) {
                /* Skip this user - they are being deleted */
                user_found = 1;
                continue;
            }

            strncpy(users[user_count].username,
                    stored_username, MAX_USERNAME - 1);
            strncpy(users[user_count].password,
                    delimiter + 1,  MAX_PASSWORD - 1);
            users[user_count].username[MAX_USERNAME - 1] = '\0';
            users[user_count].password[MAX_PASSWORD - 1] = '\0';
            user_count++;
        }
    }
    fclose(file);

    if (!user_found) {
        unlock_users_file();
        printf("[DEREGISTER] FAILED - user '%s' not found in database\n",
               username);
        send_response(client_socket, "ERROR|User not found");
        return;
    }

    file = fopen(USERS_FILE, "w");
    if (file == NULL) {
        unlock_users_file();
        printf("[DEREGISTER] ERROR - cannot rewrite users file\n");
        send_response(client_socket,
                      "ERROR|Server error: Cannot update user database");
        return;
    }

    for (int i = 0; i < user_count; i++)
        fprintf(file, "%s|%s\n", users[i].username, users[i].password);

    fclose(file);
    unlock_users_file();

    printf("[DEREGISTER] SUCCESS - account '%s' permanently deleted\n",
           username);
    send_response(client_socket, "SUCCESS|Account permanently deleted");
}

/* -----------------------------------------------------------------------
 * search_user
 * Looks up a target username. Logs who searched for whom and the result.
 * --------------------------------------------------------------------- */
void search_user(int client_socket, char username[], char target[]) {
    char response[BUFFER_SIZE];

    sanitize_input(username);
    sanitize_input(target);

    if (strcmp(username, target) == 0) {
        printf("[SEARCH] FAILED - '%s' searched for themselves\n", username);
        send_response(client_socket, "ERROR|You cannot search for yourself");
        return;
    }

    if (!validate_unique_user(target)) {
        /* validate_unique_user returns 0 when user EXISTS */
        printf("[SEARCH] '%s' searched for '%s' - FOUND\n",
               username, target);
        snprintf(response, BUFFER_SIZE,
                 "SUCCESS|User %s is registered and available for chat",
                 target);
    } else {
        printf("[SEARCH] '%s' searched for '%s' - NOT FOUND\n",
               username, target);
        snprintf(response, BUFFER_SIZE, "ERROR|User %s not found", target);
    }

    send_response(client_socket, response);
}

/* -----------------------------------------------------------------------
 * logout_user
 * Sends goodbye response. handle_client() breaks the session loop after
 * this returns.
 * --------------------------------------------------------------------- */
void logout_user(int client_socket, char username[]) {
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "SUCCESS|Goodbye %s", username);
    send_response(client_socket, response);
}