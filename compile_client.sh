#!/bin/bash
cd "$(dirname "$0")"
echo "Compiling client..."
cd client
gcc -o client client.c ui.c network_client.c
if [ $? -eq 0 ]; then
    echo "[+] Client compiled successfully: client/client"
else
    echo "[!] Compilation failed"
fi
cd ..