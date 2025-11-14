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

#### POST /transaction

Submit a new transaction to the mempool.

**Request:**
```bash
curl -X POST http://localhost:28443/transaction \
  -H "Content-Type: application/json" \
  -d '{
    "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
    "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
    "data": "SGVsbG8gQmxvY2tjaGFpbiBkYXRhIQ==",
    "fee": 0.00012
  }'
```

**Request Body:**
- `from` (string, required) - Sender address
- `to` (string, required) - Recipient address
- `data` (string, required) - Transaction data (treated as plain text, not base64)
- `fee` (number, optional) - Transaction fee, default: 0

**Response (Success):**
```json
{
  "status": "success",
  "transaction_id": "a1b2c3d4e5f6...",
  "from": "bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
  "to": "bc1qw508d6qejxtdg4y5r3zarvaryv98gj9p8t5z6",
  "data_size": 23,
  "fee": 0
}
```

**Response (Error):**
```json
{
  "error": "Bad Request",
  "message": "Missing required field: from, to, data"
}
```

**Status Codes:**
- 200 OK - Transaction accepted
- 400 Bad Request - Missing or invalid fields
- 500 Internal Server Error - Server error

---

#### POST /files

Upload a file as a transaction.

**Request (Multipart Form Data):**
```bash
curl -X POST http://localhost:28443/files \
  -F "file=@/tmp/test_upload.txt"
```

**Request (Raw Binary):**
```bash
curl -X POST http://localhost:28443/files \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@/tmp/test_upload.txt"
```

**Response (Success):**
```json
{
  "status": "success",
  "transaction_id": "a1b2c3d4e5f6...",
  "uuid": "550e8400-e29b-41d4-a716-446655440000",
  "original_filename": "test_upload.txt",
  "saved_path": "./data/550e8400-e29b-41d4-a716-446655440000",
  "size": 1024,
  "message": "File uploaded and saved to disk"
}
```

**Response (Error):**
```json
{
  "error": "Bad Request",
  "message": "Empty file data"
}
```

**Behavior:**
- File is saved to `data_dir` with UUID filename
- Transaction created with file data
- Owner: miner address (from config)
- Target: "file_storage"
- Fee: 0

**Status Codes:**
- 200 OK - File uploaded successfully
- 400 Bad Request - Empty file or invalid format
- 500 Internal Server Error - Failed to save file

---

---

## RPC API

RPC endpoints for peer management (also HTTP POST requests).

### POST /rpc/addpeer

Add an outbound peer connection.

**Request:**
```bash
curl -X POST http://localhost:28443/rpc/addpeer \
  -H "Content-Type: application/json" \
  -d '{
    "address": "127.0.0.1",
    "port": 28334
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

Get list of all connected peers.

**Request:**
```bash
curl http://localhost:28443/rpc/getpeer
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
- 500 Internal Server Error - Server error

---

### POST /rpc/minestart

Start continuous mining (localnet only).

**Request:**
```bash
curl -X POST http://localhost:48443/rpc/minestart
```

**Response (Success):**
```json
{
  "status": "success",
  "mining_enabled": true,
  "message": "Mining started"
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
- 200 OK - Mining started
- 403 Forbidden - Not running on localnet
- 500 Internal Server Error - Server error

**Notes:**
- **Localnet only** - Returns 403 Forbidden on mainnet/testnet
- Enables automatic continuous mining
- Mining continues until explicitly stopped with /rpc/minestop

---

### POST /rpc/minestop

Stop continuous mining (localnet only).

**Request:**
```bash
curl -X POST http://localhost:48443/rpc/minestop
```

**Response (Success):**
```json
{
  "status": "success",
  "mining_enabled": false,
  "message": "Mining stopped"
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
- 200 OK - Mining stopped
- 403 Forbidden - Not running on localnet
- 500 Internal Server Error - Server error

**Notes:**
- **Localnet only** - Returns 403 Forbidden on mainnet/testnet
- Stops automatic mining
- Does not affect /rpc/minetrigger (which works independently)

---

### POST /rpc/minetrigger

Mine one block immediately (localnet only).

**Request:**
```bash
curl -X POST http://localhost:48443/rpc/minetrigger
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
