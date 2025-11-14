# Blockweave Design Document

## Overview

This document describes the architecture and design decisions for the blockweave implementation, a blockchain variant that implements proof-of-access consensus for permanent data storage.

## Threading Architecture

The system uses a multi-threaded architecture with named threads for concurrent operation:

### Thread Types

1. **Main Thread** (`main_thread`)
   - Initializes all components
   - Waits for shutdown signal (SIGTERM/SIGINT)
   - Coordinates graceful shutdown sequence

2. **Mining Thread** (`mining_thread`)
   - Continuously mines new blocks when enabled
   - Checks mempool for pending transactions
   - Performs proof-of-work computation
   - Sleeps 500ms after mining, 100ms when idle

3. **REST API Threads**
   - **Listener Thread** (`rest_listener`) - Accepts incoming HTTP connections
   - **Worker Threads** (`rest_worker0-4`) - Process HTTP requests from queue
   - Default: 5 worker threads (configurable in settings.h)
   - Uses producer/consumer pattern with condition variables

4. **P2P Network Threads**
   - **Peer Manager Thread** (`peer_manager`) - Manages P2P connections, sends PING every 30 seconds
   - **Peer Listener Thread** (`peer_listener`) - Accepts incoming P2P connections
   - **Peer Connection Threads** (`peer_<address>`) - One thread per active peer
   - Separate tracking for inbound (max 120) and outbound (max 8) connections

### Thread Synchronization

- **Mutexes**: Protect shared data structures (prefix: `cs_`)
  - `cs_blockweave` - Protects blockchain state
  - `cs_peers` - Protects peer connection lists
  - `cs_filter` - Protects peer filter mappings

- **Atomic Flags**: For simple boolean state
  - `f_running` - Component running state
  - `f_stop_requested` - Shutdown signal
  - `f_active` - Thread activity state

- **Condition Variables**: For efficient waiting
  - Request queue uses CV for worker threads
  - No busy-waiting patterns

### Thread Naming

All threads are named for easier debugging:
- Uses `SetThreadName()` utility (pthread-based)
- Names limited to 15 characters (pthread limitation)
- Visible in logs: `[timestamp] [level] [process:thread_name] message`
- Visible in debuggers and system monitoring tools

## Component Architecture

### Core Components

```
bweave: main process

CBlockweave: orchestrator

CMiningManager: mining loop

CRestApiServer: HTTP and RPC interface

CPeerManager: P2P network
```

### Library Architecture

Modular design with shared libraries and clear dependencies:

```
libbwthreadname (no dependencies)

libbwlogger (depends on threadname)

libbwutils (depends on logger, threadname)

libbwpeer (depends on logger, threadname)

libbwblockcore (depends on utils, logger, threadname, peer)

libbwrest (depends on blockcore, utils, logger, threadname)
```

**Design Benefits:**
- Clear dependency hierarchy
- Easy to test individual components
- Can be used by external applications
- Facilitates incremental builds

## P2P Network Architecture

### Connection Types

**Inbound Connections** (peers connecting to us):
- Default limit: 120 peers
- Accept connections on P2P port (default 28333)
- No initiation control
- Subject to connection flood

**Outbound Connections** (we connect to them):
- Default limit: 8 peers
- Actively initiated by node
- More reliable and controlled
- Used for critical network topology

### Message Protocol

String-based message format with network isolation via magic bytes:
```
[4 bytes magic][1 byte type_length][N bytes type_string][4 bytes payload_length][M bytes payload]
```

**Network Magic Bytes:**
- Mainnet:  `0x8AC65DF3`
- Testnet:  `0xEA71B96E`
- Localnet: `0xACDE4892`

**Message Types:**
- `ping` / `pong` - Connection keep-alive (PING sent every 30 seconds)
- `get_peers` - Request peer list
- `tx_ids` - Broadcast transaction IDs
- `block` - Broadcast block data
- `get_chain` - Request blockchain state
- `inv` - Inventory announcement (notify peers about transactions/blocks)
- `getdata` - Request specific data by hash

