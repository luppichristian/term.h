@echo off
setlocal

wsl.exe bash -lc "gcc -std=c11 -Wall -Wextra -pedantic term_example.c -o term_example.out"
if errorlevel 1 (
  echo WSL build failed.
  exit /b 1
)

echo WSL build succeeded: term_example
