# Unit Tests

This directory contains unit tests for the blockweave project. All tests are combined into a single executable (`test_all`) that runs 118 tests across multiple modules.

## Test Framework

The tests use a custom lightweight C++ unit test framework defined in `unit_test.h`. This framework provides:

- **Test registration**: `TEST(test_name)` macro for easy test definition
- **Assertions**: `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_EQUAL`, `ASSERT_NOT_EQUAL`, `ASSERT_NULL`, `ASSERT_NOT_NULL`
- **Test runner**: `RunAllTests()` function with color-coded output
- **Zero dependencies**: No external testing libraries required

## Current Tests

### test_peer.cpp

Unit tests for the peer module (`CPeerConnection` and `CPeerManager`):

1. **PeerConnection_DefaultConstructor** - Tests default constructor initialization
2. **PeerConnection_ParameterizedConstructor** - Tests parameterized constructor
3. **PeerConnection_MoveConstructor** - Tests move semantics
4. **PeerManager_Constructor** - Tests manager initialization (both inbound and outbound peer counts)
5. **PeerManager_GetConnectedPeers_Empty** - Tests empty peer list handling
6. **PeerManager_ThreadSafe_GetOutboundPeerCount** - Tests thread-safe outbound peer count access
7. **PeerManager_ThreadSafe_GetConnectedPeers** - Tests thread-safe peer retrieval
8. **PeerManager_BroadcastTransactionIds_Empty** - Tests broadcast with empty transaction list
9. **PeerManager_BroadcastTransactionIds_NoPeers** - Tests broadcast with no connected peers
10. **PeerConnection_AtomicFlag** - Tests atomic flag operations
11. **PeerManager_GetInboundPeerCount** - Tests inbound peer count tracking
12. **PeerManager_ThreadSafe_GetInboundPeerCount** - Tests thread-safe inbound peer count access
13. **PeerManager_ConstructorWithLimits** - Tests constructor with custom inbound/outbound limits
14. **PeerManager_InboundOutboundIndependent** - Tests that inbound and outbound tracking is independent
15. **PeerManager_ThreadSafe_BothPeerCounts** - Tests concurrent access to both peer count methods
16. **PeerManager_SendMessageToPeer_NoPeers** - Tests SendMessageToPeer with no connected peers
17. **PeerManager_BroadcastMessage_NoPeers** - Tests BroadcastMessage with no connected peers
18. **PeerManager_BroadcastMessage_DifferentTypes** - Tests BroadcastMessage with different message types

### test_peer_message.cpp

Unit tests for the P2P message protocol (`CPeerMessage`):

1. **PeerMessage_DefaultConstructor** - Tests default constructor creates UNKNOWN message
2. **PeerMessage_ConstructorWithType** - Tests constructor with message type
3. **PeerMessage_ConstructorWithStringPayload** - Tests constructor with type and string payload
4. **PeerMessage_ConstructorWithBinaryPayload** - Tests constructor with type and binary payload
5. **PeerMessage_SerializeEmpty** - Tests serialization with empty payload
6. **PeerMessage_SerializeSmallPayload** - Tests serialization with small payload
7. **PeerMessage_SerializeLargePayload** - Tests serialization with large payload (1000 bytes)
8. **PeerMessage_DeserializeValid** - Tests deserialization with valid data
9. **PeerMessage_DeserializeWithPayload** - Tests deserialization with payload data
10. **PeerMessage_DeserializeTooShort** - Tests deserialization with insufficient header data
11. **PeerMessage_DeserializeInsufficientPayload** - Tests deserialization with insufficient payload
12. **PeerMessage_RoundTripEmpty** - Tests serialize then deserialize with empty payload
13. **PeerMessage_RoundTripWithPayload** - Tests serialize then deserialize with string payload
14. **PeerMessage_RoundTripBinaryWithNulls** - Tests binary payload with null bytes
15. **PeerMessage_TypeToString** - Tests type-to-string conversion for all message types
16. **PeerMessage_SettersAndGetters** - Tests SetType and SetPayload methods
17. **PeerMessage_GetHeaderSize** - Tests header size constant (5 bytes)
18. **PeerMessage_MessageTypeValidity** - Tests that all types except UNKNOWN are valid
19. **PeerMessage_SerializationFormat** - Tests message format compliance
20. **PeerMessage_EmptyStringPayload** - Tests empty string payload handling
21. **PeerMessage_NetworkByteOrder** - Tests network byte order conversion

### test_request_queue.cpp

Unit tests for the request queue (`CRequestQueue` and `CHttpRequest`):

1. **HttpRequest_Initialization** - Tests HTTP request structure initialization
2. **RequestQueue_Constructor** - Tests request queue constructor
3. **RequestQueue_Enqueue** - Tests enqueue operation
4. **RequestQueue_EnqueueDequeue** - Tests enqueue and dequeue together
5. **RequestQueue_DequeueTimeout** - Tests dequeue timeout on empty queue
6. **RequestQueue_FIFOOrdering** - Tests FIFO ordering of requests
7. **RequestQueue_Shutdown** - Tests shutdown mechanism
8. **RequestQueue_ThreadSafeEnqueue** - Tests concurrent enqueue from multiple threads
9. **RequestQueue_ThreadSafeConcurrent** - Tests concurrent enqueue and dequeue
10. **RequestQueue_MultipleShutdown** - Tests multiple shutdown calls

