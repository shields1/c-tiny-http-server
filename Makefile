# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS =

# Source and object files
SRC_DIR = ./src
OBJ_DIR = ./obj
BIN_DIR = ./bin
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Executable name and target directory
TARGET = tiny

# Default target: compile and link the program
all: $(BIN_DIR)/$(TARGET)

# Rule to create the target executable
$(BIN_DIR)/$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)  # Create the bin directory if it doesn't exist
	$(CC) $(OBJ) -o $(BIN_DIR)/$(TARGET) $(LDFLAGS)

# Rule to compile source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)  # Create the obj directory if it doesn't exist
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up the build files
clean:
	rm -f $(OBJ_DIR)/*.o $(BIN_DIR)/$(TARGET)

# Rule to install the program (optional)
install: $(BIN_DIR)/$(TARGET)
	# Optionally, you can install to a system directory or just ensure the binary is in $(BIN_DIR)

# Phony targets
.PHONY: all clean install
