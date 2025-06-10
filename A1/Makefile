# Define compiler.
CC = gcc

# Compilation flags.
CFLAGS = -Wall -pedantic -std=gnu99 -Wextra -I/local/courses/csse2310/include

# Linking flags.
LDFLAGS = -L/local/courses/csse2310/lib -ltinyexpr -lm

# Define additional flags for debugging.
DEBUG = -g

# Default target to run
.DEFAULT_GOAL := uqexpr

# Targets which do not generate output files
.PHONY: debug clean

# Define targets and list dependencies
uqexpr: uqexpr.o
        $(CC) $(CFLAGS) -o uqexpr uqexpr.o $(LDFLAGS)

uqexpr.o: uqexpr.c
        $(CC) $(CFLAGS) -c uqexpr.c -o uqexpr.o

# Clean.
clean:
        rm -f uqexpr *.o