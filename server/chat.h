#ifndef CHAT_H
#define CHAT_H

// Processes a SEND request by storing a new message
void send_message(int client_socket, char sender[], char receiver[], char text[]);

// Processes a FETCH request by retrieving conversation messages
void fetch_messages(int client_socket, char sender[], char receiver[]);

// Processes a CONTACTS request by retrieving list of chat partners
void fetch_contacts(int client_socket, char username[]);

// Helper function to get the highest message ID
int get_last_message_id();

#endif // CHAT_H
