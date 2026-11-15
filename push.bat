@echo off
setlocal
cd /d "%~dp0"

set "GITBASH="
if exist "%ProgramFiles%\Git\bin\bash.exe" set "GITBASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined GITBASH if exist "%ProgramFiles(x86)%\Git\bin\bash.exe" set "GITBASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
if not defined GITBASH if exist "%LocalAppData%\Programs\Git\bin\bash.exe" set "GITBASH=%LocalAppData%\Programs\Git\bin\bash.exe"

if not defined GITBASH (
    echo [ERROR] Git Bash not found. Install Git for Windows: https://git-scm.com/download/win
    pause
    exit /b 1
)

"%GITBASH%" --login push.sh
endlocal
