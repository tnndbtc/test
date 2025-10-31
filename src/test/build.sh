#!/bin/bash
# ============= build.sh =============
# Build script for unit tests
#
# This script builds the unit tests in standalone mode.
# It ensures the main project libraries are built first.
#
# Automatic setup:
# - If ../../build doesn't exist, runs ./configure to set up the project
# - If libraries are missing, builds them automatically
# - Then builds and links the test executable
#
# Usage:
#   ./build.sh           # Build with single process
#   ./build.sh -j4       # Build with 4 parallel processes
#   ./build.sh -j8       # Build with 8 parallel processes

set -e  # Exit on error

# Parse command line arguments
MAKE_JOBS=""
while [[ $# -gt 0 ]]; do
    case $1 in
        -j*)
            MAKE_JOBS="$1"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [-jN]"
            echo "  -jN    Use N parallel jobs for building (e.g., -j4)"
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}/../.."
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"

echo "========================================"
echo "Building Blockweave Unit Tests"
echo "========================================"
echo ""

# Determine library extension based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    LIB_EXT="dylib"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    LIB_EXT="dll"
else
    LIB_EXT="so"
fi

# Check if main project build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found: $BUILD_DIR"
    echo "Setting up main project..."
    echo ""

    # Check if configure script exists
    if [ ! -f "$PROJECT_ROOT/configure" ]; then
        echo "Error: configure script not found at $PROJECT_ROOT/configure"
        echo "Please ensure you are in the correct project directory."
        exit 1
    fi

    # Run configure script
    echo "Running configure script..."
    cd "$PROJECT_ROOT"
    ./configure --generator="Unix Makefiles"
    echo ""

    # Check if build directory was created
    if [ ! -d "$BUILD_DIR" ]; then
        echo "Error: configure script did not create build directory"
        exit 1
    fi

    echo "✓ Project configured successfully"
    echo ""
fi

# Check if required libraries exist
LIBS_EXIST=true
REQUIRED_LIBS=(
    "libbwpeer.${LIB_EXT}"
    "libbwlogger.${LIB_EXT}"
    "libbwthreadname.${LIB_EXT}"
    "libbwblockcore.${LIB_EXT}"
    "libbwrest.${LIB_EXT}"
    "libbwutils.${LIB_EXT}"
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

    # Navigate to build directory
    cd "$BUILD_DIR"

    # Check if Makefile exists, if not run cmake
    if [ ! -f "Makefile" ]; then
        echo "Running cmake to generate build files..."
        cmake ..
        if [ $? -ne 0 ]; then
            echo "Error: cmake configuration failed"
            exit 1
        fi
        echo ""
    fi

    # Build the required libraries
    if [ -n "$MAKE_JOBS" ]; then
        echo "Building libraries with $MAKE_JOBS parallel jobs (this may take a few minutes)..."
        make $MAKE_JOBS bwthreadname bwlogger bwpeer bwutils bwblockcore bwrest
    else
        echo "Building libraries (this may take a few minutes)..."
        make bwthreadname bwlogger bwpeer bwutils bwblockcore bwrest
    fi

    if [ $? -ne 0 ]; then
        echo ""
        echo "Error: Build failed. Please check the error messages above."
        exit 1
    fi
    echo ""

    # Verify libraries were built successfully
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

# Remove old test build directory if it exists (for clean build)
if [ -d "$TEST_BUILD_DIR" ]; then
    echo "Removing existing build directory: $TEST_BUILD_DIR"
    rm -rf "$TEST_BUILD_DIR"
fi

# Create fresh test build directory
echo "Creating build directory: $TEST_BUILD_DIR"
mkdir -p "$TEST_BUILD_DIR"

cd "$TEST_BUILD_DIR"

# Run cmake
echo "Running cmake..."
cmake ..
echo ""

# Build test executable
if [ -n "$MAKE_JOBS" ]; then
    echo "Building test executable with $MAKE_JOBS parallel jobs..."
    make $MAKE_JOBS
else
    echo "Building test executable..."
    make
fi
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
    echo "  - test_peer_manager.cpp  (Peer manager - 18 tests)"
    echo "  - test_peer_message.cpp  (Peer message protocol - 21 tests)"
    echo "  - test_request_queue.cpp (Request queue - 10 tests)"
    echo "  - test_api_server.cpp    (API server - 5 tests)"
    echo "  - test_block.cpp         (Block - 4 tests)"
    echo "  - test_transaction.cpp   (Transaction - 5 tests)"
    echo "  - test_block_weave.cpp   (Block weave - 7 tests)"
    echo "  - test_wallet.cpp        (Wallet - 3 tests)"
    echo "  - test_hash.cpp          (Hash - 2 tests)"
    echo "  - test_block_file.cpp    (Block file persistence - 17 tests)"
    echo "  - test_logger.cpp        (Logger - 26 tests)"
    echo "  Total: 118 tests"
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
