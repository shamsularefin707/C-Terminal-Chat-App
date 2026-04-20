# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS_SERVER = -pthread
LDFLAGS_CLIENT = -pthread -lncurses

# Executable names
SERVER_EXE = server
CLIENT_EXE = client

# Source files (net_helper.c is included by server.c and client.c)
SERVER_SRC = server.c
CLIENT_SRC = client.c

# Default target: build all
all: $(SERVER_EXE) $(CLIENT_EXE)

# Rule to build the server
# net_helper.c is NOT listed as a dependency because it's #included in server.c
$(SERVER_EXE): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER_EXE) $(SERVER_SRC) $(LDFLAGS_SERVER)

# Rule to build the client
# net_helper.c is NOT listed as a dependency because it's #included in client.c
$(CLIENT_EXE): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT_EXE) $(CLIENT_SRC) $(LDFLAGS_CLIENT)

# Rule to clean up build artifacts and logs
clean:
	rm -f $(SERVER_EXE) $(CLIENT_EXE) chat_history.log
	@echo "Cleaned up all build files and logs."

# Phony targets
.PHONY: all clean
