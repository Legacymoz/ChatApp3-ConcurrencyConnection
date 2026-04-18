#ifndef NETWORK_SERVER_H
#define NETWORK_SERVER_H

#ifdef _WIN32
    #include <winsock2.h>
#else
    #define SOCKET int
#endif

int initialize_server_network();
int get_server_network_mode();
SOCKET get_discovery_socket();
void handle_discovery_request(SOCKET discovery_socket);
void cleanup_server_network();

#endif // NETWORK_SERVER_H
