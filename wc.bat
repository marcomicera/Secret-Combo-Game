@echo off
gcc -g -Wall comb_client.c -o comb_client
cls
comb_client 127.0.0.1 8765