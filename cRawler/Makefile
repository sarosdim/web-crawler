# Variables
CC       := gcc
CFLAGS   := -Wall -Wextra -O2
LDFLAGS  := -lcurl
TARGET   := crawler
SRC      := crawler.c

# Default goal
all: $(TARGET)

# Build
$(TARGET): $(SRC) cjson.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

# Run (pass GITHUB_TOKEN env var for higher rate limits)
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET)

.PHONY: all run clean
