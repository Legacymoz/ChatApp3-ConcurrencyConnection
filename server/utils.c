#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "utils.h"
#include "../shared/models.h"

// Generates the current timestamp in HH:MM format
void generate_timestamp(char buffer[]) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, 10, "%H:%M", t);
}

// Strips unwanted characters from input (newlines, pipes)
void sanitize_input(char buffer[]) {
    // Remove trailing newline
    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
    }
    
    // Remove carriage return
    char *cr = strchr(buffer, '\r');
    if (cr) {
        *cr = '\0';
    }
    
    // Remove pipe characters to prevent format corruption
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '|') {
            buffer[i] = ' ';
        }
    }
}

// Returns the size of a file in bytes
long fetch_byte_count(char filename[]) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return 0;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return size;
}

// Parses a pipe-delimited request string into fields
void parse_request(char buffer[], char fields[][256], int *count) {
    *count = 0;
    char temp[BUFFER_SIZE];
    strncpy(temp, buffer, BUFFER_SIZE - 1);
    temp[BUFFER_SIZE - 1] = '\0';
    
    // Remove trailing newline/carriage return
    char *newline = strchr(temp, '\n');
    if (newline) *newline = '\0';
    char *cr = strchr(temp, '\r');
    if (cr) *cr = '\0';
    
    char *token = strtok(temp, "|");
    while (token != NULL && *count < 10) {
        strncpy(fields[*count], token, 255);
        fields[*count][255] = '\0';
        (*count)++;
        token = strtok(NULL, "|");
    }
}

// Creates a file if it does not exist
int create_file_if_missing(char filename[]) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        // File doesn't exist, create it
        file = fopen(filename, "w");
        if (file != NULL) {
            fclose(file);
            return 1; // File was created
        }
        return -1; // Error creating file
    }
    fclose(file);
    return 0; // File already existed
}
