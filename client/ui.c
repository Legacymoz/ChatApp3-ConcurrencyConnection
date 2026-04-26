#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

#define HEADER_WIDTH 50

// Clears the terminal screen
void ui_clear_screen() {
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif
}

// Renders a bordered header
void ui_display_header(const char title[]) {
    int title_len = strlen(title);
    int padding = (HEADER_WIDTH - title_len - 2) / 2;
    
    // Top border
    printf("\n");
    for (int i = 0; i < HEADER_WIDTH; i++) printf("=");
    printf("\n");
    
    // Title with padding
    printf("|");
    for (int i = 0; i < padding; i++) printf(" ");
    printf("%s", title);
    for (int i = 0; i < HEADER_WIDTH - padding - title_len - 2; i++) printf(" ");
    printf("|\n");
    
    // Bottom border
    for (int i = 0; i < HEADER_WIDTH; i++) printf("=");
    printf("\n\n");
}

// Renders the public facing main menu
void ui_display_main_menu() {
    ui_clear_screen();
    ui_display_header("ONE-ON-ONE CHAT APPLICATION");
    
    printf("  Welcome! Please select an option:\n\n");
    printf("  [1] Login\n");
    printf("  [2] Register\n");
    printf("  [3] Exit\n\n");
    printf("  Enter your choice: ");
}

// Renders the login input interface
void ui_display_login_screen() {
    ui_clear_screen();
    ui_display_header("ACCOUNT LOGIN");
}

// Renders the registration input interface
void ui_display_register_screen() {
    ui_clear_screen();
    ui_display_header("CREATE NEW ACCOUNT");
}

// Renders the authenticated dashboard
void ui_display_dashboard(char username[]) {
    ui_clear_screen();
    ui_display_header("USER DASHBOARD");
    
    printf("  Welcome, %s!\n\n", username);
    printf("  [1] Open Chat\n");
    printf("  [2] Search User\n");
    printf("  [3] Deregister Account\n");
    printf("  [4] Logout\n\n");
    printf("  Enter your choice: ");
}

// Renders the user search interface
void ui_display_search_screen() {
    ui_clear_screen();
    ui_display_header("USER SEARCH");
}

// Renders the deregistration warning
void ui_display_deregister_warning() {
    ui_clear_screen();
    ui_display_header("!! WARNING !!");
    
    printf("  This action is PERMANENT and cannot be undone.\n");
    printf("  Your account and all associated data will be deleted.\n\n");
    printf("  Are you sure you want to deregister? (y/n): ");
}

// Renders the chat inbox with contacts list
void ui_display_inbox(char contacts[][50], int count) {
    ui_clear_screen();
    ui_display_header("OPEN A CHAT");
    
    if (count > 0) {
        printf("  Recent conversations:\n\n");
        for (int i = 0; i < count; i++) {
            printf("    [%d] %s\n", i + 1, contacts[i]);
        }
        printf("\n  ----------------------------------\n\n");
    }
    
    printf("  Enter username or number to start chatting:\n\n");
}

// Renders the active chat session header
void ui_display_chat_screen(const char me[], const char partner[]) {
    ui_clear_screen();
    
    char header[100];
    snprintf(header, sizeof(header), "CHAT: %s <-> %s", me, partner);
    ui_display_header(header);
    
    printf("  Type your message and press Enter to send.\n");
    printf("  Type /q to exit the chat.\n");
    printf("--------------------------------------------------\n\n");
}

// Renders a single message
void ui_display_message(char sender[], char timestamp[], char text[]) {
    printf("  < %s : [%s] > %s\n", sender, timestamp, text);
}

// Renders the chat input prompt
void ui_display_chat_input_prompt() {
    printf("\n  You: ");
    fflush(stdout);
}

// Renders a success message
void ui_success(char message[]) {
    printf("\n  [+] SUCCESS: %s\n", message);
}

// Renders an error message
void ui_error(char message[]) {
    printf("\n  [!] ERROR: %s\n", message);
}

// Renders server connection error
void ui_server_error() {
    printf("\n  [!] ERROR: Could not connect to server. Please try again later.\n");
}

// Pauses until user presses Enter
void ui_wait_for_enter() {
    printf("\n  Press Enter to continue...");
    getchar();
}
