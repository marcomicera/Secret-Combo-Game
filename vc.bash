#!/bin/bash

reset
gcc -g -Wall comb_client.c -o comb_client && valgrind --track-origins=yes --leak-check=full ./comb_client 127.0.0.1 8765
