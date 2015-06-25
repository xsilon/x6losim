#!/bin/sh

valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./src/x6losim
