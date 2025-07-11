# Compiler & Flags
CC = gcc
#CFLAGS = -Wall -Wextra -g

# Source & Object Files
SRCS = fss_console.c fss_manager.c worker.c
OBJS = $(SRCS:.c=.o)

# Executables
BINS = fss_console fss_manager worker

.PHONY: all build clean

# Default target
all: build

# Build all binaries
build: $(BINS)

# Rules for individual binaries
fss_console: fss_console.o
	$(CC) $(CFLAGS) -o $@ $^

fss_manager: fss_manager.o
	$(CC) $(CFLAGS) -o $@ $^

worker: worker.o
	$(CC) $(CFLAGS) -o $@ $^

# Generic rule for compiling .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f *.o $(BINS)