### test_api_server.cpp

Unit tests for the REST API server (`CRestApiServer`):

1. **RestApiServer_Constructor** - Tests server constructor
2. **RestApiServer_StartStop** - Tests server start and stop
3. **RestApiServer_MultipleStartStop** - Tests multiple start/stop cycles
4. **RestApiServer_StopWithoutStart** - Tests stop without start
5. **RestApiServer_DestructorStops** - Tests destructor stops running server

## Building Tests

### Option 1: Using the Build Script (Recommended)

The easiest way to build the tests is to use the provided build script:

```bash
cd src/test
chmod +x build.sh
./build.sh
```

The script will:
- Check if the required libraries exist
- Build the main project libraries if needed
- Configure and build the unit tests
- Display the path to the test executable

### Option 2: Build as Part of Main Project

Add the test target back to the main `CMakeLists.txt`, then from the project root:

```bash
cd build
cmake ..
make test_all
```

The test executable will be created at `build/test_all`.

### Option 3: Build Tests Standalone Manually

**Prerequisites**: Main project libraries must be built first.

Step 1: Build the main project libraries:
```bash
cd build
cmake ..
make bwthreadname bwlogger bwpeer bwutils bwblockcore bwrest
```

Step 2: Build the tests:
```bash
cd ../src/test
mkdir -p build
cd build
cmake ..
make
```

Step 3: Run tests:
```bash
./test_all
```

## Running Tests

After building, run the test executable:

```bash
# From build directory
./test_all                         # Run all tests
./test_all --run_test=Peer         # Run peer tests (connection & manager)
./test_all --run_test=PeerMessage  # Run peer message protocol tests
./test_all --run_test=Queue        # Run RequestQueue tests
./test_all --run_test=RestApiServer # Run API server tests
./test_all --list                  # List all available tests
./test_all --help                  # Show usage information

# Or with full path
./build/test_all
```

### Command-Line Options

| Option | Description |
|--------|-------------|
| `--run_test=<filter>` | Run only tests matching filter (substring match) |
| `--list` | List all available tests without running them |
| `--help` | Show usage information and examples |

### Filter Examples

The `--run_test` option uses substring matching on test names:

| Filter | Tests Matched | Count |
|--------|---------------|-------|
| `Peer` | All peer tests (PeerConnection_*, PeerManager_*, PeerMessage_*) | 39 |
| `PeerConnection` | Only PeerConnection tests | 3 |
| `PeerManager` | Only PeerManager tests | 15 |
| `PeerMessage` | Only PeerMessage protocol tests | 21 |
| `Request` | Request queue tests (HttpRequest_*, RequestQueue_*) | 10 |
| `RestApiServer` | Only REST API server tests | 5 |
| `Block` | Block and blockweave tests | 11 |
| `Transaction` | Transaction tests | 5 |
| `Wallet` | Wallet tests | 3 |
| `Logger` | Logger tests | 26 |
| (no filter) | All tests | 118 |

### Expected Output

**Running all tests:**
```
======================================================================
Blockweave Unit Test Suite
======================================================================
Test modules:
  - test_peer.cpp (Peer networking - 12 tests)
  - test_rest.cpp (REST API - 15 tests)
======================================================================

======================================================================
Running Unit Tests
======================================================================

Running: PeerConnection_DefaultConstructor ... ✓ PASSED
Running: PeerConnection_ParameterizedConstructor ... ✓ PASSED
Running: PeerConnection_MoveConstructor ... ✓ PASSED
Running: HttpRequest_Initialization ... ✓ PASSED
Running: RequestQueue_Constructor ... ✓ PASSED
...

======================================================================
TEST SUMMARY
======================================================================
Total:  27
Passed: 27
Failed: 0
======================================================================

✓ ALL TESTS PASSED
```

**Running with filter (`./test_all --run_test=Peer`):**
```
======================================================================
Blockweave Unit Test Suite
======================================================================
Test modules:
  - test_peer.cpp (Peer networking - 12 tests)
  - test_rest.cpp (REST API - 15 tests)
======================================================================

======================================================================
Running Unit Tests (filter: "Peer")
======================================================================

Running: PeerConnection_DefaultConstructor ... ✓ PASSED
Running: PeerConnection_ParameterizedConstructor ... ✓ PASSED
Running: PeerConnection_MoveConstructor ... ✓ PASSED
Running: PeerManager_Constructor ... ✓ PASSED
...

======================================================================
TEST SUMMARY
======================================================================
Total:  12
Passed: 12
Failed: 0
Skipped: 15
======================================================================

✓ ALL TESTS PASSED
```

