# One-on-One Chat Application

A connection-oriented iterative client-server chat application that supports running across different machines with two startup modes:

- **Tailscale mode** for remote machine-to-machine communication using MagicDNS
- **LAN mode** for automatic server discovery using UDP broadcast fallback

This version preserves the original iterative TCP chat behavior while adding startup-time network detection and server discovery.

## Features

- **User Registration & Authentication** - Create accounts, login, and logout
- **User Search** - Find other registered users
- **One-on-One Messaging** - Send and receive messages in real-time
- **Account Deregistration** - Permanently delete your account
- **Inbox / Contacts View** - Open previous conversations quickly
- **Auto-refresh Chat** - Messages refresh every 2 seconds
- **Last 20 Messages** - Chat displays the most recent 20 messages
- **Tailscale Detection** - Uses `tailscale status` via `_popen()`
- **MagicDNS Resolution** - Client resolves server hostname with `getaddrinfo()`
- **LAN Auto Discovery** - Client broadcasts over UDP if Tailscale is unavailable

## Startup Logic

At startup, both client and server run:

```c
_popen("tailscale status", "r")
```

The output is analyzed as follows:

- If a `100.x.x.x` Tailscale IP is found and the output does **not** contain `offline`
  - the program assumes **Tailscale is active**
- If the command fails, no valid Tailscale IP is found, or the output contains `offline`
  - the program assumes **Tailscale is unavailable**
  - the program falls back to **LAN mode**

### Tailscale Mode
When Tailscale is active:

- the program prints the local Tailscale IP
- the **client prompts**:
  - `Enter the Server's MagicDNS name:`
- the client resolves the server name with `getaddrinfo()`
- the resolved IP is used for the normal TCP connection on port `8080`

### LAN Mode
When Tailscale is unavailable:

- the client sends a UDP broadcast to `255.255.255.255` on port `8081`
- the server listens for discovery requests on UDP port `8081`
- the server replies with:
  - `CHAT_SERVER|<server-ip>|8080`
- the client extracts the returned IP and uses it for the TCP chat connection

## Project Structure

```text
ChatApp3-Iterative-Connection/
├── server/
│   ├── server.c             # Main iterative TCP server loop
│   ├── auth.c/h             # Authentication & account management
│   ├── chat.c/h             # Messaging engine
│   ├── utils.c/h            # Utility functions
│   ├── network_server.c/h   # Server-side Tailscale check + UDP discovery
├── client/
│   ├── client.c             # Main client entry point
│   ├── ui.c/h               # User interface functions
│   ├── network_client.c/h   # Client-side Tailscale check + discovery
├── shared/
│   ├── models.h             # Shared data structures and constants
│   └── network_config.h     # Shared network/discovery constants
├── data/
│   ├── users.txt            # User credentials storage
│   └── messages.txt         # Message storage
├── compile_server.bat
├── compile_client.bat
└── README.md
```

## Prerequisites

- **GCC Compiler** (MinGW on Windows)
- `gcc` must be available in PATH
- **Tailscale** installed if you want to use Tailscale mode

To verify GCC:

```batch
gcc --version
```

To verify Tailscale:

```batch
tailscale status
```

## How to Compile

### Using Batch Files

```batch
.\compile_server.bat
.\compile_client.bat
```

### Manual Compilation

**Compile Server**
```batch
cd server
gcc -o server.exe server.c auth.c chat.c utils.c network_server.c -lws2_32
```

**Compile Client**
```batch
cd client
gcc -o client.exe client.c ui.c network_client.c -lws2_32
```

## How to Run

### Step 1: Start the Server

```batch
cd server
.\server.exe
```

On startup the server will:

- check `tailscale status`
- print either:
  - local Tailscale IP, or
  - local LAN IP and UDP discovery status

### Step 2: Start the Client

Open another terminal:

```batch
cd client
.\client.exe
```

The client will:

- check `tailscale status`
- if Tailscale is active:
  - ask for the server MagicDNS name
- otherwise:
  - automatically broadcast on LAN and discover the server IP

### Step 3: Use the Chat App

Once discovery/resolution succeeds, the client proceeds with the normal menus and chat features.

## Usage Guide

### Main Menu (Not Logged In)
```text
[1] Login
[2] Register
[3] Exit
```

### Dashboard (Logged In)
```text
[1] Open Chat
[2] Search User
[3] Deregister Account
[4] Logout
```

### Chat Commands
- Type a message and press **Enter** to send
- Type `/q` and press **Enter** to leave the chat

## Communication Protocol

The application still uses a pipe-delimited TCP text protocol for the actual chat system.

### TCP Requests

| Request Type | Format | Description |
|--------------|--------|-------------|
| REGISTER | `REGISTER\|username\|password` | Create new account |
| LOGIN | `LOGIN\|username\|password` | Authenticate user |
| LOGOUT | `LOGOUT\|username` | End session |
| SEARCH | `SEARCH\|username\|target` | Find a user |
| DEREGISTER | `DEREGISTER\|username` | Delete account |
| SEND | `SEND\|sender\|receiver\|message` | Send message |
| FETCH | `FETCH\|sender\|receiver` | Get conversation |
| CONTACTS | `CONTACTS\|username` | Get inbox contact list |

### TCP Response Format
- Success: `SUCCESS|message`
- Error: `ERROR|message`
- Contact row: `CONTACT|username`
- Message row: `DATA|id|sender|receiver|text|timestamp`
- Terminator: `END`

### UDP Discovery Protocol
- Client request: `DISCOVER_CHAT_SERVER`
- Server reply: `CHAT_SERVER|<server-ip>|8080`

## Technical Specifications

- **Chat Transport:** TCP (`SOCK_STREAM`)
- **Discovery Transport:** UDP (`SOCK_DGRAM`)
- **Server Model:** Iterative, single-threaded
- **Server Loop Design:** One main loop monitors TCP and UDP sockets
- **TCP Port:** `8080`
- **UDP Discovery Port:** `8081`
- **Buffer Size:** `1024`

## Important Notes

1. The **chat service remains connection-oriented and iterative**
2. UDP is used **only once during startup discovery**
3. No extra threads are used
4. The server uses a single-threaded loop to handle both:
   - UDP discovery
   - TCP client requests
5. Passwords are still stored in plain text for educational purposes
6. Data is stored in flat text files under `data/`

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `tailscale status` fails | The program falls back to LAN mode |
| Client cannot discover server on LAN | Make sure both machines are on the same network and UDP broadcast is allowed |
| MagicDNS resolution fails | Confirm the entered Tailscale hostname is correct |
| Could not connect to server | Ensure the server is running first |
| Compilation errors | Ensure GCC/MinGW is installed and available in PATH |
| Messages not appearing | Wait for auto-refresh or send a new message |

## Authors

SCS304 Networks Group 4
