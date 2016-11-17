#!/bin/sh
gcc client.c ../c/SMFPClient.c ../c/elemental/elemental.c -I../c/elemental -I../c -Wall -o client
./client
