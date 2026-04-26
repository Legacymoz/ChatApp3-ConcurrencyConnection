#ifndef CHAT_H
#define CHAT_H

// Processes a SEND request by storing a new message
void send_message(int client_socket, char sender[], char receiver[], char text[]);

// Processes a FETCH request by retrieving conversation messages
// Updated to include last_known_id to match chat.c and server.c
void fetch_messages(int client_socket, char sender[], char receiver[], int last_known_id);

// Processes a CONTACTS request by retrieving list of chat partners
void fetch_contacts(int client_socket, char username[]);

// get_last_message_id was removed from here because it is private (static) to chat.c

#endif // CHAT_H