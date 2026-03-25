@echo off
cd /d "%~dp0"
echo Compiling client...
cd client
gcc -o client.exe client.c ui.c -lws2_32
if %ERRORLEVEL% EQU 0 (
    echo [+] Client compiled successfully: client\client.exe
) else (
    echo [!] Compilation failed
)
cd ..
pause
