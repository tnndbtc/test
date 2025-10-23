# Unit Tests

This directory contains unit tests for the blockweave project.

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
4. **PeerManager_Constructor** - Tests manager initialization
5. **PeerManager_GetConnectedPeers_Empty** - Tests empty peer list handling
6. **PeerManager_ThreadSafe_GetOutboundPeerCount** - Tests thread-safe peer count access
7. **PeerManager_ThreadSafe_GetConnectedPeers** - Tests thread-safe peer retrieval
8. **PeerManager_BroadcastTransactionIds_Empty** - Tests broadcast with empty transaction list
9. **PeerManager_BroadcastTransactionIds_NoPeers** - Tests broadcast with no connected peers
10. **PeerConnection_AtomicFlag** - Tests atomic flag operations
11. **PeerManager_StartStop** - Tests lifecycle management
12. **PeerManager_MultipleStartStop** - Tests multiple start/stop cycles

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
make test_peer
```

The test executable will be created at `build/test_peer`.

### Option 3: Build Tests Standalone Manually

**Prerequisites**: Main project libraries must be built first.

Step 1: Build the main project libraries:
```bash
cd build
cmake ..
make bfthreadname bflogger bfpeer
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
./test_peer
```

## Running Tests

After building, run the test executable:

```bash
# From build directory
./test_peer

# Or with full path
./build/test_peer
```

### Expected Output

```
======================================================================
Running Unit Tests
======================================================================

Running: PeerConnection_DefaultConstructor ... ✓ PASSED
Running: PeerConnection_ParameterizedConstructor ... ✓ PASSED
Running: PeerConnection_MoveConstructor ... ✓ PASSED
...

======================================================================
TEST SUMMARY
======================================================================
Total:  13
Passed: 13
Failed: 0
======================================================================

✓ ALL TESTS PASSED
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
   make test_peer
   ```

## Test Organization

- `unit_test.h` - Test framework header (include this in all test files)
- `test_peer.cpp` - Peer module tests
- Future test files can be added here following the same pattern

## CMake Configuration

The `CMakeLists.txt` in this directory supports both:
- **Standalone builds** - Independent compilation from src/test directory
- **Integrated builds** - Part of the main project build system

The CMake script automatically detects the build mode and configures paths accordingly.

## Dependencies

Test dependencies:
- **libbfpeer** - Peer networking library (being tested)
- **libbflogger** - Logging system
- **libbfthreadname** - Thread naming utilities
- **Threads** - POSIX threads (pthread)
- **C++17 compiler** - GCC, Clang, or MSVC

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
./build/test_peer

# Or from src/test standalone build
./src/test/build/test_peer
```

### Port Already in Use

Some tests create `CPeerManager` instances on specific ports (8333-8338, 18333-18334). If tests fail with "Address already in use", ensure no other instances are running.

## Thread Safety Testing

Several tests verify thread-safe operations:
- Tests spawn 10 concurrent threads
- Each thread performs 100+ operations
- Validates no race conditions or deadlocks occur

These tests use atomic counters and sleep intervals to simulate real concurrent access patterns.
