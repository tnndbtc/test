# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Reference

**Project:** C++17 blockweave implementation (similar to Arweave)
**Key Concept:** Proof-of-access consensus
**Networks:** Mainnet (production), Testnet (public testing), Localnet (dev/testing)
**Documentation:**
- [design.md](design.md) - Architecture and design decisions
- [api.md](api.md) - REST API, RPC, and CLI documentation
- [naming_convention.txt](naming_convention.txt) - Code style guide

**Network Configuration:**
```bash
# Mainnet (default): Block time 10min, ports 28443/28333
./bweave

# Testnet: Block time 1min, ports 38443/38333
./bweave --network testnet

# Localnet: Block time 1s, ports 48443/48333, fast mining
./bweave --network localnet
```

## Dependencies

**Required:**
- C++17 compiler (GCC 7+, Clang 5+, or MSVC 2017+)
- CMake 3.10+
- OpenSSL 1.1.0+
- **Boost 1.70+** (for Boost.Asio async I/O)

**Installation:**

macOS:
```bash
brew install cmake openssl boost
```

Linux (Ubuntu/Debian):
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libssl-dev libboost-all-dev
```

Linux (RHEL/CentOS):
```bash
sudo yum install gcc-c++ cmake openssl-devel boost-devel
```

Windows:
- Download Boost from [boost.org](https://www.boost.org/users/download/)
- Or use vcpkg: `vcpkg install boost-asio boost-system`

**Verify installation:**
```bash
boost --version  # Should show >= 1.70
openssl version  # Should show >= 1.1.0
cmake --version  # Should show >= 3.10
```

## Build

**Standard CMake Workflow:**

```bash
# Debug build (with sanitizers on Linux/macOS)
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -- -j$(nproc)

# Release build (optimized)
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -- -j$(nproc)

# Build with tests enabled
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON ..
cmake --build . -- -j$(nproc)

# Run tests (when built with -DBUILD_TESTS=ON)
./src/test/test_all
# Or use CTest
ctest --output-on-failure
```

**One-liner for quick builds:**
```bash
# Without tests (default)
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build . -- -j$(nproc)

# With tests
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON .. && cmake --build . -- -j$(nproc)
```

**Executables:**
- `bweave` - Main daemon
- `bweave_cli` - Control utility
- `wallet` - Address generator
- `test_all` - Unit tests (in build/src/test/, only built with -DBUILD_TESTS=ON)

**Libraries:** libbwthreadname, libbwlogger, libbwutils, libbwpeer, libbwblockcore, libbwrest

**Build Options:**
- `-DCMAKE_BUILD_TYPE=Debug` - Debug build with sanitizers (Linux/macOS)
- `-DCMAKE_BUILD_TYPE=Release` - Optimized release build
- `-DBUILD_TESTS=ON` - Build unit tests (default: OFF)
- OpenSSL detection automatically handles macOS Homebrew paths

## Quick Start

```bash
# 1. Generate wallet
./wallet

# 2. Edit bweave.conf
miner_address=<generated_address>

# 3. Start daemon
./bweave_cli start

# 4. Check status
curl http://localhost:28443/chain

