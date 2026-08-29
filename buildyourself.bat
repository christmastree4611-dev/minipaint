@echo off
REM THIS USES GCC TO BUILD.. REPLACE WITH YOUR COMMANDS OR RUN FROM CMD/POWRSHELL TO BUILD.
set GCC=gcc

%GCC% paint.c -o paint.exe -mwindows -lgdi32 -luser32 -lcomdlg32 -O2 -s

if errorlevel 1 (
    echo.
    echo Build failed. Make sure gcc ^(.MinGW^) is installed and on PATH.
    exit /b 1
)

echo.
echo Build OK -^> paint.exe
dir paint.exe | findstr /R "paint.exe"
