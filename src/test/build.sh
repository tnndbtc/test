#!/bin/bash
# ============= build.sh =============
# Build script for unit tests
#
# This script builds the unit tests in standalone mode.
# It ensures the main project libraries are built first.

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/../.."
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"

echo "========================================"
echo "Building Blockweave Unit Tests"
echo "========================================"
echo ""

# Check if main project build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Main project build directory not found: $BUILD_DIR"
    echo "Please create it first:"
    echo "  cd $PROJECT_ROOT"
    echo "  mkdir -p build"
    echo "  cd build"
    exit 1
fi

# Determine library extension based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    LIB_EXT="dylib"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    LIB_EXT="dll"
else
    LIB_EXT="so"
fi

# Check if required libraries exist
LIBS_EXIST=true
REQUIRED_LIBS=(
    "libbfpeer.${LIB_EXT}"
    "libbflogger.${LIB_EXT}"
    "libbfthreadname.${LIB_EXT}"
    "libbfblockcore.${LIB_EXT}"
    "libbfrest.${LIB_EXT}"
    "libbfutils.${LIB_EXT}"
)

echo "Checking for required libraries in: $BUILD_DIR"
for lib in "${REQUIRED_LIBS[@]}"; do
    if [ ! -f "$BUILD_DIR/$lib" ]; then
        echo "  ✗ Missing: $lib"
        LIBS_EXIST=false
    else
        echo "  ✓ Found: $lib"
    fi
done
echo ""

# Build main project libraries if they don't exist
if [ "$LIBS_EXIST" = false ]; then
    echo "Required libraries not found. Building main project..."
    echo ""

    cd "$BUILD_DIR"

    # Run cmake if Makefile doesn't exist
    if [ ! -f "Makefile" ]; then
        echo "Running cmake..."
        cmake ..
        echo ""
    fi

    # Build the libraries
    echo "Building libraries..."
    make bfthreadname bflogger bfpeer bfutils bfblockcore bfrest
    echo ""

    # Verify libraries were built
    LIBS_EXIST=true
    for lib in "${REQUIRED_LIBS[@]}"; do
        if [ ! -f "$BUILD_DIR/$lib" ]; then
            echo "Error: Failed to build $lib"
            LIBS_EXIST=false
        fi
    done

    if [ "$LIBS_EXIST" = false ]; then
        echo ""
        echo "Error: Failed to build required libraries"
        exit 1
    fi

    echo "✓ All libraries built successfully"
    echo ""
fi

# Build the unit tests
echo "Building unit tests..."
echo ""

# Create test build directory if it doesn't exist
if [ ! -d "$TEST_BUILD_DIR" ]; then
    echo "Creating build directory: $TEST_BUILD_DIR"
    mkdir -p "$TEST_BUILD_DIR"
else
    echo "Using existing build directory: $TEST_BUILD_DIR"
fi

cd "$TEST_BUILD_DIR"

# Run cmake
echo "Running cmake..."
cmake ..
echo ""

# Build test executable
echo "Building test executable..."
make
echo ""

# Check if test executable was created
if [ -f "test_all" ]; then
    echo "========================================"
    echo "✓ Build successful!"
    echo "========================================"
    echo ""
    echo "Test executable created: $TEST_BUILD_DIR/test_all"
    echo ""
    echo "Test modules included:"
    echo "  - test_peer.cpp      (Peer networking - 12 tests)"
    echo "  - test_rest.cpp      (REST API - 15 tests)"
    echo "  - test_blockfile.cpp (Block/Transaction/Wallet - 22 tests)"
    echo "  Total: 49 tests"
    echo ""
    echo "To run all tests:"
    echo "  cd $TEST_BUILD_DIR"
    echo "  ./test_all"
    echo ""
    echo "Or run directly:"
    echo "  $TEST_BUILD_DIR/test_all"
    echo ""
else
    echo "========================================"
    echo "✗ Build failed"
    echo "========================================"
    echo ""
    echo "The test executable was not created."
    exit 1
fi
