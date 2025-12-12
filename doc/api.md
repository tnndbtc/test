# API Documentation

This document describes all APIs for interacting with the blockweave node: REST API, RPC endpoints, and CLI utilities.

## REST API

The REST API server runs on port 28443 (configurable via `rest_api_port` in bweave.conf).

### Endpoints

#### GET /chain

Get current blockchain state.

**Request:**
```bash
curl http://localhost:28443/chain
```

**Response:**
```json
{
  "mempool_size": 0,
  "mining_enabled": "true"
}
```

**Fields:**
- `mempool_size` (integer) - Number of pending transactions
- `mining_enabled` (boolean) - Whether mining is currently active

---

#### GET /transaction

Retrieve transaction data by hash.

**Request:**
```bash
curl "http://localhost:28443/transaction?hash=a1b2c3d4e5f6..."
```

**Query Parameters:**
- `hash` (string, required) - Transaction hash (hex string)

**Response (Success):**
```json
{
  "status": "success",
  "transaction_hash": "a1b2c3d4e5f6...",
  "data_hex": "48656c6c6f20426c6f636b636861696e206461746121",
  "data_size": 23
}
```

**Response (Error - Missing Hash):**
```json
{
  "error": "Bad Request",
  "message": "Missing query parameter 'hash'"
}
```

**Response (Error - Not Found):**
```json
{
  "error": "Not Found",
  "message": "Transaction not found"
}
```

**Fields:**
- `status` (string) - "success"
- `transaction_hash` (string) - Transaction hash
- `data_hex` (string) - Transaction data encoded as hex string
- `data_size` (integer) - Size of transaction data in bytes

**Status Codes:**
- 200 OK - Transaction found
- 400 Bad Request - Missing or invalid hash parameter
- 404 Not Found - Transaction not found
- 500 Internal Server Error - Server error

**Notes:**
- No authentication required
- Transaction data is hex-encoded to safely handle binary data
- Searches all blocks in the blockchain

---

#### GET /block

Retrieve block information by hash.

**Request:**
```bash
curl "http://localhost:28443/block?hash=b1c2d3e4f5a6..."
```

**Query Parameters:**
- `hash` (string, required) - Block hash (hex string)

**Response (Success):**
```json
{
  "status": "success",
  "block_hash": "b1c2d3e4f5a6...",
  "height": 42,
  "timestamp": 1234567890,
  "nonce": 123456,
  "transaction_count": 5,
  "miner": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
  "difficulty": 1000,
  "cumulative_data_size": 1048576,
  "previous_block": "a1b2c3d4e5f6..."
}
```

**Response (Error - Missing Hash):**
```json
{
  "error": "Bad Request",
  "message": "Missing query parameter 'hash'"
}
```

**Response (Error - Not Found):**
```json
{
  "error": "Not Found",
  "message": "Block not found"
}
```

**Fields:**
- `status` (string) - "success"
- `block_hash` (string) - Block hash
- `height` (integer) - Block height in chain
- `timestamp` (integer) - UNIX timestamp
- `nonce` (integer) - Mining nonce
- `transaction_count` (integer) - Number of transactions in block
- `miner` (string) - Miner address
- `difficulty` (integer) - Block difficulty
- `cumulative_data_size` (integer) - Total data size up to this block
- `previous_block` (string) - Previous block hash

**Status Codes:**
- 200 OK - Block found
- 400 Bad Request - Missing or invalid hash parameter
- 404 Not Found - Block not found
- 500 Internal Server Error - Server error

**Notes:**
- No authentication required
- Checks memory first, then loads from disk if needed

---

## RPC API

**Authentication Required:** All `/rpc/*` endpoints require HTTP Basic Authentication using credentials from the `.cookie` file located in `{data_dir}/{network}/.cookie`. The cookie file format is `__cookie__:<64 hex characters>`.

**Example with authentication:**
```bash
# Load credentials
COOKIE=$(cat data/localnet/.cookie)

# Make authenticated request
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/transaction \
  -H "Content-Type: application/json" \
  -d '{"from":"addr1","to":"addr2","data":"test","fee":1}'
```

RPC endpoints for transaction submission, peer management, and mining control.

---

### POST /rpc/transaction

Submit a new transaction to the mempool (requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Submit transaction
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
    "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
    "data": "Hello Blockchain data!",
    "fee": 1
  }'