# 5. Stop daemon
./bweave_cli stop
```

## Project Structure

```
src/
├── main.cpp                    # Daemon entry point
├── blockcore/                  # Blockchain core (CBlockweave, CBlock, CTransaction)
├── rest/                       # REST API server
├── peer/                       # P2P networking
├── cli/                        # CLI utilities
├── wallet/                     # Wallet functionality
├── logger/                     # Logging system
├── utils/                      # Hash, config, thread naming, settings
└── test/                       # Unit tests (136 tests)
```

## Core Components

**CBlockweave** (src/blockcore/blockweave.h)
- Main orchestrator for blockweave operations
- Thread-safe with `cs_blockweave` mutex
- Key methods: `AddTransaction()`, `MineBlock()`

**CBlock** (src/blockcore/block.h)
- References previous block for proof-of-access

**CPeerManager** (src/peer/peer_manager.h)
- P2P networking: max 120 inbound, 8 outbound peers
- **Hybrid I/O Architecture** (Boost.Asio):
  - Inbound connections: Async I/O with thread pool (1 monitor + 120 workers)
  - Outbound connections: Thread-per-connection (8 threads max)
  - Reduces thread count from 130+ to ~43 threads (67% reduction)
- PING sent every 30 seconds to maintain connections
- String-based message protocol: `[type_length][type][payload_length][payload]`
- Automatic cleanup of disconnected peers with async operation cancellation

**CRestApiServer** (src/rest/rest_api_server.h)
- Multi-threaded HTTP server (1 listener + 5 workers)
- Endpoints: /chain, /transaction, /files, /mine/start, /mine/stop
- RPC: /rpc/addpeer, /rpc/getpeer

**CPeerFilter** (src/peer/peer_filter.h)
- Tracks which peers know about transactions/blocks
- Prevents redundant broadcasts

## Naming Conventions

Strictly follow these conventions from `naming_convention.txt`:

- **Classes/Structs**: `CClassName` (C prefix)
- **Member variables**: `m_variable_name` (m_ prefix)
- **Local variables**: `snake_case`
- **Functions**: `CamelCase`
- **Boolean**: `f_flag_name` (f prefix)
- **Numeric**: `n_number` (n_ prefix)
- **String**: `str_name` (str_ prefix)
- **Map**: `map_name` (map_ prefix)
- **Mutex**: `cs_lock_name` (cs_ prefix)
- **Constants**: `ALL_CAPS_WITH_UNDERSCORES`

## Configuration

**bweave.conf** settings (all optional except miner_address):

| Setting | Default | Description |
|---------|---------|-------------|
| `miner_address` | *required* | Wallet address for rewards |
| `network` | mainnet | Network type: mainnet/testnet/localnet |
| `rest_api_port` | *varies* | REST API port (28443/38443/48443 by network) |
| `p2p_port` | *varies* | P2P listening port (28333/38333/48333 by network) |
| `max_inbound_peers` | 120 | Max inbound connections |
| `max_outbound_peers` | 8 | Max outbound connections |
| `log_dir` | ./log | Log directory |
| `log_level` | INFO | FATAL/ERROR/WARN/INFO/TRACE |
| `data_dir` | ./data | Blockchain data storage (network-specific subdirs created) |

Defaults defined in `src/utils/settings.h` and `src/utils/network.h`.

**Network-Specific Behavior:**
- Data stored in `{data_dir}/{network}/` subdirectory
- Port defaults vary by network (see Quick Reference above)
- Magic bytes used for P2P message validation (prevents cross-network communication)
- Localnet has 1-second block time for fast testing

## Threading Model

**Main Application Threads:**
1. `main_thread` - Initialization and shutdown
2. `mining_thread` - Block mining loop
3. `rest_listener` + `rest_worker0-4` - HTTP request handling (6 threads)

**P2P Networking Threads (Boost.Asio Hybrid Architecture):**
4. `peer_manager` - P2P management, sends PING every 30s
5. `peer_listener` - Accept inbound P2P connections
6. `monitor_inbound` - Boost.Asio I/O multiplexing for inbound sockets
7. `inbound_worker_<id>` - Thread pool for inbound message processing (120 workers)
8. `peer_<address>` - Outbound connection threads (8 max, thread-per-connection)

**Thread Count:**
- Base: 3 threads (main, mining, rest_listener)
- REST workers: 5 threads
- P2P core: 3 threads (peer_manager, peer_listener, monitor_inbound)
- Inbound workers: 120 threads (thread pool)
- Outbound peers: 8 threads max (active connections)
- **Total: ~139 threads max** (down from 130+ with old architecture)

All threads are named using `SetThreadName()` for debugging. Use mutexes (`cs_` prefix) and atomic flags (`f_` prefix) for synchronization.

## Testing

**Unit Tests:** 262 tests across 13 modules

Tests are built when `-DBUILD_TESTS=ON` is specified:

```bash
# Build with tests enabled
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON ..
cmake --build . -- -j$(nproc)

# Run tests directly
./src/test/test_all

