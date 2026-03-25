# One-on-One Chat Application

A connection-oriented iterative server chat application that allows users to register, login, search for other users, and exchange messages in real-time.

## Features

- **User Registration & Authentication** - Create accounts, login, and logout
- **User Search** - Find other registered users
- **One-on-One Messaging** - Send and receive messages with other users
- **Account Deregistration** - Permanently delete your account
- **Auto-refresh Chat** - Messages refresh every 2 seconds
- **Last 20 Messages** - Chat displays the most recent 20 messages

## Project Structure

```
ChatApp-2/
├── server/
│   ├── server.c      # Main server entry point
│   ├── auth.c/h      # Authentication & account management
│   ├── chat.c/h      # Messaging engine
│   └── utils.c/h     # Utility functions
├── client/
│   ├── client.c      # Main client entry point
│   └── ui.c/h        # User interface functions
├── shared/
│   └── models.h      # Shared data structures
├── data/
│   ├── users.txt     # User credentials storage
│   └── messages.txt  # Message storage
├── compile_server.bat
├── compile_client.bat
└── README.md
```

## Prerequisites

- **GCC Compiler** (MinGW on Windows)
- Make sure `gcc` is in your system PATH

To verify GCC is installed:
```
gcc --version
```

## How to Compile

### Option 1: Using Batch Files (Recommended)

Simply double-click the batch files or run from command line:

```batch
.\compile_server.bat    # Compiles the server
.\compile_client.bat    # Compiles the client
```

### Option 2: Manual Compilation

**Compile Server:**
```batch
cd server
gcc -o server.exe server.c auth.c chat.c utils.c -lws2_32
```

**Compile Client:**
```batch
cd client
gcc -o client.exe client.c ui.c -lws2_32
```

## How to Run

### Step 1: Start the Server

Open a terminal and run:
```batch
cd server
.\server.exe
```

You should see:
```
[+] Server started. Listening on port 8080...
[*] Waiting for connection...
```

### Step 2: Start the Client

Open a **second terminal** and run:
```batch
cd client
.\client.exe
```

You can run multiple client instances in separate terminals to simulate different users.

## Usage Guide

### Main Menu (Not Logged In)
```
[1] Login      - Login to existing account
[2] Register   - Create a new account
[3] Exit       - Close the application
```

### Dashboard (Logged In)
```
[1] Open Chat         - Start chatting with another user
[2] Search User       - Check if a user exists
[3] Deregister Account - Delete your account permanently
[4] Logout            - Return to main menu
```

### Chat Commands
- Type your message and press **Enter** to send
- Type `/q` and press **Enter** to exit the chat

## Communication Protocol

The application uses a pipe-delimited text protocol over TCP:

| Request Type | Format | Description |
|--------------|--------|-------------|
| REGISTER | `REGISTER\|username\|password` | Create new account |
| LOGIN | `LOGIN\|username\|password` | Authenticate user |
| LOGOUT | `LOGOUT\|username` | End session |
| SEARCH | `SEARCH\|username\|target` | Find a user |
| DEREGISTER | `DEREGISTER\|username` | Delete account |
| SEND | `SEND\|sender\|receiver\|message` | Send message |
| FETCH | `FETCH\|sender\|receiver` | Get conversation |

**Response Format:**
- Success: `SUCCESS|message`
- Error: `ERROR|message`
- Data: `DATA|id|sender|receiver|text|timestamp` (followed by `END`)

## Important Notes

1. **Start Server First** - Always start the server before running clients
2. **Single Thread Server** - The server handles one client at a time (iterative)
3. **Port 8080** - Both client and server use localhost port 8080
4. **Data Persistence** - User and message data is stored in flat text files
5. **No Encryption** - Passwords are stored in plain text (for educational purposes)
6. **Auto-refresh** - Chat screen refreshes every 2 seconds to show new messages
7. **Ctrl+C** - Press Ctrl+C in the server terminal to shut down gracefully

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "Could not connect to server" | Make sure the server is running |
| Compilation errors | Ensure GCC and MinGW are properly installed |
| Port already in use | Close other applications using port 8080 |
| Messages not appearing | Wait 2 seconds for auto-refresh or send a new message |

## Technical Specifications

- **Connection Type:** TCP (SOCK_STREAM)
- **Server Model:** Iterative (one client at a time)
- **Connection Pattern:** Synchronous + Transient (new connection per operation)
- **Buffer Size:** 1024 bytes
- **Port:** 8080
- **Host:** localhost (127.0.0.1)

## Authors

SCS304 Networks Group 4