```

**Request Body:**
- `from` (string, required) - Sender address
- `to` (string, required) - Recipient address
- `data` (string, required) - Transaction data (plain text string)
- `fee` (number, optional) - Transaction fee, default: 0

**Response (Success):**
```json
{
  "status": "success",
  "transaction_id": "a1b2c3d4e5f6...",
  "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
  "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
  "data_size": 23,
  "fee": 1
}
```

**Response (Error - Missing Authentication):**
```json
{
  "error": "Unauthorized",
  "message": "Authentication required"
}
```

**Response (Error - Missing Fields):**
```json
{
  "error": "Bad Request",
  "message": "Missing required field: from, to, data"
}
```

**Status Codes:**
- 200 OK - Transaction accepted
- 400 Bad Request - Missing or invalid fields
- 401 Unauthorized - Missing or invalid authentication
- 500 Internal Server Error - Server error

**Notes:**
- **Authentication required** - Must provide HTTP Basic Auth with cookie credentials
- Transaction is added to mempool
- Will be included in next mined block

---

### POST /rpc/addpeer

Add an outbound peer connection (requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Add peer
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/addpeer \
  -H "Content-Type: application/json" \
  -d '{
    "address": "127.0.0.1",
    "port": 48334
  }'
```

**Request Body:**
- `address` (string, required) - Peer IP address or hostname
- `port` (integer, optional) - Peer P2P port, default: 28333

**Response (Success):**
```json
{
  "status": "success",
  "address": "127.0.0.1",
  "port": 28334,
  "message": "Peer connection initiated"
}
```

**Response (Failed):**
```json
{
  "status": "failed",
  "address": "127.0.0.1",
  "port": 28334,
  "message": "Failed to initiate peer connection"
}
```

**Response (Error - Missing Address):**
```json
{
  "error": "Bad Request",
  "message": "Missing required field: address"
}
```

**Response (Error - Invalid Port):**
```json
{
  "error": "Bad Request",
  "message": "Invalid port value (must be 1-65535)"
}
```

**Status Codes:**
- 200 OK - Connection attempt made (check status field)
- 400 Bad Request - Missing/invalid parameters
- 500 Internal Server Error - Server error

**Notes:**
- Connection is asynchronous (initiated in background)
- Subject to max outbound peers limit (default: 8)
- Returns immediately, connection may fail later

---

### GET /rpc/getpeer

