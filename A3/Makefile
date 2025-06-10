# Define compiler.
CC = gcc

# Compilation flags.
CFLAGS = -Wall -pedantic -std=gnu99 -Wextra -I/local/courses/csse2310/include

# Linking flags.
LDFLAGS = -L/local/courses/csse2310/lib -lcsse2310a3

# Define additional flags for debugging.
DEBUG = -g

# Default target to run
.DEFAULT_GOAL := uqparallel

# Targets which do not generate output files
.PHONY: debug clean

# Define targets and list dependencies
uqparallel: uqparallel.o
        $(CC) $(CFLAGS) -o uqparallel uqparallel.o $(LDFLAGS)

uqparallel.o: uqparallel.c
        $(CC) $(CFLAGS) -c uqparallel.c -o uqparallel.o

# Clean.
clean:
        rm -f uqparallel *.o