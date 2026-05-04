@echo off
setlocal

wsl.exe bash -lc "if ! command -v gcc >/dev/null 2>&1; then echo 'gcc not found in WSL. Install build-essential first.'; exit 1; fi; cd /mnt/c/Users/Utente/Developer/termc && gcc -std=c11 -Wall -Wextra -pedantic term_example.c -o term_example"
if errorlevel 1 (
  echo WSL build failed.
  exit /b 1
)

echo WSL build succeeded: term_example
