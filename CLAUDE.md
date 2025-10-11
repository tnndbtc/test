# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a C++17 implementation of a blockweave system (similar to Arweave), which is a blockchain variant that implements proof-of-access consensus. Unlike traditional blockchains, blockweave requires miners to access data from a randomly selected previous block ("recall block") during mining, incentivizing permanent data storage.

**Key Dependencies:**
- OpenSSL (for cryptographic hashing)
- CMake 3.10+
- C++17 compiler (GCC/Clang)

## Build Commands

### Initial Configuration and Build

```bash
# Configure the project (detects platform, dependencies, generator)
./configure

# Build (from project root)
cd build && make

# Or use the shorthand from notes.txt
./configure --generator="Unix Makefiles"
cd build
make
```

### Configure Options

```bash
# Debug build
./configure --debug

# Custom install prefix
./configure --prefix=/opt/restdaemon

# Specify build directory
./configure --build-dir=debug_build

# Use Ninja instead of Make
./configure --generator=Ninja
```

### Running

```bash
# After building
./build/rest_daemon
```

## Architecture

### Core Components

**CBlockweave** (src/blockweave.h, src/blockweave.cpp)
- Main orchestrator managing the entire blockweave structure
- Maintains: block storage (`map_blocks`), block hash list (`m_block_hashes`), mempool
- Implements proof-of-access via `SelectRecallBlock()` which randomly selects historical blocks for mining
- Key operations: `AddTransaction()`, `MineBlock()`, `GetBlock()`, `GetData()`

**CBlock** (src/block.h, src/block.cpp)
- Individual block representation with unique recall block mechanism
- Fields: `m_previous_block`, `m_recall_block` (proof-of-access), `m_n_cumulative_data_size`
- The `m_recall_block` field is what distinguishes this from traditional blockchain

**CTransaction** (src/transaction.h, src/transaction.cpp)
- Represents data storage transactions
- Each transaction carries arbitrary data (`m_data`) and has a reward (`m_n_reward`)
- Transaction IDs are computed as hashes

**CWallet** (src/wallet.h, src/wallet.cpp)
- Manages addresses and transaction creation
- Generates unique addresses on instantiation

**CHash** (src/hash.h, src/hash.cpp)
- Cryptographic hash wrapper using OpenSSL SHA-256
- Used for block hashes, transaction IDs, and proof-of-work

**RestServer** (include/rest_server.h)
- Multi-threaded REST server interface (not yet integrated with main)
- Intended for HTTP API access to blockweave operations

### Blockweave vs Blockchain

The key architectural difference is the **recall block mechanism**:
1. When mining, a block must reference both the previous block AND a randomly selected historical block
2. The recall block is selected using: `SelectRecallBlock(current_height)`
3. This forces miners to maintain historical data, creating permanent storage incentives
4. The randomization is deterministic based on the current block's properties

## Naming Conventions

Per `naming_convention.txt`, this codebase follows specific conventions:

- **Classes/Structs**: `C` prefix + CapitalizedName (e.g., `CBlock`, `CTransaction`)
- **Member variables**: `m_` prefix (e.g., `m_hash`, `m_n_height`)
- **Local variables**: snake_case
- **Functions/Methods**: CamelCase
- **Boolean variables**: `f` prefix
- **Numeric variables**: `n_` prefix
- **String variables**: `str_` prefix
- **Map variables**: `map_` prefix
- **Global variables**: `g_` prefix
- **Static variables**: `s_` prefix
- **Constants**: ALL_CAPS_WITH_UNDERSCORES
- **Mutexes**: `cs_` prefix

When adding or modifying code, strictly adhere to these conventions.

## Platform Support

The build system (CMakeLists.txt + configure script) supports:
- **macOS**: Handles Homebrew OpenSSL paths (both ARM `/opt/homebrew` and Intel `/usr/local`)
- **Linux**: Uses system OpenSSL and pkg-config
- **Windows**: Configured but not currently tested in this environment

Platform-specific defines are set automatically:
- `PLATFORM_MACOS`
- `PLATFORM_LINUX`
- `PLATFORM_WINDOWS`

## Development Notes

- The executable is named `rest_daemon` but currently runs as a CLI demo
- REST server integration is incomplete (header exists but not linked in main)
- All source files include banner comments: `// ============= filename.ext =============`
- The project uses smart pointers (`std::shared_ptr`) for transaction and block management
