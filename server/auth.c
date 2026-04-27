#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>   /* flock() */
#include <sys/socket.h>
#include <unistd.h>     /* getpid() */

#include "auth.h"
#include "utils.h"
#include "../shared/models.h"

#define USERS_FILE "../data/users.txt"

void send_response(int client_socket, char response[]);

/* -----------------------------------------------------------------------
 * validate_unique_user
 * --------------------------------------------------------------------- */
int validate_unique_user(char username[]) {
    char line[256];
    char stored_username[MAX_USERNAME];

    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) return 1;

    flock(fileno(file), LOCK_EX);

    while (fgets(line, sizeof(line), file)) {
        char *delimiter = strchr(line, '|');
        if (delimiter) {
            int len = (int)(delimiter - line);
            strncpy(stored_username, line, len);
            stored_username[len] = '\0';
            if (strcmp(stored_username, username) == 0) {
                flock(fileno(file), LOCK_UN);
                fclose(file);
                return 0;
            }
        }
    }

    flock(fileno(file), LOCK_UN);
    fclose(file);
    return 1;
}

/* -----------------------------------------------------------------------
 * register_user
 * --------------------------------------------------------------------- */
void register_user(int client_socket, char username[], char password[]) {
    char response[BUFFER_SIZE];

    sanitize_input(username);
    sanitize_input(password);

    if (strlen(username) == 0 || strlen(password) == 0) {
        printf("[Child PID %d][REGISTER] FAILED - empty username or password\n", getpid());
        send_response(client_socket, "ERROR|Username and password cannot be empty");
        return;
    }

    if (!validate_unique_user(username)) {
        printf("[Child PID %d][REGISTER] FAILED - username '%s' is already taken\n", getpid(), username);
        send_response(client_socket, "ERROR|Username already taken");
        return;
    }

    FILE *file = fopen(USERS_FILE, "a");
    if (file == NULL) {
        printf("[Child PID %d][REGISTER] ERROR - cannot open users file for writing\n", getpid());
        send_response(client_socket, "ERROR|Server error: Cannot access user database");
        return;
    }

    flock(fileno(file), LOCK_EX);
    fprintf(file, "%s|%s\n", username, password);
    flock(fileno(file), LOCK_UN);
    fclose(file);

    printf("[Child PID %d][REGISTER] SUCCESS - new account created for '%s'\n", getpid(), username);
    snprintf(response, BUFFER_SIZE, "SUCCESS|Account created for %s", username);
    send_response(client_socket, response);
}

/* -----------------------------------------------------------------------
 * authenticate_user
 * --------------------------------------------------------------------- */
int authenticate_user(int client_socket, char username[], char password[]) {
    char response[BUFFER_SIZE];
    char line[256];
    char stored_username[MAX_USERNAME];
    char stored_password[MAX_PASSWORD];

    sanitize_input(username);
    sanitize_input(password);

    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        printf("[Child PID %d][LOGIN] ERROR - cannot open users file for '%s'\n", getpid(), username);
        send_response(client_socket, "ERROR|Invalid username or password");
        return 0;
    }

    flock(fileno(file), LOCK_EX);

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
                flock(fileno(file), LOCK_UN);
                fclose(file);
                printf("[Child PID %d][LOGIN] SUCCESS - '%s' is now logged in\n", getpid(), username);
                snprintf(response, BUFFER_SIZE, "SUCCESS|Welcome back %s", username);
                send_response(client_socket, response);
                return 1;
            }
        }
    }

    flock(fileno(file), LOCK_UN);
    fclose(file);

    printf("[Child PID %d][LOGIN] FAILED - bad credentials for '%s'\n", getpid(), username);
    send_response(client_socket, "ERROR|Invalid username or password");
    return 0;
}

/* -----------------------------------------------------------------------
 * deregister_user
 * --------------------------------------------------------------------- */
void deregister_user(int client_socket, char username[]) {
    char response[BUFFER_SIZE];
    User users[MAX_USERS];
    int  user_count = 0;
    int  user_found = 0;
    char line[256];

    sanitize_input(username);

    FILE *file = fopen(USERS_FILE, "r");
    if (file == NULL) {
        printf("[Child PID %d][DEREGISTER] ERROR - cannot open users file for '%s'\n", 
               getpid(), username);
        send_response(client_socket,
                      "ERROR|Server error: Cannot access user database");
        return;
    }

    flock(fileno(file), LOCK_EX);

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
                user_found = 1;
                continue;
            }

            strncpy(users[user_count].username, stored_username, MAX_USERNAME - 1);
            strncpy(users[user_count].password, delimiter + 1,  MAX_PASSWORD - 1);
            users[user_count].username[MAX_USERNAME - 1] = '\0';
            users[user_count].password[MAX_PASSWORD - 1] = '\0';
            user_count++;
        }
    }

    flock(fileno(file), LOCK_UN);
    fclose(file);

    if (!user_found) {
        printf("[Child PID %d][DEREGISTER] FAILED - user '%s' not found in database\n", 
               getpid(), username);
        send_response(client_socket, "ERROR|User not found");
        return;
    }

    file = fopen(USERS_FILE, "w");
    if (file == NULL) {
        printf("[Child PID %d][DEREGISTER] ERROR - cannot rewrite users file\n", getpid());
        send_response(client_socket,
                      "ERROR|Server error: Cannot update user database");
        return;
    }

    flock(fileno(file), LOCK_EX);
    for (int i = 0; i < user_count; i++)
        fprintf(file, "%s|%s\n", users[i].username, users[i].password);
    flock(fileno(file), LOCK_UN);
    fclose(file);

    printf("[Child PID %d][DEREGISTER] SUCCESS - account '%s' permanently deleted\n", 
           getpid(), username);
    send_response(client_socket, "SUCCESS|Account permanently deleted");
}

/* -----------------------------------------------------------------------
 * search_user
 * --------------------------------------------------------------------- */
void search_user(int client_socket, char username[], char target[]) {
    char response[BUFFER_SIZE];

    sanitize_input(username);
    sanitize_input(target);

    if (strcmp(username, target) == 0) {
        printf("[Child PID %d][SEARCH] FAILED - '%s' searched for themselves\n", getpid(), username);
        send_response(client_socket, "ERROR|You cannot search for yourself");
        return;
    }

    if (!validate_unique_user(target)) {
        printf("[Child PID %d][SEARCH] '%s' searched for '%s' - FOUND\n", getpid(), username, target);
        snprintf(response, BUFFER_SIZE, "SUCCESS|User %s is registered and available for chat", target);
    } else {
        printf("[Child PID %d][SEARCH] '%s' searched for '%s' - NOT FOUND\n", getpid(), username, target);
        snprintf(response, BUFFER_SIZE, "ERROR|User %s not found", target);
    }

    send_response(client_socket, response);
}

/* -----------------------------------------------------------------------
 * logout_user
 * --------------------------------------------------------------------- */
void logout_user(int client_socket, char username[]) {
    char response[BUFFER_SIZE];
    printf("[Child PID %d][LOGOUT] SUCCESS - '%s' logged out\n", getpid(), username);
    snprintf(response, BUFFER_SIZE, "SUCCESS|Goodbye %s", username);
    send_response(client_socket, response);
}