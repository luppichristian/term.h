@echo off
setlocal

gcc -std=c11 -Wall -Wextra -pedantic term_example.c -o term_example.exe
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

echo Build succeeded: term_example.exe