# Or use CTest
ctest --output-on-failure
```

**Note:** By default, tests are NOT built. Use `-DBUILD_TESTS=ON` to enable test building.

**Test Modules:**
- test_peer_manager (18 tests)
- test_peer_message (21 tests)
- test_peer_filter (21 tests)
- test_request_queue (10 tests)
- test_api_server (5 tests)
- test_block (4 tests)
- test_transaction (5 tests)
- test_blockweave (7 tests)
- test_wallet (3 tests)
- test_hash (10 tests)
- test_blockfile (17 tests)
- test_logger (26 tests)
- test_network (14 tests)
- test_get_public_ip (6 tests)

Custom test framework in `unit_test.h`, no external dependencies.

## Key Design Decisions

**Blockweave vs Blockchain:**
- Blocks reference both previous block AND random historical block
- Forces miners to store historical data
- Incentivizes permanent storage

**String-Based P2P Messages:**
```
[1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
```
- Extensible (easy to add new message types)
- Network byte order (big-endian)

**Separate Inbound/Outbound Peers:**
- Inbound: High limit (120), passive acceptance
- Outbound: Low limit (8), active connection
- Different roles in network topology

**File-Based Block Storage:**
- Simple, no database dependencies
- One file per block: `data/blocks/block_0000000001.dat`
- Easy to backup and debug

## Platform Support

- **macOS**: Fully supported (POSIX daemon)
- **Linux**: Fully supported (POSIX daemon)
- **Windows**: Fully supported (Windows Service + Console daemon)

Platform-specific defines: `PLATFORM_MACOS`, `PLATFORM_LINUX`, `PLATFORM_WINDOWS`

### Windows Service Support

**bweave** runs as a native Windows Service with full Service Control Manager (SCM) integration.

**Three run modes on Windows:**
1. **Service mode** (recommended): Runs under SCM control, managed via services.msc
2. **Console mode**: Runs as foreground process with Ctrl+C support
3. **Console daemon mode**: Runs detached from console with PID file (like POSIX)

**Installation and Management:**

```cmd
REM Install as Windows service (requires Administrator)
bweave.exe --install-service

REM Or use bweave_cli
bweave_cli install

REM Start service
bweave_cli start
REM Or: net start bweave
REM Or: services.msc GUI

REM Check status
bweave_cli status
REM Or: sc query bweave

REM Stop service
bweave_cli stop
REM Or: net stop bweave

REM Uninstall service
bweave.exe --uninstall-service
REM Or: bweave_cli uninstall
```

**Console daemon mode** (when not installed as service):
```cmd
REM Start console daemon (background process with PID file)
bweave.exe -d

REM Or use bweave_cli
bweave_cli start

REM Stop console daemon
bweave_cli stop
```

**Console mode** (foreground):
```cmd
REM Run in console (Ctrl+C to stop)
bweave.exe
```

**Service configuration:**
- Service name: `bweave`
- Display name: `Blockweave Daemon`
- Startup type: Automatic
- Account: LocalSystem (default)
- Dependencies: None

**Implementation details:**
- **src/blockcore/win_service.h/cpp**: Windows Service wrapper
  - `ServiceMain()` - SCM entry point
  - `ServiceCtrlHandler()` - Handles STOP/SHUTDOWN control codes
  - `ConsoleCtrlHandler()` - Handles Ctrl+C in console mode
  - `InstallService()/UninstallService()` - Service installation
- **src/main.cpp**: Automatic mode detection
  - Detects service vs console mode via `IsRunningAsService()`
  - Calls `StartServiceDispatcher()` in service mode
  - Registers `ConsoleCtrlHandler()` in console mode
- **src/cli/bweave_cli.cpp**: Cross-platform CLI
  - Windows: Uses SCM APIs when service installed, CreateProcess for console daemon
  - POSIX: Uses fork/exec and signals

**Shutdown mechanism:**
- **Service mode**: `ServiceCtrlHandler()` sets `g_f_shutdown_requested` on SERVICE_CONTROL_STOP
- **Console mode**: `ConsoleCtrlHandler()` sets `g_f_shutdown_requested` on Ctrl+C
- **Console daemon**: `TerminateProcess()` via bweave_cli (not graceful)
- **POSIX**: Signal handler sets `g_f_shutdown_requested` on SIGTERM/SIGINT

All modes use the same shutdown flag for consistent graceful shutdown.

## Development Workflow

**Adding a Feature:**
1. Follow naming conventions strictly
2. Use appropriate prefixes (`C`, `m_`, `f_`, `n_`, `str_`, `cs_`)
3. Add unit tests if applicable
4. Update relevant documentation (design.md or api.md)
5. Test with ./test_all and manual testing

**Adding an API Endpoint:**
1. Add handler method to CRestApiServer
2. Update HandleGET() or HandlePOST() routing
3. Document in doc/api.md
4. Test with curl commands

**Adding a Configuration Option:**
1. Add default to src/utils/settings.h
2. Add to CConfig::LoadDefaults() in src/utils/config.cpp
3. Add getter method to CConfig class
4. Document in doc/api.md configuration section

## Common Tasks

**View Logs:**
```bash
tail -f build/log/bweave.log
grep ERROR build/log/bweave.log
```

**Check Daemon Status:**
```bash
./bweave_cli status
lsof -i :28443  # REST API port
lsof -i :28333  # P2P port
```

**Debug Build:**
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -- -j$(nproc)
```

**Clean Build:**
```bash
rm -rf build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -- -j$(nproc)
```

## Security Notes

⚠️ **This is a demo/prototype system:**
- No authentication on APIs
- No input validation
- No rate limiting
- No encryption
- Runs as current user

For production, implement: authentication, TLS, input validation, rate limiting, privilege separation.

## Boost.Asio P2P Architecture

The P2P networking uses a **hybrid I/O model** combining async I/O for inbound connections with thread-per-connection for outbound:

### Inbound Connections (Async I/O)
```
peer_listener thread
  └─> accept() new connections
       └─> RegisterInboundSocket()
            └─> wrap in boost::asio::posix::stream_descriptor
                 └─> async_read_some() → monitor_inbound thread

monitor_inbound thread
  └─> io_context.run() (select/epoll/poll multiplexing)
       └─> HandleAsyncRead() on data available
            └─> post to thread_pool
                 └─> ProcessReceivedMessage() (inbound_worker_<id>)
                      └─> SendMessageAsync() → async_write()
```

### Outbound Connections (Thread-per-connection)
```
AddPeer() → ConnectToPeer()
  └─> spawn OutboundConnectionThread (peer_<address>)
       └─> blocking recv() loop
            └─> process messages
                 └─> blocking send() for responses
```

### Key Components

**peer_manager.h members:**
- `boost::asio::io_context m_io_context` - Event loop for async I/O
- `boost::asio::thread_pool m_thread_pool` - Worker threads (120)
- `map<int, stream_descriptor> map_inbound_descriptors` - Socket FD to async descriptor map
- `std::mutex cs_inbound_descriptors` - Protects descriptor map

**Key methods:**
- `RegisterInboundSocket()` - Wrap socket in stream_descriptor, start async_read_some
- `HandleAsyncRead()` - Completion handler, posts work to thread pool
- `ProcessReceivedMessage()` - Worker thread processes PING/PONG/etc
- `SendMessageAsync()` - Async write for inbound peers
- `CleanupDisconnectedPeers()` - Cancels async ops, closes descriptors

**Error Handling:**
All async error paths properly:
1. Cancel pending operations: `descriptor->cancel()`
2. Close descriptor: `descriptor->close()`
3. Mark peer disconnected: `f_connected = false`

### Benefits
- **Scalability:** 120 inbound peers without 120 threads
- **Efficiency:** Single I/O thread monitors all inbound sockets
- **Simplicity:** Outbound unchanged (no migration needed)
- **Thread reduction:** 130+ threads → ~43 threads (67% reduction)

## Additional Resources

- **Design Document:** [doc/design.md](design.md) - Architecture, threading, P2P protocol, storage design
- **API Documentation:** [doc/api.md](api.md) - Complete REST API, RPC, CLI reference
- **Test Documentation:** `src/test/README.md` - Unit testing details
- **Naming Conventions:** [doc/naming_convention.txt](naming_convention.txt) - Code style rules
- **Boost.Asio Evaluation:** [doc/boost_asio_evaluation.md](boost_asio_evaluation.md) - Detailed async I/O implementation analysis

## Summary for Claude Code

When working on this codebase:
1. **Always follow naming conventions** (C prefix, m_ prefix, snake_case, etc.)
2. **Use existing patterns** (mutexes for shared data, atomic flags for simple state)
3. **Name all threads** using `SetThreadName()`
4. **Add tests** for new functionality in src/test/
5. **Document APIs** in doc/api.md for user-facing changes
6. **Document design** in doc/design.md for architectural changes
7. **Check CLAUDE.md** for quick reference before starting work
