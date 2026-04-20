#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "network_server.h"
#include "../shared/network_config.h"

static int network_mode = NETWORK_MODE_LAN;
static SOCKET discovery_socket = INVALID_SOCKET;
static char local_tailscale_ip[INET_ADDRSTRLEN] = "";
static char local_lan_ip[INET_ADDRSTRLEN] = "";

static int extract_tailscale_ip(const char *status_output, char *ip_buffer, size_t ip_buffer_size) {
    const char *cursor = status_output;

    while (cursor && *cursor) {
        if (strncmp(cursor, "100.", 4) == 0) {
            int a, b, c, d;
            char parsed_ip[INET_ADDRSTRLEN];

            if (sscanf(cursor, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                snprintf(parsed_ip, sizeof(parsed_ip), "%d.%d.%d.%d", a, b, c, d);
                strncpy(ip_buffer, parsed_ip, ip_buffer_size - 1);
                ip_buffer[ip_buffer_size - 1] = '\0';
                return 1;
            }
        }
        cursor++;
    }

    return 0;
}

static int get_tailscale_status(char *output, size_t output_size, char *tailscale_ip, size_t ip_size) {
    FILE *pipe;
    char line[512];
    size_t used = 0;

    output[0] = '\0';
    tailscale_ip[0] = '\0';

    pipe = _popen("tailscale status", "r");
    if (pipe == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), pipe) != NULL) {
        size_t line_length = strlen(line);

        if (used + line_length < output_size - 1) {
            strcpy(output + used, line);
            used += line_length;
        }
    }

    _pclose(pipe);

    if (used == 0) {
        return 0;
    }

    return extract_tailscale_ip(output, tailscale_ip, ip_size);
}

static int determine_lan_ip(char *ip_buffer, size_t ip_buffer_size) {
    char hostname[256];
    struct hostent *host_info;
    int index = 0;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return 0;
    }

    host_info = gethostbyname(hostname);
    if (host_info == NULL) {
        return 0;
    }

    while (host_info->h_addr_list[index] != NULL) {
        struct in_addr addr;
        const char *converted;

        memcpy(&addr, host_info->h_addr_list[index], sizeof(struct in_addr));
        converted = inet_ntoa(addr);

        if (converted != NULL && strcmp(converted, "127.0.0.1") != 0) {
            strncpy(ip_buffer, converted, ip_buffer_size - 1);
            ip_buffer[ip_buffer_size - 1] = '\0';
            return 1;
        }

        index++;
    }
    printf("[Network] Using LAN IP: %s\n", local_lan_ip);

    return 0;
}

static int setup_discovery_socket() {
    struct sockaddr_in discovery_addr;
    int reuse = 1;

    discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (discovery_socket == INVALID_SOCKET) {
        return 0;
    }

    setsockopt(discovery_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    memset(&discovery_addr, 0, sizeof(discovery_addr));
    discovery_addr.sin_family = AF_INET;
    discovery_addr.sin_port = htons(DISCOVERY_PORT);
    discovery_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(discovery_socket, (struct sockaddr *)&discovery_addr, sizeof(discovery_addr)) == SOCKET_ERROR) {
        CLOSE_SOCKET(discovery_socket);
        discovery_socket = INVALID_SOCKET;
        return 0;
    }

    return 1;
}

int initialize_server_network() {
    char status_output[4096];

    if (get_tailscale_status(status_output, sizeof(status_output), local_tailscale_ip, sizeof(local_tailscale_ip)) &&
        strstr(status_output, "offline") == NULL) {
        network_mode = NETWORK_MODE_TAILSCALE;
        printf("[Network] Tailscale detected and active.\n");
        printf("[Network] Local Tailscale IP: %s\n", local_tailscale_ip);
        
    }

    network_mode = NETWORK_MODE_LAN;
    printf("[Network] Tailscale unavailable or offline. Using LAN discovery mode.\n");

    if (!determine_lan_ip(local_lan_ip, sizeof(local_lan_ip))) {
        printf("[Network] Failed to determine LAN IP.\n");
        return 0;
    }

    printf("[Network] Local LAN IP: %s\n", local_lan_ip);

    if (!setup_discovery_socket()) {
        printf("[Network] Failed to start UDP discovery socket.\n");
        return 0;
    }

    printf("[Network] UDP discovery listening on port %d.\n", DISCOVERY_PORT);
    return 1;
}

int get_server_network_mode() {
    return network_mode;
}

SOCKET get_discovery_socket() {
    return discovery_socket;
}

void handle_discovery_request(SOCKET socket_handle) {
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    int received;

    received = recvfrom(socket_handle, buffer, BUFFER_SIZE - 1, 0,
                        (struct sockaddr *)&client_addr, &client_addr_len);
    if (received <= 0) {
        return;
    }

    buffer[received] = '\0';

    if (strcmp(buffer, DISCOVERY_REQUEST) != 0) {
        return;
    }

    snprintf(response, sizeof(response), "%s|%s|%d",
             DISCOVERY_RESPONSE_PREFIX, local_lan_ip, PORT);

    sendto(socket_handle, response, (int)strlen(response), 0,
           (struct sockaddr *)&client_addr, client_addr_len);

    printf("[Discovery] Responded to LAN discovery from %s:%d\n",
       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
}

void cleanup_server_network() {
    if (discovery_socket != INVALID_SOCKET) {
        CLOSE_SOCKET(discovery_socket);
        discovery_socket = INVALID_SOCKET;
    }
}
