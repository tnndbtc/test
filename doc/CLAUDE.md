# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Reference

**Project:** C++17 blockweave implementation (similar to Arweave)
**Key Concept:** Proof-of-access consensus
**Documentation:**
- [design.md](design.md) - Architecture and design decisions
- [api.md](api.md) - REST API, RPC, and CLI documentation
- [naming_convention.txt](naming_convention.txt) - Code style guide

## Build

```bash
./configure
cd build && make
```

**Executables:**
- `bweave` - Main daemon
- `bweave_cli` - Control utility
- `wallet` - Address generator
- `test_all` - Unit tests (in src/test/build/)

**Libraries:** libbwthreadname, libbwlogger, libbwutils, libbwpeer, libbwblockcore, libbwrest

## Quick Start

```bash
# 1. Generate wallet
./wallet

# 2. Edit blockweave.conf
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
- PING sent every 30 seconds to maintain connections
- String-based message protocol

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

**blockweave.conf** settings (all optional except miner_address):

| Setting | Default | Description |
|---------|---------|-------------|
| `miner_address` | *required* | Wallet address for rewards |
| `rest_api_port` | 28443 | REST API port |
| `p2p_port` | 28333 | P2P listening port |
| `max_inbound_peers` | 120 | Max inbound connections |
| `max_outbound_peers` | 8 | Max outbound connections |
| `log_dir` | ./log | Log directory |
| `log_level` | INFO | FATAL/ERROR/WARN/INFO/TRACE |
| `data_dir` | ./data | Blockchain data storage |

Defaults defined in `src/utils/settings.h`.

## Threading Model

1. `main_thread` - Initialization and shutdown
2. `mining_thread` - Block mining loop
3. `rest_listener` + `rest_worker0-4` - HTTP request handling
4. `peer_manager` - P2P management, sends PING every 30s
5. `peer_listener` - Accept P2P connections
6. `peer_<address>` - Per-connection threads

All threads are named for debugging. Use mutexes (`cs_` prefix) and atomic flags for synchronization.

## Testing

**Unit Tests:** 136 tests across 12 modules

```bash
cd src/test
./build.sh
cd build
./test_all
```

**Test Modules:**
- test_peer_manager (18 tests)
- test_peer_message (21 tests)
- test_peer_filter (18 tests)
- test_request_queue (10 tests)
- test_api_server (5 tests)
- test_block (4 tests)
- test_transaction (5 tests)
- test_blockweave (7 tests)
- test_wallet (3 tests)
- test_hash (2 tests)
- test_blockfile (17 tests)
- test_logger (26 tests)

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

- **macOS**: Primary development platform
- **Linux**: Fully supported
- **Windows**: Configured but untested

Platform-specific defines: `PLATFORM_MACOS`, `PLATFORM_LINUX`, `PLATFORM_WINDOWS`

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
./configure --debug
cd build && make
```

**Clean Build:**
```bash
rm -rf build
./configure && cd build && make
```

## Security Notes

⚠️ **This is a demo/prototype system:**
- No authentication on APIs
- No input validation
- No rate limiting
- No encryption
- Runs as current user

For production, implement: authentication, TLS, input validation, rate limiting, privilege separation.

## Additional Resources

- **Design Document:** [doc/design.md](design.md) - Architecture, threading, P2P protocol, storage design
- **API Documentation:** [doc/api.md](api.md) - Complete REST API, RPC, CLI reference
- **Test Documentation:** `src/test/README.md` - Unit testing details
- **Naming Conventions:** [doc/naming_convention.txt](naming_convention.txt) - Code style rules

## Summary for Claude Code

When working on this codebase:
1. **Always follow naming conventions** (C prefix, m_ prefix, snake_case, etc.)
2. **Use existing patterns** (mutexes for shared data, atomic flags for simple state)
3. **Name all threads** using `SetThreadName()`
4. **Add tests** for new functionality in src/test/
5. **Document APIs** in doc/api.md for user-facing changes
6. **Document design** in doc/design.md for architectural changes
7. **Check CLAUDE.md** for quick reference before starting work
