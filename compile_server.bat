@echo off
cd /d "%~dp0"
echo Compiling server...
cd server
gcc -o server.exe server.c auth.c chat.c utils.c -lws2_32
if %ERRORLEVEL% EQU 0 (
    echo [+] Server compiled successfully: server\server.exe
) else (
    echo [!] Compilation failed
)
cd ..
pause
