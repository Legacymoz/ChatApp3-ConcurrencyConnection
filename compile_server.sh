#!/bin/bash
cd "$(dirname "$0")"
echo "Compiling server..."
cd server
gcc -o server server.c auth.c chat.c utils.c network_server.c
if [ $? -eq 0 ]; then
    echo "[+] Server compiled successfully: server/server"
else
    echo "[!] Compilation failed"
fi
cd ..