**Listing tests (`./test_all --list`):**
```
======================================================================
Blockweave Unit Test Suite
======================================================================
Test modules:
  - test_peer.cpp (Peer networking - 12 tests)
  - test_rest.cpp (REST API - 15 tests)
======================================================================

======================================================================
Available Tests (27 total)
======================================================================
  PeerConnection_DefaultConstructor
  PeerConnection_ParameterizedConstructor
  PeerConnection_MoveConstructor
  ...
======================================================================
```

## Adding New Tests

1. **Include the framework**:
   ```cpp
   #include "unit_test.h"
   ```

2. **Define a test**:
   ```cpp
   TEST(MyTestName) {
       // Test code here
       ASSERT_TRUE(condition, "Description of what should be true");
       ASSERT_EQUAL(actual, expected, "Description");
   }
   ```

3. **Add to main()**:
   Tests are automatically registered via the `TEST()` macro. Just call:
   ```cpp
   int main() {
       return UnitTest::RunAllTests();
   }
   ```

4. **Rebuild**:
   ```bash
   make test_all   # Build the unified test executable
   ```

## Test Organization

- `unit_test.h` - Test framework header with filtering support
- `test_all.cpp` - Main entry point with command-line parsing
- `test_peer.cpp` - Peer connection and manager tests (18 tests)
- `test_peer_message.cpp` - P2P message protocol tests (21 tests)
- `test_request_queue.cpp` - Request queue tests (10 tests)
- `test_api_server.cpp` - REST API server tests (5 tests)
- `test_block.cpp` - Block tests (4 tests)
- `test_transaction.cpp` - Transaction tests (5 tests)
- `test_blockweave.cpp` - Blockweave tests (7 tests)
- `test_wallet.cpp` - Wallet tests (3 tests)
- `test_hash.cpp` - Hash tests (2 tests)
- `test_blockfile.cpp` - Block file persistence tests (17 tests)
- `test_logger.cpp` - Logger tests (26 tests)
- `build.sh` - Automated build script
- `CMakeLists.txt` - Build configuration
- Future test modules can be added by creating new .cpp files and adding them to CMakeLists.txt

### Test Naming Conventions

Tests follow a naming pattern that allows filtering by module:

| Module | Naming Pattern | Example |
|--------|----------------|---------|
| Peer Connections | `PeerConnection_*` | PeerConnection_DefaultConstructor |
| Peer Manager | `PeerManager_*` | PeerManager_ThreadSafe_GetOutboundPeerCount |
| Peer Messages | `PeerMessage_*` | PeerMessage_RoundTripWithPayload |
| HTTP Request | `HttpRequest_*` | HttpRequest_Initialization |
| Request Queue | `RequestQueue_*` | RequestQueue_EnqueueDequeue |
| REST API Server | `RestApiServer_*` | RestApiServer_StartStop |
| Block | `Block_*` | Block_HashComputation |
| Transaction | `Transaction_*` | Transaction_IDGeneration |
| Blockweave | `Blockweave_*` | Blockweave_ThreadSafeAddTransaction |
| Wallet | `Wallet_*` | Wallet_CreateTransaction |
| Hash | `Hash_*` | Hash_EmptyInput |
| Block File | `BlockFile*` | BlockFileSaveAndLoadEmptyBlock |
| Logger | `Logger*` | LoggerThreadSafeConcurrentWrites |

This naming scheme enables filtering tests by feature area using `--run_test=<pattern>`.

## CMake Configuration

The `CMakeLists.txt` in this directory supports both:
- **Standalone builds** - Independent compilation from src/test directory
- **Integrated builds** - Part of the main project build system

The CMake script automatically detects the build mode and configures paths accordingly.

## Dependencies

Test dependencies:

**All test modules require:**
- **libbwpeer** - Peer networking library
- **libbwrest** - REST API library
- **libbwblockcore** - Core blockchain implementation
- **libbwutils** - Hash and configuration utilities
- **libbwlogger** - Logging system
- **libbwthreadname** - Thread naming utilities
- **Threads** - POSIX threads (pthread)
- **C++17 compiler** - GCC, Clang, or MSVC

**test_peer_message specifically tests:**
- Message serialization/deserialization
- Network byte order conversion (big-endian)
- Binary payload handling with null bytes
- All 11 message types defined in the P2P protocol

## Troubleshooting

### Libraries Not Found (Standalone Build)

If you get library linking errors:
```
cd ../../build
make  # Build all libraries first
cd ../src/test/build
cmake ..
make
```

### Test Executable Not Found

Make sure you're running from the correct directory:
```bash
# From project root
./build/test_all

# Or from src/test standalone build
./src/test/build/test_all
```

### Port Already in Use

Some tests create `CPeerManager` instances on specific ports (8333-8338, 18333-18334). If tests fail with "Address already in use", ensure no other instances are running.

## Thread Safety Testing

Several tests verify thread-safe operations:
- Tests spawn 10 concurrent threads
- Each thread performs 100+ operations
- Validates no race conditions or deadlocks occur

These tests use atomic counters and sleep intervals to simulate real concurrent access patterns.
