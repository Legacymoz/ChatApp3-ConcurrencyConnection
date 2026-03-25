#ifndef UI_H
#define UI_H

// Renders the public facing main menu
void ui_display_main_menu();

// Renders the login input interface
void ui_display_login_screen();

// Renders the registration input interface
void ui_display_register_screen();

// Renders the authenticated dashboard
void ui_display_dashboard(char username[]);

// Renders the user search interface
void ui_display_search_screen();

// Renders the deregistration warning
void ui_display_deregister_warning();

// Renders the chat inbox with contacts list
void ui_display_inbox(char contacts[][50], int count);

// Renders the active chat session header
void ui_display_chat_screen(char me[], char partner[]);

// Renders a single message
void ui_display_message(char sender[], char timestamp[], char text[]);

// Renders the chat input prompt
void ui_display_chat_input_prompt();

// Renders a success message
void ui_success(char message[]);

// Renders an error message
void ui_error(char message[]);

// Renders server connection error
void ui_server_error();

// Clears the terminal screen
void ui_clear_screen();

// Pauses until user presses Enter
void ui_wait_for_enter();

// Renders a bordered header
void ui_display_header(char title[]);

#endif // UI_H
