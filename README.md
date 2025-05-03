# Distributed File System

A proof-of-concept distributed file system written in C. The system consists of four independent server nodes (`S1`, `S2`, `S3`, `S4`) and a command-line client (`w25clients`). It demonstrates basic file operations (upload, download, delete, archive) over a custom TCP/IP protocol.

---

## Table of Contents

1. [Overview](#overview)  
2. [Architecture](#architecture)  
3. [Prerequisites](#prerequisites)  
4. [Building](#building)  
5. [Usage](#usage)  
6. [Project Structure](#project-structure)  
7. [License](#license)  
8. [Future Work](#future-work)  

---

## Overview

This project illustrates how to distribute file-storage responsibilities across multiple nodes, with a client that routes its requests to the appropriate server. Each server manages its own namespace and handles:

- **UPLOAD**: Accept and store a file  
- **DOWNLOAD**: Transmit a stored file  
- **REMOVE**: Delete a stored file  
- **CREATETAR**: Package files into a tar archive  

---

## Architecture

- **Server Nodes (`S1`–`S4`)**  
  Each server listens on a unique TCP port, expands its virtual path prefix (e.g. `~S1/foo` → `./Server/S1/foo`), and performs file operations in its local directory.

- **Client (`w25clients`)**  
  A CLI tool that connects to any of the four servers, issues commands, and displays server responses.  

Servers are completely independent—there is no central coordinator, simulating a simple peer-to-peer storage network.

---

## Prerequisites

- Debian-based Linux (including WSL)  
- GCC (version ≥ 5.0)  
- `make` (optional, if you create a Makefile)  

---

## Building

From the repository root, run:

```bash
# Compile servers
gcc -o Server/S1 Server/S1.c
gcc -o Server/S2 Server/S2.c
gcc -o Server/S3 Server/S3.c
gcc -o Server/S4 Server/S4.c

# Compile client
gcc -o Client/w25clients Client/w25clients.c
```

Alternatively, you can create a simple `Makefile`:

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2

all: S1 S2 S3 S4 w25clients

S1: Server/S1.c
	$(CC) $(CFLAGS) -o Server/S1 Server/S1.c

S2: Server/S2.c
	$(CC) $(CFLAGS) -o Server/S2 Server/S2.c

S3: Server/S3.c
	$(CC) $(CFLAGS) -o Server/S3 Server/S3.c

S4: Server/S4.c
	$(CC) $(CFLAGS) -o Server/S4 Server/S4.c

w25clients: Client/w25clients.c
	$(CC) $(CFLAGS) -o Client/w25clients Client/w25clients.c

clean:
	rm -f Server/S1 Server/S2 Server/S3 Server/S4 Client/w25clients
```

---

## Usage

1. **Start each server** (one terminal per server):

   ```bash
   ./Server/S1
   ./Server/S2
   ./Server/S3
   ./Server/S4
   ```

2. **Run the client**:

   ```bash
   ./Client/w25clients
   ```

3. **Available commands** (enter at the client prompt):

   - `UPLOAD <local_path> <remote_path>`  
   - `DOWNLOAD <remote_path> <local_path>`  
   - `REMOVE <remote_path>`  
   - `CREATETAR <remote_directory> <tar_name>`  

   Replace `<remote_path>` with the server-specific prefix (e.g. `~S2/data.txt`).

---

## Project Structure

```
├── Client
│   └── w25clients.c
├── Server
│   ├── S1.c
│   ├── S2.c
│   ├── S3.c
│   └── S4.c
├── .gitignore
└── LICENSE
```

- **Client/**: Command-line interface  
- **Server/**: Four independent node implementations  
- **.gitignore**: Excludes object files and temporary artifacts  
- **LICENSE**: MIT License © 2025 Nilkanth Suthar  

---

## License

This project is released under the MIT License. See [LICENSE](LICENSE) for details.

---

## Future Work

- Add TLS for encrypted client–server communication  
- Implement automatic replication and failover  
- Introduce a central directory service or distributed hash table  
- Develop automated test suite and integration scripts