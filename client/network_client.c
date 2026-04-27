#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include "network_client.h"
#include "../shared/network_config.h"

#define CLOSE_SOCKET   close
#define SOCKET         int
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1

static char server_ip[INET_ADDRSTRLEN] = "127.0.0.1";

static int extract_tailscale_ip(const char *status_output, char *ip_buffer,
                                 size_t ip_buffer_size) {
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

static int get_tailscale_status(char *output, size_t output_size,
                                char *tailscale_ip, size_t ip_size) {
    FILE  *pipe;
    char   line[512];
    size_t used = 0;

    output[0]       = '\0';
    tailscale_ip[0] = '\0';

    pipe = popen("tailscale status", "r");   /* <-- was _popen */
    if (pipe == NULL) return 0;

    while (fgets(line, sizeof(line), pipe) != NULL) {
        size_t len = strlen(line);
        if (used + len < output_size - 1) {
            strcpy(output + used, line);
            used += len;
        }
    }

    pclose(pipe);   /* <-- was _pclose */

    if (used == 0) return 0;
    return extract_tailscale_ip(output, tailscale_ip, ip_size);
}

static int resolve_magicdns_name(const char *hostname, char *resolved_ip,
                                  size_t resolved_ip_size) {
    struct hostent *host_info = gethostbyname(hostname);
    if (host_info == NULL) return 0;

    int index = 0;
    while (host_info->h_addr_list[index] != NULL) {
        struct in_addr addr;
        memcpy(&addr, host_info->h_addr_list[index], sizeof(struct in_addr));
        const char *converted = inet_ntoa(addr);
        if (converted != NULL) {
            strncpy(resolved_ip, converted, resolved_ip_size - 1);
            resolved_ip[resolved_ip_size - 1] = '\0';
            return 1;
        }
        index++;
    }
    return 0;
}

static int discover_server_on_lan(char *discovered_ip, size_t discovered_ip_size) {
    SOCKET udp_socket;
    struct sockaddr_in broadcast_addr, response_addr;
    int    broadcast_enabled  = 1;
    char   response[BUFFER_SIZE];
    char   response_ip[INET_ADDRSTRLEN];
    socklen_t response_addr_len = sizeof(response_addr);

    /* <-- was DWORD timeout_ms = 5000 + SO_RCVTIMEO with DWORD */
    struct timeval timeout;
    timeout.tv_sec  = 5;
    timeout.tv_usec = 0;

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket == INVALID_SOCKET) return 0;

    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST,
               &broadcast_enabled, sizeof(broadcast_enabled));
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO,
               &timeout, sizeof(timeout));   /* <-- struct timeval now */

    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family      = AF_INET;
    broadcast_addr.sin_port        = htons(DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    if (sendto(udp_socket, DISCOVERY_REQUEST, strlen(DISCOVERY_REQUEST), 0,
               (struct sockaddr *)&broadcast_addr,
               sizeof(broadcast_addr)) == SOCKET_ERROR) {
        CLOSE_SOCKET(udp_socket);
        return 0;
    }

    int received = recvfrom(udp_socket, response, BUFFER_SIZE - 1, 0,
                            (struct sockaddr *)&response_addr, &response_addr_len);
    if (received <= 0) {
        CLOSE_SOCKET(udp_socket);
        return 0;
    }

    response[received] = '\0';

    if (sscanf(response, "CHAT_SERVER|%15[^|]|%*d", response_ip) != 1) {
        CLOSE_SOCKET(udp_socket);
        return 0;
    }

    strncpy(discovered_ip, response_ip, discovered_ip_size - 1);
    discovered_ip[discovered_ip_size - 1] = '\0';

    CLOSE_SOCKET(udp_socket);
    return 1;
}

int initialize_server_address(int mode) {
    char status_output[4096];
    char tailscale_ip[INET_ADDRSTRLEN];
    char magicdns_name[256];
    char resolved_ip[INET_ADDRSTRLEN];

    if (mode == NETWORK_MODE_TAILSCALE) {
        printf("\n[Network] Tailnet mode selected.\n");
        if (!get_tailscale_status(status_output, sizeof(status_output),
                                  tailscale_ip, sizeof(tailscale_ip)) ||
            strstr(status_output, "offline") != NULL) {
            printf("[Network] Tailscale is not active on this machine.\n");
            return 0;
        }
        printf("[Network] Local Tailscale IP: %s\n", tailscale_ip);
        printf("Enter the Server's MagicDNS name: ");
        if (fgets(magicdns_name, sizeof(magicdns_name), stdin) == NULL) return 0;
        magicdns_name[strcspn(magicdns_name, "\r\n")] = '\0';
        if (strlen(magicdns_name) == 0) {
            printf("[Network] MagicDNS name cannot be empty.\n");
            return 0;
        }
        if (!resolve_magicdns_name(magicdns_name, resolved_ip, sizeof(resolved_ip))) {
            printf("[Network] Failed to resolve MagicDNS name.\n");
            return 0;
        }
        strncpy(server_ip, resolved_ip, sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
        printf("[Network] Server resolved: %s\n", server_ip);
        return 1;

    } else if (mode == NETWORK_MODE_LAN) {
        printf("\n[Network] Same LAN mode selected.\n");
        printf("[Network] Scanning for the server...\n");
        if (!discover_server_on_lan(resolved_ip, sizeof(resolved_ip))) {
            printf("[Network] Server not found on LAN.\n");
            return 0;
        }
        strncpy(server_ip, resolved_ip, sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
        printf("[Network] Server found: %s\n", server_ip);
        return 1;

    } else if (mode == NETWORK_MODE_MANUAL) {
        char manual_ip[INET_ADDRSTRLEN];
        printf("\n[Network] Manual mode selected.\n");
        printf("Enter the Server's IP address: ");
        if (fgets(manual_ip, sizeof(manual_ip), stdin) == NULL) return 0;
        manual_ip[strcspn(manual_ip, "\r\n")] = '\0';
        if (strlen(manual_ip) == 0) {
            printf("[Network] IP address cannot be empty.\n");
            return 0;
        }
        strncpy(server_ip, manual_ip, sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
        printf("[Network] Server set to: %s\n", server_ip);
        return 1;
    }

    return 0;
}

const char *get_server_ip() {
    return server_ip;
}