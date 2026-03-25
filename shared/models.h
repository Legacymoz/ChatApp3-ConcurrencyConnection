#ifndef MODELS_H
#define MODELS_H

#define MAX_USERNAME 50
#define MAX_PASSWORD 50
#define MAX_MESSAGE 512
#define MAX_TIMESTAMP 10
#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_USERS 100
#define MAX_MESSAGES 1000

// User structure for storing account information
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} User;

// Message structure for storing chat messages
typedef struct {
    int id;
    char sender[MAX_USERNAME];
    char receiver[MAX_USERNAME];
    char text[MAX_MESSAGE];
    char timestamp[MAX_TIMESTAMP];
} Message;

#endif // MODELS_H
