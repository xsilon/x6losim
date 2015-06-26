#!/bin/sh

valgrind --malloc-fill=0x5a --leak-check=full --show-leak-kinds=all --track-origins=yes --track-fds=yes --log-file="vgrind-results.txt" ./src/x6losim
