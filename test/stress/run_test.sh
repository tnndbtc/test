#!/bin/bash
#
# Stress Test Runner for Blockweave
#
# Executes stress tests and aggregates results.
#
# Usage:
#   ./run_test.sh --test <test_name> --output <dir>
#   ./run_test.sh --test all --output test_results/
#
# Examples:
#   ./run_test.sh --test transaction --output test_results/
#   ./run_test.sh --test all --output test_results/
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
OUTPUT_DIR="test_results"
TEST_NAME=""

# Test definitions
declare -A TESTS
TESTS["transaction"]="test_stress_transaction.py"
TESTS["transaction_propagation"]="test_stress_transaction_propagation.py"
TESTS["block_propagation"]="test_stress_block_propagation.py"
TESTS["orphan_blocks"]="test_stress_orphan_blocks.py"
TESTS["rpc_endpoints"]="test_stress_rpc_endpoints.py"
TESTS["malicious_nodes"]="test_stress_malicious_nodes.py"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --test)
            TEST_NAME="$2"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 --test <test_name> --output <dir>"
            echo ""
            echo "Options:"
            echo "  --test <name>         Test to run or 'all' for all tests"
            echo "  --output <dir>        Output directory for results (default: test_results/)"
            echo "  --help                Show this help message"
            echo ""
            echo "Available tests:"
            for test in "${!TESTS[@]}"; do
                echo "  - $test"
            done
            exit 0
            ;;
        *)
            echo -e "${RED}Error: Unknown option $1${NC}"
            exit 1
            ;;
    esac
done

# Validate arguments
if [ -z "$TEST_NAME" ]; then
    echo -e "${RED}Error: Must specify --test <name> or --test all${NC}"
    echo "Run with --help for usage information"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Export PYTHONPATH for test framework
export PYTHONPATH=../functional

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Blockweave Stress Test Runner${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "Output directory: ${GREEN}$OUTPUT_DIR${NC}"
echo -e "PYTHONPATH: ${GREEN}$PYTHONPATH${NC}"
echo ""

# Copy test_runner.py to current folder
echo -e "Copying test_runner.py from ../functional..."
cp ../functional/test_runner.py ./test_runner.py
chmod +x ./test_runner.py

# Function to run a single test
run_test() {
    local test_name=$1
    local test_file=$2

    echo -e "${YELLOW}[Running]${NC} $test_name"
    echo -e "  Test file: $test_file"

    local start_time=$(date +%s)

    # Run the test
    if ./test_runner.py "$test_file" --tmpdir /tmp --nocleanup --output "$OUTPUT_DIR"; then
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        echo -e "${GREEN}[PASSED]${NC} $test_name (${duration}s)"
        return 0
    else
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        echo -e "${RED}[FAILED]${NC} $test_name (${duration}s)"
        return 1
    fi
}

# Track test results
declare -a PASSED_TESTS
declare -a FAILED_TESTS
TOTAL_START_TIME=$(date +%s)

# Run tests
if [ "$TEST_NAME" = "all" ]; then
    echo -e "${BLUE}Running all stress tests...${NC}"
    echo ""

    # Run each test sequentially
    for test_name in "${!TESTS[@]}"; do
        test_file="${TESTS[$test_name]}"

        if run_test "$test_name" "$test_file"; then
            PASSED_TESTS+=("$test_name")
        else
            FAILED_TESTS+=("$test_name")
        fi

        echo ""
    done
else
    # Run specific test
    if [ -z "${TESTS[$TEST_NAME]}" ]; then
        echo -e "${RED}Error: Unknown test '$TEST_NAME'${NC}"
        echo "Available tests:"
        for test in "${!TESTS[@]}"; do
            echo "  - $test"
        done
        exit 1
    fi

    test_file="${TESTS[$TEST_NAME]}"

    if run_test "$TEST_NAME" "$test_file"; then
        PASSED_TESTS+=("$TEST_NAME")
    else
        FAILED_TESTS+=("$TEST_NAME")
    fi
fi

TOTAL_END_TIME=$(date +%s)
TOTAL_DURATION=$((TOTAL_END_TIME - TOTAL_START_TIME))

# Print summary
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "Total duration: ${TOTAL_DURATION}s"
echo -e "Output directory: ${GREEN}$OUTPUT_DIR${NC}"
echo ""

if [ ${#PASSED_TESTS[@]} -gt 0 ]; then
    echo -e "${GREEN}Passed tests (${#PASSED_TESTS[@]}):${NC}"
    for test in "${PASSED_TESTS[@]}"; do
        echo -e "  ${GREEN}✓${NC} $test"
    done
    echo ""
fi

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo -e "${RED}Failed tests (${#FAILED_TESTS[@]}):${NC}"
    for test in "${FAILED_TESTS[@]}"; do
        echo -e "  ${RED}✗${NC} $test"
    done
    echo ""
fi

# Aggregate results
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Aggregating Results${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

if [ -f aggregate_results.py ]; then
    echo -e "Running aggregate_results.py..."
    # Pass test name filter to aggregate_results.py
    if [ "$TEST_NAME" = "all" ]; then
        python3 aggregate_results.py "$OUTPUT_DIR" all
    else
        python3 aggregate_results.py "$OUTPUT_DIR" "$TEST_NAME"
    fi
else
    echo -e "${YELLOW}Warning: aggregate_results.py not found${NC}"
fi

echo ""

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi

echo -e "${GREEN}All tests passed!${NC}"
exit 0
