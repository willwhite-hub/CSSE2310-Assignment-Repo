# Define compiler.
CC              := gcc

# Compilation flags.
CFLAGS  := -Wall -pedantic -std=gnu99 -Wextra

# Linking flags.
LDFLAGS := -L/usr/lib64 \
                   -lopencv_core \
                   -lopencv_imgcodecs \
                   -lopencv_objdetect \
                   -lopencv_imgproc \
                   -lpthread

# Define additional flags for debugging.
DEBUG   := -g

# Programs to compile:
PROGS   := uqfacedetect uqfaceclient

all: $(PROGS)

# Targets which do not generate output files
.PHONY: all debug clean

# Recipe to define targets and list dependencies
%.o: %.c
        $(CC) $(CFLAGS) -c $< -o $@

uqfacedetect: uqfacedetect.o protocol.o
        $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

uqfaceclient: uqfaceclient.o protocol.o
        $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Clean.
clean:
        rm -f $(PROGS) *.o