# Functional Tests

This directory contains functional tests for the Blockweave REST daemon. These tests start a local node and verify the REST API endpoints work correctly.

## Prerequisites

1. **Build the project:**
   ```bash
   cd /path/to/project
   mkdir -p build && cd build
   cmake ..
   make
   ```

2. **Python 3 with requests library:**
   ```bash
   pip3 install requests
   ```

3. **Valid blockweave.conf:**
   Make sure `blockweave.conf` exists in the project root with a valid `miner_address`.

## Running Tests

### Using the Test Runner (Recommended)

The `test_runner.py` script provides a convenient way to run tests.

**Run all tests (from project root):**
```bash
python3 test/functional/test_runner.py
```

**Run all tests (from build directory):**
```bash
cd build/test/functional && python3 test_runner.py
```

**Run a specific test (by filename):**
```bash
python3 test/functional/test_runner.py test_chain.py
```

**Run a test by relative path:**
```bash
python3 test/functional/test_runner.py test/functional/test_chain.py
```

**Run a test by absolute path:**
```bash
python3 test/functional/test_runner.py /full/path/to/test_chain.py
```

**Use custom tmpdir:**
```bash
python3 test/functional/test_runner.py --tmpdir=/tmp/my_tests
```

**Preserve test data after run:**
```bash
python3 test/functional/test_runner.py --nocleanup
```

**Combine options:**
```bash
python3 test/functional/test_runner.py test_chain.py --tmpdir=/tmp/debug --nocleanup
```

The test runner provides:
- Aggregated test results and summary
- Exit code 0 if all tests pass, non-zero if any fail
- Clear formatting and progress reporting
- Automatic test discovery (finds all `test_*.py` files)
- Temporary directory management with optional cleanup
- Logging to `test_framework.log` in test tmpdir
- Per-node logging in `node0/`, `node1/`, etc. subdirectories

### Run Individual Test Directly

From the project root directory:

```bash
cd test/functional
python3 test_chain.py
```

Or make the test executable and run directly:

```bash
chmod +x test/functional/test_chain.py
./test/functional/test_chain.py
```

## Available Tests

### test_chain.py

Tests the `GET /chain` endpoint:
- Starts a local blockweave node
- Queries the `/chain` endpoint
- Verifies response status, content type, and JSON structure
- Checks that required fields (`mempool_size`, `mining_enabled`) are present
- Validates field types and values
- Makes multiple requests to verify stability

**Expected Output:**
```
======================================================================
Running: ChainTest
======================================================================

ℹ INFO: Setting up test environment...
ℹ INFO: Starting local blockweave node...
Starting blockweave node on port 28443...
Node started successfully (PID from daemon)
ℹ INFO: Node started successfully
ℹ INFO: Running test...
ℹ INFO: Testing GET /chain endpoint...
✓ PASS: GET /chain returns 200 OK
✓ PASS: Response Content-Type is application/json
ℹ INFO: Response data: {
  "mempool_size": 0,
  "mining_enabled": true
}
✓ PASS: Response is valid JSON
✓ PASS: Response contains 'mempool_size' field
✓ PASS: Response contains 'mining_enabled' field
✓ PASS: mempool_size is an integer (got: int)
✓ PASS: mining_enabled is a boolean (got: bool)
✓ PASS: mempool_size is non-negative (got: 0)
ℹ INFO: Mempool size: 0
ℹ INFO: Mining enabled: true
ℹ INFO: Making second request to verify endpoint stability...
✓ PASS: Second GET /chain also returns 200 OK
✓ PASS: Second response also contains required fields
ℹ INFO: Cleaning up...
ℹ INFO: Stopping blockweave node...
Stopping blockweave node...
Node stopped successfully
ℹ INFO: Node stopped

======================================================================
TEST SUMMARY
======================================================================
Passed: 11
Failed: 0
Total:  11

✓ ALL TESTS PASSED
```

## Test Data and Logging

### Temporary Directories

Each test run creates a temporary directory for test data:

- **Default behavior**: Auto-generated temporary directory (e.g., `/tmp/blockweave_test_0_xyz/`)
- **Custom tmpdir**: Use `--tmpdir` to specify a custom directory
- **Cleanup**: By default, test data is cleaned up after tests complete
- **Preservation**: Use `--nocleanup` to keep test data for debugging

### Directory Structure

```
<tmpdir>/
└── test_run_20251015_143052_123/    # Test run with timestamp
    ├── test_framework.log           # Python test framework logs
    └── node0/                       # Node 0 directory
        ├── node0.conf               # Custom config for this node
        ├── logs/                    # Node logs directory
        │   └── rest_daemon_*.log    # REST daemon logs
        └── data/                    # Node blockchain data
```

