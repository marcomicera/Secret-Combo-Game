@echo off
gcc -g -Wall comb_server.c -o comb_server
cls
comb_server 127.0.0.1 8765