Get list of all connected peers (requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Get peer list
curl -u "$COOKIE" http://localhost:48443/rpc/getpeer
```

**Response:**
```json
{
  "status": "success",
  "total_peers": 3,
  "outbound_peers": 2,
  "inbound_peers": 1,
  "peers": [
    "192.168.1.100:28333",
    "192.168.1.101:28333",
    "10.0.0.50:28334"
  ]
}
```

**Fields:**
- `status` (string) - Always "success"
- `total_peers` (integer) - Total connected peers (inbound + outbound)
- `outbound_peers` (integer) - Number of outbound connections
- `inbound_peers` (integer) - Number of inbound connections
- `peers` (array of strings) - List of "address:port" strings

**Status Codes:**
- 200 OK - Peer list retrieved
- 401 Unauthorized - Missing or invalid authentication
- 500 Internal Server Error - Server error

---

### POST /rpc/minetrigger

Mine one block immediately (localnet only, requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Trigger block mining
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/minetrigger
```

**Response (Success):**
```json
{
  "status": "success",
  "message": "Block mined successfully",
  "block_height": 42
}
```

**Response (Forbidden - Wrong Network):**
```json
{
  "error": "Forbidden",
  "message": "Mining control is only available on localnet. Current network: mainnet"
}
```

**Status Codes:**
- 200 OK - Block mined
- 403 Forbidden - Not running on localnet
- 500 Internal Server Error - Mining failed or server error

**Notes:**
- **Localnet only** - Returns 403 Forbidden on mainnet/testnet
- Mines exactly one block immediately (if mempool has transactions)
- Returns immediately without blocking
- Works regardless of continuous mining state (minestart/minestop)
- Useful for functional tests to deterministically trigger mining

---

### POST /rpc/setmocktime

Set mock time for testing time-dependent logic (localnet only, requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Set mock time
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/setmocktime \
  -H "Content-Type: application/json" \
  -d '{"time": 1234567890}'
```

**Request Body:**
- `time` (integer, required) - UNIX timestamp in seconds, or 0 to disable mock time

**Response (Success):**
```json
{
  "result": "success",
  "mocktime": 1234567890,
  "enabled": true
}
```

**Response (Disabled):**
```json
{
  "result": "success",
  "mocktime": 0,
  "enabled": false
}
```

**Response (Forbidden - Wrong Network):**
```json
{
  "error": "setmocktime only available on localnet"
}
```

**Response (Bad Request - Invalid Time):**
```json
{
  "error": "Invalid time value"
}
```

**Response (Bad Request - Missing Field):**
```json
{
  "error": "Missing required field 'time'"
}
```

**Status Codes:**
- 200 OK - Mock time set successfully
- 400 Bad Request - Invalid time value or missing field
- 403 Forbidden - Not running on localnet
- 500 Internal Server Error - Server error

**Notes:**
- **Localnet only** - Returns 403 Forbidden on mainnet/testnet
- When mock time is set (non-zero), all time-dependent operations use the mock time instead of real system time
- Set to 0 to disable mock time and return to real system time
- Mock time is not persisted - restarting the daemon resets to real time
- Affects time-dependent operations:
  - Outbound peer rotation (every 1800 seconds)
  - Ban expiry (24 hours = 86400 seconds)
  - Peer connection timestamps
- Thread-safe with atomic operations
- Useful for functional tests to control time progression without real-time waits

**Example Usage in Tests:**
```bash
# Set mock time to specific timestamp
curl -X POST http://localhost:48443/rpc/setmocktime -d '{"time": 10000}'

# Advance time by 1800 seconds
curl -X POST http://localhost:48443/rpc/setmocktime -d '{"time": 11800}'

# Disable mock time (return to real time)
curl -X POST http://localhost:48443/rpc/setmocktime -d '{"time": 0}'
```

---

### POST /rpc/triggerrotation

Trigger immediate peer rotation check (localnet only, requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Trigger rotation
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/triggerrotation
```

**Request Body:**
- None

**Response (Success):**
```json
{
  "result": "success",
  "message": "Rotation check executed"
}
```

**Response (Forbidden - Wrong Network):**
```json
{
  "error": "triggerrotation only available on localnet"
}
```

**Status Codes:**
- 200 OK - Rotation check executed
- 403 Forbidden - Not running on localnet
- 500 Internal Server Error - Server error

**Notes:**
- **Localnet only** - Returns 403 Forbidden on mainnet/testnet
- Forces immediate execution of `RotateOutboundConnections()` without waiting for the periodic check (normally every 5 seconds)
- Rotation still respects the time interval requirement (1800 seconds since last rotation) and minimum peer count (2 peers)
- Used by functional tests to trigger rotation deterministically after setting mock time
- If rotation conditions are not met (e.g., elapsed time < 1800s or fewer than 2 peers), the method returns without disconnecting any peers

**Example Usage in Tests:**
```bash
# Load credentials
COOKIE=$(cat data/localnet/.cookie)

# Set mock time to trigger rotation interval
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/setmocktime -d '{"time": 11800}'

# Force rotation check immediately (instead of waiting 5 seconds)
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/triggerrotation
```

---

### POST /rpc/disconnectpeer

Disconnect a specific peer connection (localnet only, requires authentication).

**Request:**
```bash
# Load credentials from cookie file
COOKIE=$(cat data/localnet/.cookie)

# Disconnect peer
curl -u "$COOKIE" -X POST http://localhost:48443/rpc/disconnectpeer \
  -H "Content-Type: application/json" \
  -d '{
    "address": "127.0.0.1",
    "port": 48334
  }'
```

**Request Body:**
- `address` (string, required) - Peer IP address
- `port` (integer, required) - Peer P2P port

**Response (Success):**
```json
{
  "result": "success",
  "message": "Peer disconnected"
}
```

**Response (Not Found):**
```json
{
  "error": "Peer not found"
}
```

**Response (Forbidden - Wrong Network):**
```json
{
  "error": "disconnectpeer only available on localnet"
}
```

**Response (Bad Request - Missing Fields):**
```json
{
  "error": "Missing required fields 'address' or 'port'"
}
```

**Response (Bad Request - Invalid Port):**
```json
{
  "error": "Invalid port value"
}
```

**Status Codes:**
- 200 OK - Peer disconnected successfully
- 400 Bad Request - Missing or invalid fields
- 403 Forbidden - Not running on localnet
- 404 Not Found - Peer not found (not connected)
- 500 Internal Server Error - Server error

**Notes:**
- **Localnet only** - Returns 403 Forbidden on mainnet/testnet
- Searches both outbound and inbound peer lists for the specified address:port combination
- Gracefully closes the peer connection and cleans up resources
- Used by functional tests for explicit peer cleanup between test cases
- Returns 404 if the peer is not currently connected

**Example Usage in Tests:**
```bash
# Disconnect peer1
curl -X POST http://localhost:48443/rpc/disconnectpeer \
  -d '{"address": "127.0.0.1", "port": 48334}'

# Disconnect peer2
curl -X POST http://localhost:48443/rpc/disconnectpeer \
  -d '{"address": "127.0.0.1", "port": 48335}'
```

---

## CLI Utilities

### bweave_cli

Process control utility for the blockweave daemon.

**Location:** `build/bweave_cli`

#### Commands

**Start Daemon:**
```bash
./bweave_cli start
./bweave_cli start -c /path/to/config.conf
```

Starts `bweave` in background with daemon mode enabled.

**Output (Success):**
```
[CLI] Found bweave at: /path/to/bweave
[CLI] Starting REST daemon...
[CLI] Waiting for daemon to initialize...
[CLI] Daemon started successfully (PID: 12345)
```

**Output (Already Running):**
```
[CLI] REST daemon is already running (PID: 12345)
```

**Exit Codes:**
- 0 - Success
- 1 - Failed to start or find executable

---

**Stop Daemon:**
```bash
./bweave_cli stop
```

Sends SIGTERM to daemon process for graceful shutdown.

**Output (Success):**
```
[CLI] Stopping REST daemon (PID: 12345)...
[CLI] Daemon stopped successfully
```

**Output (Not Running):**
```
[CLI] Daemon is not running
```

**Exit Codes:**
- 0 - Success
- 1 - Failed to stop or timeout

---

**Check Status:**
```bash
./bweave_cli status
```

Checks if daemon is running via PID file.

**Output (Running):**
```
[CLI] REST daemon is running (PID: 12345)
```

**Output (Not Running):**
```
[CLI] Daemon is not running
```

**Exit Codes:**
- 0 - Daemon is running
- 1 - Daemon is not running

---

**Restart Daemon:**
```bash
./bweave_cli restart
./bweave_cli restart -c /path/to/config.conf
```

Stops daemon (if running) and starts it again.

**Exit Codes:**
- 0 - Success
- 1 - Failed to restart

---

#### Options

- `-c <config_file>` - Specify configuration file path
- Default config: `bweave.conf` in current directory

---

### bweave

Main daemon process (not typically run directly).

**Location:** `build/bweave`

#### Usage

**Foreground Mode (for debugging):**
```bash
./bweave
./bweave -c custom.conf
```

Runs daemon in foreground with output to console.

**Background Mode:**
```bash
./bweave -d
./bweave -d -c custom.conf
```

Runs daemon in background (forks to daemon process).

**Options:**
- `-d, --daemon` - Run in daemon mode (background)
- `-c, --config <file>` - Configuration file path
- `--network <type>` - Network type: mainnet/testnet/localnet (overrides config file)
- `-h, --help` - Show help message

**Network Examples:**
```bash
# Run on mainnet (default)
./bweave

# Run on testnet
./bweave --network testnet

# Run on localnet for development
./bweave --network localnet
```

**Signals:**
- SIGTERM - Graceful shutdown
- SIGINT - Graceful shutdown (Ctrl+C)

---

### wallet

Wallet address generator utility.

**Location:** `build/wallet`

#### Usage

```bash
./wallet
```

**Output:**
```
=== Blockweave Wallet Generator ===
Generated Wallet Address:
ea6dc2ca1bd34a376850629cc74510133b7c2a4c318f7b9e3d1c...
```

**Behavior:**
- Generates a new unique address on each run
- No storage or key management (demo utility)
- Copy address to `bweave.conf` for mining

---

## Configuration File (bweave.conf)

While not an API, the configuration file controls all daemon behavior.

### Format

Key-value pairs, one per line:
```
key=value
```

Comments start with `#`:
```
# This is a comment
miner_address=abc123
```

### Settings

| Setting | Type | Required | Default | Description |
|---------|------|----------|---------|-------------|
| `miner_address` | string | **Yes** | - | Wallet address for mining rewards |
| `network` | string | No | mainnet | Network type: mainnet/testnet/localnet |
| `rest_api_port` | integer | No | *varies* | REST API server port (28443/38443/48443 by network) |
| `p2p_port` | integer | No | *varies* | P2P network listening port (28333/38333/48333 by network) |
| `max_inbound_peers` | integer | No | 120 | Maximum inbound peer connections |
| `max_outbound_peers` | integer | No | 8 | Maximum outbound peer connections |
| `log_dir` | string | No | ./log | Log directory path |
| `log_level` | string | No | INFO | Log level (FATAL/ERROR/WARN/INFO/TRACE) |
| `log_file_size_in_mb` | integer | No | 10 | Log file size limit before rotation |
| `log_file_keep` | integer | No | 5 | Number of rotated log files to keep |
| `data_dir` | string | No | ./data | Blockchain data storage (network subdirs auto-created) |
| `daemon` | boolean | No | false | Daemon mode (set by -d flag) |

**Network-Specific Defaults:**
- Mainnet: ports 28443/28333, 10min blocks, magic `0xBEEFCAFE`
- Testnet: ports 38443/38333, 1min blocks, magic `0xDEADBEEEF`
- Localnet: ports 48443/48333, 1sec blocks, magic `0xCAFEBABE`, fast mining enabled

### Example

```
# === Blockweave Configuration ===

# Miner address (REQUIRED)
miner_address=ea6dc2ca1bd34a376850629cc74510133b7c2a4c318

# Network selection (mainnet/testnet/localnet)
network=mainnet

# REST API Settings (defaults vary by network)
rest_api_port=28443

# P2P Settings (defaults vary by network)
p2p_port=28333
max_inbound_peers=120
max_outbound_peers=8

# Storage (network-specific subdirs auto-created: data/mainnet/, data/testnet/, etc.)
data_dir=./data

# Logging
log_dir=./log
log_level=INFO
log_file_size_in_mb=128
log_file_keep=5

# Daemon mode (managed by -d flag)
daemon=false
```

---

## Error Codes

### HTTP Status Codes

| Code | Meaning | Description |
|------|---------|-------------|
| 200 | OK | Request successful |
| 400 | Bad Request | Invalid or missing parameters |
| 403 | Forbidden | Request forbidden (e.g., localnet-only endpoint on mainnet) |
| 404 | Not Found | Endpoint not found |
| 405 | Method Not Allowed | HTTP method not supported for endpoint |
| 500 | Internal Server Error | Server error during processing |
| 501 | Not Implemented | Endpoint not yet implemented |

### Common Error Responses

**Missing Parameters:**
```json
{
  "error": "Bad Request",
  "message": "Missing required field: address"
}
```

**Invalid Data:**
```json
{
  "error": "Bad Request",
  "message": "Invalid port value (must be 1-65535)"
}
```

**Not Implemented:**
```json
{
  "error": "Not implemented"
}
```

**Internal Error:**
```json
{
  "error": "Internal Server Error",
  "message": "Failed to process request"
}
```

---

## API Examples

### Complete Workflow

**1. Generate Wallet:**
```bash
./wallet
# Copy generated address
```

**2. Configure Node:**
```bash
# Edit bweave.conf
miner_address=<generated_address>
```

**3. Start Daemon:**
```bash
./bweave_cli start
```

**4. Check Status:**
```bash
curl http://localhost:28443/chain
```

**5. Add Peer:**
```bash
curl -X POST http://localhost:28443/rpc/addpeer \
  -H "Content-Type: application/json" \
  -d '{"address": "192.168.1.100", "port": 28333}'
```

**6. Submit Transaction:**
```bash
curl -X POST http://localhost:28443/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "from": "sender_address",
    "to": "recipient_address",
    "data": "Hello Blockweave!"
  }'
```

**7. Start Mining:**
```bash
curl -X POST http://localhost:28443/mine/start
```

**8. Monitor:**
```bash
# Check blockchain state
curl http://localhost:28443/chain

# Check connected peers
curl http://localhost:28443/rpc/getpeer

# View logs
tail -f ./log/bweave.log
```

**9. Stop Daemon:**
```bash
./bweave_cli stop
```

---

## Rate Limiting

**Current Implementation:**
- No rate limiting implemented
- All endpoints accept unlimited requests
- Subject to TCP connection limits only

**Production Considerations:**
- Implement rate limiting per IP
- Add authentication for sensitive endpoints
- Consider API quotas per user

---

## Security Notes

**? Warning: This is a demo/prototype system**

Current security limitations:
- No authentication required
- No TLS/HTTPS support
- No input sanitization
- No request validation
- Open to all network interfaces (INADDR_ANY)
- No DOS protection

**For production use, implement:**
- API authentication (tokens, API keys)
- TLS encryption for all endpoints
- Input validation and sanitization
- Rate limiting and throttling
- Firewall rules and network segmentation
- Request size limits
- Timeout controls

---

## P2P Message Protocol

While not an HTTP API, the P2P protocol is documented here for completeness.

### Message Format

```
[1 byte type_length][N bytes type_string][4 bytes payload_length][M bytes payload]
```

- **type_length**: Length of message type string (1-255)
- **type_string**: Message type in ASCII lowercase (e.g., "ping", "tx_ids")
- **payload_length**: Payload size in bytes (network byte order, big-endian)
- **payload**: Message payload (optional, can be 0 bytes)

### Message Types

| Type | Direction | Payload | Description |
|------|-----------|---------|-------------|
| `ping` | Both | None | Connection keep-alive (sent every 30 seconds) |
| `pong` | Both | None | Response to ping |
| `get_peers` | Request | None | Request peer list |
| `peers` | Response | Peer list | List of known peers |
| `tx_ids` | Broadcast | TX ID list | Transaction ID announcement |
| `get_tx` | Request | TX ID | Request transaction by ID |
| `tx` | Response | TX data | Transaction data |
| `block` | Broadcast | Block data | New block announcement |
| `get_block` | Request | Block hash | Request block by hash |
| `get_chain` | Request | None | Request blockchain state |

### Connection Lifecycle

1. **Connection Established**
   - TCP connection on P2P port (default 28333)
   - TCP keep-alive enabled

2. **Keep-Alive**
   - PING sent every 30 seconds by peer manager
   - PONG expected in response
   - Connection closed after 6 failed keep-alive probes

3. **Message Exchange**
   - Messages sent/received asynchronously
   - Non-blocking sockets
   - Messages newline-terminated for text protocols

4. **Disconnection**
   - Graceful: QUIT message (not implemented)
   - Ungraceful: TCP FIN/RST or timeout
   - Cleanup: Remove from peer list

---

## Monitoring and Debugging

### Log Files

**Location:** Configured via `log_dir` (default: `./log/bweave.log`)

**Format:**
```
[2025-10-28 01:05:20.261 UTC] [INFO ] [bweave:peer_manager] Message here
```

**Fields:**
- Timestamp (UTC)
- Log level (FATAL/ERROR/WARN/INFO/TRACE)
- Process name
- Thread name
- Message

**Log Levels:**
- `FATAL` - Fatal errors, process will exit
- `ERROR` - Errors that don't stop execution
- `WARN` - Warning conditions
- `INFO` - General informational messages (default)
- `TRACE` - Detailed trace information

**Configuration:**
```
log_level=TRACE  # Show all messages
log_level=INFO   # Show INFO and above (default)
log_level=ERROR  # Show only errors and fatal
```

### Health Checks

**Check if daemon is running:**
```bash
./bweave_cli status
```

**Check REST API:**
```bash
curl http://localhost:28443/chain
```

**Check P2P connectivity:**
```bash
curl http://localhost:28443/rpc/getpeer
```

**Check logs:**
```bash
tail -f ./log/bweave.log
grep ERROR ./log/bweave.log
```

### Performance Metrics

Currently no built-in metrics. Monitor via:
- Log analysis
- System tools (top, htop, iostat)
- Network tools (netstat, ss, lsof)

**Future:** Prometheus metrics export endpoint
