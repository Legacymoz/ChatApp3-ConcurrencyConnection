#ifndef UTILS_H
#define UTILS_H

// Generates the current timestamp in HH:MM format
void generate_timestamp(char buffer[]);

// Strips unwanted characters from input (newlines, pipes)
void sanitize_input(char buffer[]);

// Returns the size of a file in bytes
long fetch_byte_count(char filename[]);

// Parses a pipe-delimited request string into fields
void parse_request(char buffer[], char fields[][256], int *count);

// Creates a file if it does not exist
int create_file_if_missing(char filename[]);

#endif // UTILS_H