Each test run creates a directory named `test_run_<timestamp>` where the timestamp format is `YYYYMMDD_HHMMSS_mmm` (date, time, and milliseconds).

**Node Directory Organization:**
- Each node gets its own directory: `node0/`, `node1/`, etc.
- Node-specific configuration file is auto-generated with correct paths
- REST daemon logs are stored in `node<N>/logs/`
- Blockchain data is stored in `node<N>/data/`

### Logging

Test activity is logged to multiple locations:

**Python test framework logs** (`test_framework.log`):
- Test framework initialization and cleanup
- Node start/stop operations
- HTTP requests and responses (at DEBUG level)
- Test assertions and results
- Exception tracebacks

**REST daemon logs** (`node<N>/logs/rest_daemon_*.log`):
- Daemon startup and configuration
- REST API request/response handling
- Mining operations
- Block creation and validation
- Internal daemon operations

**View logs after a test run:**
```bash
# Run test with preserved data
python3 test/functional/test_runner.py test_chain.py --tmpdir=/tmp/mytest --nocleanup

# View Python test framework logs
cat /tmp/mytest/test_run_*/test_framework.log

# View REST daemon logs
cat /tmp/mytest/test_run_*/node0/logs/rest_daemon_*.log
```

## Test Framework

The `test_framework.py` module provides:

### BlockweaveNode Class

Manages a local blockweave node for testing:

- `start(timeout=10)` - Start the node
- `stop(timeout=10)` - Stop the node
- `is_ready()` - Check if node is ready
- `get(endpoint, timeout=5)` - Make GET request
- `post(endpoint, data=None, json_data=None, timeout=5)` - Make POST request

Can be used as a context manager:

```python
with BlockweaveNode() as node:
    response = node.get("/chain")
    # Node automatically stops when exiting context
```

### TestFramework Class

Base class for functional tests:

- `setup()` - Override for test setup
- `run_test()` - Override with test logic
- `cleanup()` - Override for test cleanup
- `add_node(port=28443, **kwargs)` - Create and add a new node for testing (automatically manages node directories)
- `assert_equal(actual, expected, message)` - Assert equality
- `assert_true(condition, message)` - Assert condition is true
- `assert_in(item, container, message)` - Assert item in container
- `log_info(message)` - Log informational message

**Automatic Features:**
- Temporary directory creation and management
- Logging to `test_framework.log`
- Node directory creation (`node0/`, `node1/`, etc.)
- Automatic cleanup unless `--nocleanup` is specified
- Environment variable support (`TEST_TMPDIR`, `TEST_NOCLEANUP`)

## Writing New Tests

To create a new functional test:

1. Create a new file `test_<feature>.py` in `test/functional/`

2. Import the test framework:
   ```python
   #!/usr/bin/env python3
   from test_framework import TestFramework, BlockweaveNode
   import sys
   ```

3. Create a test class inheriting from `TestFramework`:
   ```python
   class MyTest(TestFramework):
       def setup(self):
           self.node = BlockweaveNode()
           if not self.node.start():
               raise RuntimeError("Failed to start node")

       def run_test(self):
           # Your test logic here
           response = self.node.get("/endpoint")
           self.assert_equal(response.status_code, 200, "Test passes")

       def cleanup(self):
           if self.node:
               self.node.stop()
   ```

4. Add main entry point:
   ```python
   if __name__ == "__main__":
       test = MyTest()
       sys.exit(test.main())
   ```

5. Make the test executable:
   ```bash
   chmod +x test/functional/test_<feature>.py
   ```

## Troubleshooting

**Test fails to start node:**
- Ensure the project is built (`cd build && make`)
- Check that `blockweave.conf` exists with valid `miner_address`
- Verify no other instance is running on port 28443

**Port already in use:**
- Stop any running instances: `build/daemon_cli stop`
- Or specify a different port in your test:
  ```python
  self.node = BlockweaveNode(port=28444)
  ```

**Import errors:**
- Run tests from the `test/functional` directory
- Or add the directory to PYTHONPATH:
  ```bash
  export PYTHONPATH=/path/to/project/test/functional:$PYTHONPATH
  ```

## CI/CD Integration

To run tests in a CI/CD pipeline:

```bash
#!/bin/bash
set -e

# Build project
mkdir -p build && cd build
cmake ..
make
cd ..

# Run functional tests using test runner
python3 test/functional/test_runner.py

echo "All tests passed!"
```
