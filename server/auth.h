#ifndef AUTH_H
#define AUTH_H

// Processes a REGISTER request by creating a new user account
void register_user(int client_socket, char username[], char password[]);

// Processes a LOGIN request by verifying credentials
int authenticate_user(int client_socket, char username[], char password[]);

// Checks if a username already exists in the registry
int validate_unique_user(char username[]);

// Processes a DEREGISTER request by removing a user account
void deregister_user(int client_socket, char username[]);

// Processes a SEARCH request by looking up a target username
void search_user(int client_socket, char username[], char target[]);

// Processes a LOGOUT request
void logout_user(int client_socket, char username[]);

#endif // AUTH_H
