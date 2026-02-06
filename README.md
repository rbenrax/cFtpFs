# cFtpfs - FTP Filesystem in C

C implementation of a FUSE filesystem for mounting FTP servers, based on PyFtpfs.

## Features

- **Mounts FTP servers as a local filesystem** using FUSE
- **Read and write support** for files
- **Directory caching** with configurable timeout (default 5 seconds/30 seconds)
- **Temporary write system** for VS Code compatibility
- **Support for FTP listing formats** Unix and Windows
- **Thread-safe connections** with lock handling
- **Multiple connection options** (port, user, password, encoding)
- **Full operations**: create, read, write, delete, rename, directories

## Dependencies

### Ubuntu/Debian
```bash
sudo apt-get install libfuse3-dev libcurl4-openssl-dev build-essential
```

### Fedora/RHEL
```bash
sudo dnf install fuse3-devel libcurl-devel gcc make
```

### Arch Linux
```bash
sudo pacman -S fuse3 curl gcc make
```

## Installation

### Method 1: Automatic Installation Script (Recommended)
```bash
chmod +x install.sh
sudo ./install.sh
```

The script will automatically detect your operating system and install all necessary dependencies.

### Method 2: Manual Compilation
```bash
make
sudo make install
```

### Uninstall
```bash
sudo make uninstall
# or
sudo /usr/local/bin/uninstall_cftpfs.sh
```

## Usage

### Basic Syntax

```bash
cftpfs <host> <mountpoint> [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-p, --port=PORT` | FTP Port | 21 |
| `-u, --user=USER` | FTP User | anonymous |
| `-P, --password=PASS` | FTP Password | (empty) |
| `-e, --encoding=ENC` | Encoding | utf-8 |
| `-d, --debug` | Debug mode with detailed logs | - |
| `-f, --foreground` | Run in foreground | - |
| `-h, --help` | Show help | - |

### Examples

Mount in foreground with user and password:
```bash
cftpfs ftp.example.com /mnt/ftp -u myuser -P mypassword -f
```

Mount in background:
```bash
cftpfs ftp.example.com /mnt/ftp -u user -P password
```

Mount anonymous FTP server:
```bash
cftpfs ftp.gnu.org /mnt/gnu -f
```

### Unmount

```bash
fusermount -u /mnt/ftp
```

## Architecture

The project is organized into the following modules:

```
cFtpfs/
├── include/
│   └── cftpfs.h          # Definitions and data structures
├── src/
│   ├── main.c            # Entry point and FUSE operations
│   ├── ftp_client.c      # FTP client using libcurl
│   ├── ftp_client_mock.c # Mock version for testing
│   ├── cache.c           # Directory cache system
│   ├── handles.c         # File handle management
│   └── parser.c          # FTP listing parser (Unix/Windows)
├── Makefile              # Compilation script
├── install.sh            # Automatic installation script
└── README.md             # Documentation
```

## Supported Operations

- **Navigation**: `getattr`, `readdir`
- **Reading**: `open`, `read`
- **Writing**: `create`, `write`, `truncate`
- **Management**: `unlink`, `mkdir`, `rmdir`, `rename`
- **Metadata**: `chmod`, `chown`, `utimens` (stubs - not supported by standard FTP)

## Cache System

- **Timeout**: Configurable (default 30s) for directory listings and attributes.
- **Strategy**: Copy-on-read to avoid race conditions.
- **Invalidation**: Automatic on write operations.

## Limitations

- Does not support real permission changes (chmod) - standard FTP does not allow it
- Does not support owner changes (chown)
- Does not support timestamp changes (utimens)
- Symlinks are detected but not followed
- Fixed cache timeout logic (though configurable via flags now)

## Compilation Modes

### Mock Version (development/testing)
```bash
make mock
```
Uses a simulated FTP client that returns test data. Useful for development and testing without a real FTP server.

### Real Version (production)
```bash
make
```
Requires libcurl installed. Uses real FTP connections.

## Performance

- **Reading**: Similar to standard FTP client.
- **Writing**: Optimized for editors (VS Code) with temporary files.
- **Cache**: Reduces network operations for directory listings.
- **Connection**: Uses persistent connections (Keep-Alive) to avoid handshake overhead.

## Troubleshooting

### Error: "fuse: device not found"
Ensure the FUSE module is loaded:
```bash
sudo modprobe fuse
```

### Error: "Permission denied"
Ensure your user is in the `fuse` group:
```bash
sudo usermod -a -G fuse $USER
# Log out and log back in to apply changes
```

### FTP Connection Error
Verify that the FTP server allows passive connections and that the port is open:
```bash
telnet ftp.example.com 21
```

### Error "munmap_chunk(): invalid pointer"
This error was fixed in the current version. If it persists, ensure you are using the latest version of the code.

## Differences from PyFtpfs

- **Performance**: Native C implementation is faster.
- **Memory**: Lower memory consumption.
- **Dependencies**: Uses libcurl instead of ftplib (Python).
- **Threading**: Manual implementation with pthreads.

## License

Apache 2.0 - Same as the original PyFtpfs project.

## Author

C implementation based on PyFtpfs.

## Contributing

Contributions are welcome. Please:

1. Fork the repository
2. Create a branch for your feature (`git checkout -b feature/new-feature`)
3. Commit your changes (`git commit -am 'Add new feature'`)
4. Push to the branch (`git push origin feature/new-feature`)
5. Open a Pull Request

## Version History

- **v1.0.0** - Initial complete version
  - Full FUSE operations support
  - Cache system
  - Temporary file management
  - Unix/Windows listing parser
  - Automatic installer
  - Memory error fixes
  - Performance improvements (persistent connections)

## Acknowledgements

This project is based on PyFtpfs and uses:
- FUSE3 for the user-space filesystem
- libcurl for FTP connections
- pthreads for threading

## Contact

To report bugs or request features, please open an issue in the repository.
