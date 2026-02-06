# Makefile for cFtpfs - FUSE FTP Filesystem in C
# REAL version with FTP support using libcurl

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE
CFLAGS += -I./include
CFLAGS += $(shell pkg-config --cflags libcurl 2>/dev/null || echo "")

# Required libraries
LDFLAGS = -lfuse3 -lcurl -lpthread
LDFLAGS += $(shell pkg-config --libs libcurl 2>/dev/null || echo "-lcurl")

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
TARGET = cftpfs
PREFIX = /usr/local

# Source files (REAL version with FTP)
SOURCES = $(SRCDIR)/main.c \
          $(SRCDIR)/ftp_client.c \
          $(SRCDIR)/cache.c \
          $(SRCDIR)/handles.c \
          $(SRCDIR)/parser.c

# Source files for mock version (testing)
MOCK_SOURCES = $(SRCDIR)/main.c \
               $(SRCDIR)/ftp_client_mock.c \
               $(SRCDIR)/cache.c \
               $(SRCDIR)/handles.c \
               $(SRCDIR)/parser.c

OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))
MOCK_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(MOCK_SOURCES))

# Main target (REAL version)
all: check-deps $(BUILDDIR) $(TARGET)

# Check dependencies
check-deps:
	@pkg-config --exists libcurl || (echo "Error: libcurl no está instalado. Instale libcurl4-openssl-dev (Ubuntu/Debian) o libcurl-devel (Fedora)" && exit 1)
	@pkg-config --exists fuse3 || (echo "Error: fuse3 no está instalado. Instale libfuse3-dev (Ubuntu/Debian) o fuse3-devel (Fedora)" && exit 1)
	@echo "Dependencies OK"

# Create build directory
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Compile executable (REAL version)
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build successful: $(TARGET) (REAL version with FTP)"

# Compile object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Mock version (for testing without FTP server)
mock: CFLAGS += -DUSE_MOCK_FTP
mock: LDFLAGS = -lfuse3 -lpthread
mock: MOCK_OBJECTS_FILTER = $(BUILDDIR)/main.o $(BUILDDIR)/ftp_client_mock.o $(BUILDDIR)/cache.o $(BUILDDIR)/handles.o $(BUILDDIR)/parser.o
mock: $(BUILDDIR)
	$(CC) $(CFLAGS) -c $(SRCDIR)/main.c -o $(BUILDDIR)/main_mock.o
	$(CC) $(BUILDDIR)/main_mock.o $(BUILDDIR)/ftp_client_mock.o $(BUILDDIR)/cache.o $(BUILDDIR)/handles.o $(BUILDDIR)/parser.o -o $(TARGET) $(LDFLAGS)
	@echo "Build successful: $(TARGET) (MOCK version for testing)"

# Install
install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/
	@echo "Installation completed in $(PREFIX)"

# Uninstall
uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	@echo "Uninstallation completed"

# Clean
.PHONY: clean install uninstall check-deps mock

clean:
	rm -rf $(BUILDDIR) $(TARGET)

# Run example with real server
run-example: $(TARGET)
	@echo "Example: ./$(TARGET) ftp.example.com /mnt/ftp -u user -P password -f"

# Format code
format:
	find $(SRCDIR) $(INCDIR) -name "*.c" -o -name "*.h" | xargs clang-format -i 2>/dev/null || true

# Quick test with mock version
test: mock
	mkdir -p /tmp/testftp
	./$(TARGET) ftp.example.com /tmp/testftp -f &
	@echo "Mounted on /tmp/testftp - try: ls /tmp/testftp"
	@echo "Unmount with: fusermount -u /tmp/testftp"