**Design Rationale:**
- **Magic bytes** prevent cross-network communication (e.g., mainnet node won't accept testnet messages)
- Variable-length type strings allow easy addition of new message types
- Network byte order (big-endian) for cross-platform compatibility
- Explicit payload length prevents buffer overruns
- Early magic validation rejects wrong-network messages with minimal CPU cost

### Peer Filter Design

Prevents redundant broadcasts by tracking peer knowledge:

```
CPeerFilter
 map_tx_peers:     map<TX_ID, set<peer_identifier>>
 map_block_peers:  map<block_hash, set<peer_identifier>>
```

**Workflow:**
1. Node receives TX_IDS message from Peer A
2. Record: `filter.AddTxIdForPeer(tx_id, peer_A)`
3. When broadcasting same TX:
   - Get all connected peers
   - Filter out peers who already know
   - Broadcast only to filtered list

**Benefits:**
- Reduces network bandwidth
- Prevents broadcast storms
- Maintains O(1) lookup performance

## Multi-Network Deployment

The blockweave supports three distinct networks for different deployment scenarios:

### Network Types

| Network | Magic Bytes | REST Port | P2P Port | Block Time | Use Case |
|---------|------------|-----------|----------|------------|----------|
| **Mainnet** | `0xBEEFCAFE` | 28443 | 28333 | 600s (10min) | Production deployment |
| **Testnet** | `0xDEADBEEF` | 38443 | 38333 | 60s (1min) | Public testing, experimentation |
| **Localnet** | `0xCAFEBABE` | 48443 | 48333 | 1s | Local development, functional tests |

### Network Isolation Mechanisms

**1. P2P Protocol Level (Primary)**
- Every P2P message includes 4-byte network magic as first field
- Peer manager validates magic on all incoming messages
- Cross-network messages rejected immediately
- Zero risk of accidental cross-network communication

**2. Data Directory Isolation**
- Each network uses separate data directory: `data/{network}/`
- Prevents blockchain data mixing
- Allows running multiple networks on same machine (different ports)

**3. Configuration**
- Network specified via `network=` config option or `--network` CLI flag
- Defaults to mainnet for safety
- Network parameters loaded from `src/utils/network.h`

### Security Benefits

**Cross-Network Protection:**
```
Mainnet node ← PING(magic=0xBEEFCAFE) ← Mainnet peer ✅ Accepted
Mainnet node ← PING(magic=0xDEADBEEF) ← Testnet peer ❌ Rejected
```

**Prevents:**
- Accidental testnet transactions on mainnet
- Replay attacks across networks
- Wrong-network peer connections
- Data directory confusion

### Implementation

Network magic bytes integrated throughout P2P stack:
- `CPeerMessage` - Serializes/deserializes magic bytes
- `CPeerManager` - Validates magic on all received messages
- `main.cpp` - Passes network magic from config to peer manager

## Storage Architecture

### Block Storage

Blocks are stored in individual files in the data directory:

```
data/
 blocks/
     block_0000000000.dat  (genesis block)
     block_0000000001.dat
     block_0000000002.dat
     ...
```

**File Format:**
- One block per file
- Filename includes height (zero-padded)
- Serialized block data
- Index file tracks all block hashes

**Design Rationale:**
- Simple to implement and debug
- Easy to backup individual blocks
- No database dependencies
- Direct file I/O performance

### Transaction Storage

Transactions are stored within blocks:
- Not stored separately in mempool on disk
- Pending transactions lost on restart (acceptable for demo)
- Production system would persist mempool

## Configuration System

### Hierarchy

```
1. Compile-time defaults (src/utils/settings.h)
   ?
2. Configuration file (bweave.conf)
   ?
3. Command-line overrides (-d for daemon mode)
```

### Settings Structure

**CConfig** class provides:
- Key-value file parser (simple format)
- Type-safe accessors (string, int, bool)
- Default value fallback
- Runtime modification via SetValue()

**Design Benefits:**
- Clear precedence rules
- Easy to add new settings
- No external dependencies (no JSON/YAML libraries)
- Fail-safe defaults

## Daemon Process Management

### Process Control

**CDaemon** class handles:
- Fork to background process
- Session creation (setsid)
- Working directory change to root
- File descriptor closing
- PID file management (/tmp/bweave.pid)
- Signal handlers (SIGTERM, SIGINT)

**bweave_cli** utility:
- Lightweight controller (no library dependencies)
- Process discovery via PID file
- Graceful shutdown with timeout
- Executable location detection

### Shutdown Sequence

```
1. Receive SIGTERM/SIGINT signal
2. Set g_f_shutdown_requested flag
3. Main thread breaks from wait loop
4. Stop mining manager
5. Stop peer manager (closes all connections)
6. Stop REST API server (closes all sockets)
7. Print final blockchain state
8. Remove PID file
9. Flush logs and exit
```

## Logging System

### Design

**CLogger** provides:
- Thread-safe logging with mutex protection
- Timestamped messages (UTC)
- Process and thread name in each log entry
- Log level filtering (FATAL?ERROR?WARN?INFO?TRACE)
- Automatic log rotation by size
- Console output for errors

**Log Format:**
```
[2025-10-28 01:05:20.261 UTC] [INFO ] [bweave:peer_manager] Message here
```

**Design Rationale:**
- Easy to parse and analyze
- Thread information helps debug concurrency issues
- UTC timestamps avoid timezone confusion
- Single logger instance (global singleton)

## Security Considerations

### Current Implementation

**This is a demo/prototype system with limited security:**

- No authentication on REST API
- No peer authentication
- No transaction validation
- No DOS protection
- No rate limiting
- Daemon runs as current user

### Production Requirements

For production use, would need:
- TLS for REST API and P2P connections
- Peer reputation system
- Rate limiting on all endpoints
- Input validation and sanitization
- Memory and CPU limits
- Proper privilege separation
- Transaction signature verification
- Merkle tree verification for blocks

## Performance Considerations

### Optimization Strategies

**Current:**
- Lock-free operations where possible (atomic flags)
- Fine-grained locking (separate mutexes for different subsystems)
- Non-blocking sockets for P2P connections
- Worker thread pool for REST API
- TCP keep-alive to detect dead connections
- Periodic PING messages (every 30 seconds) to maintain connections

**Future Improvements:**
- Block caching in memory
- Transaction pool indexing
- Peer scoring for better selection
- Parallel block validation
- Memory-mapped file I/O for blocks

## Testing Strategy

### Unit Tests

- **136 tests** across 12 modules
- Custom test framework (no external dependencies)
- Focus on thread safety and edge cases
- Concurrent operation tests (5-10 threads)

### Test Categories

1. **Component Tests**: Individual class functionality
2. **Integration Tests**: Multiple components working together
3. **Thread Safety Tests**: Concurrent access patterns
4. **Edge Case Tests**: Empty inputs, large payloads, boundary conditions

See `src/test/README.md` for complete testing documentation.

## Future Enhancements

### Planned Features

1. **Enhanced P2P:**
   - Peer discovery (DHT or seed nodes)
   - Peer reputation system
   - Connection encryption

2. **Storage Improvements:**
   - Database backend option (SQLite/LevelDB)
   - Memory pool persistence
   - Block pruning for light clients

3. **API Enhancements:**
   - WebSocket support for real-time updates
   - GraphQL query interface
   - Batch operations

4. **Performance:**
   - ASIC-resistant mining algorithm
   - Parallel transaction validation
   - Sharding for scalability

5. **Monitoring:**
   - Prometheus metrics export
   - Health check endpoints
   - Performance profiling hooks
