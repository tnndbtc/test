cmake_minimum_required(VERSION 3.10)

# Set policy to use <PackageName>_ROOT variables
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

# Use legacy FindBoost.cmake for header-only Boost libraries
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

project(RestDaemon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force DWARF-4 for compatibility between Apple Clang and Homebrew LLVM
if(APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-gdwarf-4)
endif()

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

# Build type configuration
# Note: For multi-config generators (Visual Studio, Xcode), CMAKE_BUILD_TYPE is empty
# at configure time. We use generator expressions to set flags per-configuration.
if(MSVC)
    # Visual Studio: Multi-config generator
    # Use generator expressions to set flags per configuration
    message(STATUS "MSVC: Using multi-config generator (Debug/Release determined at build time)")

    # Debug configuration flags
    add_compile_options($<$<CONFIG:Debug>:/Od>)       # Disable optimization
    add_compile_options($<$<CONFIG:Debug>:/Zi>)       # Generate debug info
    add_compile_options($<$<CONFIG:Debug>:/RTC1>)     # Runtime checks
    add_link_options($<$<CONFIG:Debug>:/DEBUG>)       # Generate PDB
    add_link_options($<$<CONFIG:Debug>:/INCREMENTAL>) # Incremental linking

    # Release configuration flags
    add_compile_options($<$<CONFIG:Release>:/O2>)     # Optimize for speed
    add_compile_options($<$<CONFIG:Release>:/DNDEBUG>) # Disable assertions
    add_compile_options($<$<CONFIG:Release>:/D_ITERATOR_DEBUG_LEVEL=0>) # Disable debug iterator checks
    # Note: /GL (whole program optimization) is applied per-target for executables only
    # to avoid conflicts with CMake's automatic DLL export generation
    add_link_options($<$<CONFIG:Release>:/OPT:REF>)   # Remove unreferenced functions/data
    add_link_options($<$<CONFIG:Release>:/OPT:ICF>)   # Identical COMDAT folding

    message(STATUS "MSVC Debug flags: /Od /Zi /RTC1 /DEBUG /INCREMENTAL")
    message(STATUS "MSVC Release flags: /O2 /DNDEBUG /D_ITERATOR_DEBUG_LEVEL=0 /OPT:REF /OPT:ICF (+ /GL /LTCG for executables)")
else()
    # GCC/Clang/MinGW: Single-config generator
    # Set default build type if not specified
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug or Release)" FORCE)
        message(STATUS "Build type not specified, defaulting to Release")
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Build type: Debug")
        # GCC/MinGW Debug flags
        add_compile_options(-g -ggdb -O0 -fno-omit-frame-pointer -fno-optimize-sibling-calls)

        if(WIN32)
            # Windows-specific: Generate DWARF debug info for MinGW
            message(STATUS "MinGW Debug: Enabled -g -ggdb -O0 (DWARF debug symbols)")
        elseif(UNIX AND NOT APPLE)
            # Linux-specific: Enable sanitizers for Debug builds
            add_compile_options(-fsanitize=address,leak -fsanitize=undefined)
            add_link_options(-fsanitize=address,leak -fsanitize=undefined)
            message(STATUS "Linux Debug: Enabled -g -ggdb -O0 with AddressSanitizer and UndefinedBehaviorSanitizer")
        elseif(APPLE)
            # macOS-specific: Enable sanitizers if supported
            add_compile_options(-fsanitize=address -fsanitize=undefined)
            add_link_options(-fsanitize=address -fsanitize=undefined)
            message(STATUS "macOS Debug: Enabled -g -ggdb -O0 with AddressSanitizer and UndefinedBehaviorSanitizer")
        else()
            message(STATUS "GCC/Clang Debug: Enabled -g -ggdb -O0")
        endif()
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(STATUS "Build type: Release")
        add_compile_options(-O3)
    else()
        message(WARNING "Unknown build type: ${CMAKE_BUILD_TYPE}. Using Release.")
        set(CMAKE_BUILD_TYPE Release)
        add_compile_options(-O3)
    endif()
endif()

# Find OpenSSL
# Add hints for Windows common installation paths
if(WIN32)
    # If user hasn't specified OPENSSL_ROOT_DIR, search common locations
    if(NOT DEFINED OPENSSL_ROOT_DIR)
        # Check vcpkg first
        if(DEFINED ENV{VCPKG_ROOT})
            if(EXISTS "$ENV{VCPKG_ROOT}/installed/x64-windows/include/openssl/ssl.h")
                set(OPENSSL_ROOT_DIR "$ENV{VCPKG_ROOT}/installed/x64-windows" CACHE PATH "OpenSSL root directory")
                message(STATUS "Found OpenSSL via vcpkg: ${OPENSSL_ROOT_DIR}")
            endif()
        endif()

        # Check common installation paths
        if(NOT DEFINED OPENSSL_ROOT_DIR)
            foreach(path
                "C:/OpenSSL-Win64"
                "C:/Program Files/OpenSSL-Win64"
                "C:/Program Files/OpenSSL"
                "C:/OpenSSL"
            )
                if(EXISTS "${path}/include/openssl/ssl.h")
                    set(OPENSSL_ROOT_DIR "${path}" CACHE PATH "OpenSSL root directory")
                    message(STATUS "Found OpenSSL at: ${OPENSSL_ROOT_DIR}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    # For non-vcpkg OpenSSL with custom lib structure (e.g., lib/VC/x64/MD/ or lib/mingw/)
    if(DEFINED OPENSSL_ROOT_DIR AND NOT DEFINED OPENSSL_CRYPTO_LIBRARY)
        # MSVC-style paths: lib/VC/x64/MD/
        if(MSVC AND EXISTS "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libssl.lib")
            set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libssl.lib" CACHE FILEPATH "OpenSSL SSL library")
            set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libcrypto.lib" CACHE FILEPATH "OpenSSL Crypto library")
            message(STATUS "Using MSVC-style OpenSSL libraries: ${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/")
        # MinGW-style paths: lib/mingw/
        elseif(MINGW AND EXISTS "${OPENSSL_ROOT_DIR}/lib/mingw/libssl.dll.a")
            set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/mingw/libssl.dll.a" CACHE FILEPATH "OpenSSL SSL library")
            set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/mingw/libcrypto.dll.a" CACHE FILEPATH "OpenSSL Crypto library")
            message(STATUS "Using MinGW-style OpenSSL libraries: ${OPENSSL_ROOT_DIR}/lib/mingw/")
        endif()
    endif()
endif()

# Add hints for macOS Homebrew paths
if(APPLE)
    set(OPENSSL_ROOT_DIR
        /opt/homebrew/opt/openssl@3
        /usr/local/opt/openssl@3
        /opt/homebrew/opt/openssl@1.1
        /usr/local/opt/openssl@1.1
    )
endif()

find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    message(STATUS "OpenSSL include: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
endif()

# Find Boost (minimum version 1.70)
# Note: Boost.Asio can be used header-only (no linking required)

# Add hints for Windows common installation paths
if(WIN32)
    # If user hasn't specified BOOST_ROOT or Boost_ROOT, search common locations
    if(NOT DEFINED BOOST_ROOT AND NOT DEFINED Boost_ROOT)
        foreach(path
            "C:/local/boost_1_89_0"
            "C:/local/boost_1_88_0"
            "C:/local/boost_1_87_0"
            "C:/local/boost"
            "C:/Boost"
            "C:/Program Files/Boost"
        )
            if(EXISTS "${path}/boost/version.hpp")
                set(BOOST_ROOT "${path}" CACHE PATH "Boost root directory")
                message(STATUS "Found Boost at: ${BOOST_ROOT}")
                break()
            endif()
        endforeach()
    endif()
endif()

find_package(Boost 1.70 REQUIRED)
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include: ${Boost_INCLUDE_DIRS}")
endif()

# Disable deprecated Boost.Asio features
add_definitions(-DBOOST_ASIO_NO_DEPRECATED)

# ============================================================
# secp256k1 Library Configuration
# Bitcoin's elliptic curve cryptography library
# ============================================================
message(STATUS "Configuring secp256k1...")

# Set secp256k1 build options before adding subdirectory
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "Build secp256k1 benchmarks" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "Build secp256k1 tests" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "Build secp256k1 exhaustive tests" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "Build secp256k1 constant time tests" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "Build secp256k1 examples" FORCE)

# Disable all optional modules (we only need core functionality)
set(SECP256K1_ENABLE_MODULE_RECOVERY OFF CACHE BOOL "Enable ECDSA pubkey recovery module" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "Enable ECDH module" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "Enable Schnorr signatures module" FORCE)
set(SECP256K1_ENABLE_MODULE_MUSIG OFF CACHE BOOL "Enable MuSig module" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLSWIFT OFF CACHE BOOL "Enable ElligatorSwift module" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS OFF CACHE BOOL "Enable extra keys module" FORCE)

# Disable ASM to avoid register exhaustion with sanitizers in Debug builds
# The C implementation is slightly slower but works correctly with ASan/UBSan
set(SECP256K1_ASM OFF CACHE BOOL "Disable assembly optimizations" FORCE)

# Build as static library
set(BUILD_SHARED_LIBS OFF)

# Add secp256k1 as subdirectory
add_subdirectory(src/utils/secp256k1)

# Set position independent code for secp256k1 to allow linking into shared library
set_target_properties(secp256k1 PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Also set for precomputed target if it exists
if(TARGET secp256k1_precomputed)
    set_target_properties(secp256k1_precomputed PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# secp256k1 is now available as target: secp256k1
message(STATUS "secp256k1 configured successfully")

# Platform detection
if(WIN32)
    message(STATUS "Configuring for Windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    message(STATUS "Configuring for macOS")
    add_definitions(-DPLATFORM_MACOS)
elseif(UNIX)
    message(STATUS "Configuring for Linux")
    add_definitions(-DPLATFORM_LINUX)
    add_definitions(-D__LINUX__)  # Linux-specific macro for conditional compilation
endif()

include_directories(include src ${OPENSSL_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})

# ============================================================
# Thread Naming Library (libbwthreadname.so / .dylib / .dll)
# Thread naming utilities (no dependencies)
# ============================================================
add_library(bwthreadname SHARED
    src/utils/threadname.cpp
)

set_target_properties(bwthreadname PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwthreadname PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# ============================================================
# Path Utilities Library (libbwpathutil.so / .dylib / .dll)
# Path manipulation utilities (no dependencies)
# ============================================================
add_library(bwpathutil SHARED
    src/utils/pathutil.cpp
)

set_target_properties(bwpathutil PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwpathutil PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# ============================================================
# Logger Library (libbwlogger.so / .dylib / .dll)
# Logging utilities
# ============================================================
add_library(bwlogger SHARED
    src/logger/logger.cpp
)

# Define BUILDING_BWLOGGER when compiling this library (for DLL export on Windows)
target_compile_definitions(bwlogger PRIVATE BUILDING_BWLOGGER)

# Suppress Windows CRT security warnings for standard C functions
if(MSVC)
    target_compile_definitions(bwlogger PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(bwlogger PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwlogger PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link threadname and pathutil to logger
target_link_libraries(bwlogger
    PUBLIC bwthreadname
    PUBLIC bwpathutil
)

# ============================================================
# Utils Library (libbwutils.so / .dylib / .dll)
# Hash, configuration, network utilities, and public IP discovery
# ============================================================
add_library(bwutils SHARED
    src/utils/hash.cpp
    src/utils/config.cpp
    src/utils/network.cpp
    src/utils/get_public_ip.cpp
    src/utils/time_util.cpp
    src/utils/address.cpp
    src/utils/serialization.cpp
)

# Suppress false positive -Warray-bounds and -Wstringop-overflow warnings from GCC in Boost.Asio
# when compiling get_public_ip.cpp (GCC 15.2.0 flags memcpy in basic_resolver_results.hpp)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/utils/get_public_ip.cpp
        PROPERTIES COMPILE_FLAGS "-Wno-array-bounds -Wno-stringop-overflow"
    )
endif()

# Suppress Windows CRT security warnings for standard C functions (getenv, etc.)
if(MSVC)
    target_compile_definitions(bwutils PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_target_properties(bwutils PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwutils PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link OpenSSL, pathutil, logger, and threadname to bwutils
target_link_libraries(bwutils
    PUBLIC OpenSSL::SSL
    PUBLIC OpenSSL::Crypto
    PUBLIC bwlogger
    PUBLIC bwthreadname
    PUBLIC bwpathutil
)

# ============================================================
# Crypto Library (libbwcrypto.so / .dylib / .dll)
# Cryptographic primitives: keypair, keystore, keccak256, AES-CTR, scrypt
# ============================================================
add_library(bwcrypto SHARED
    src/crypto/keypair.cpp
    src/crypto/keccak256.cpp
    src/crypto/keystore.cpp
    src/crypto/aes_ctr.cpp
    src/crypto/scrypt.cpp
    src/utils/tiny-keccak.c
)

set_target_properties(bwcrypto PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwcrypto PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link dependencies to bwcrypto
target_link_libraries(bwcrypto
    PUBLIC OpenSSL::SSL
    PUBLIC OpenSSL::Crypto
    PUBLIC secp256k1
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# Add compiler options for crypto
if(MSVC)
    target_compile_options(bwcrypto PRIVATE /W4)
    target_compile_definitions(bwcrypto PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(bwcrypto PRIVATE -Wall -Wextra -pedantic)
endif()

# Platform-specific libraries for crypto
if(WIN32)
    target_compile_definitions(bwcrypto PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(bwcrypto PUBLIC ws2_32)
else()
    target_link_libraries(bwcrypto PUBLIC Threads::Threads)
endif()

# ============================================================
# Peer Library (libbwpeer.so / .dylib / .dll)
# P2P networking
# Note: peer_message.cpp is in bwpeermsg library to break circular dependency
# ============================================================
add_library(bwpeer SHARED
    src/peer/peer_manager.cpp
    src/peer/peer_filter.cpp
    src/peer/peer_node.cpp
)

set_target_properties(bwpeer PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwpeer PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
        LINK_FLAGS "-undefined dynamic_lookup"
    )
endif()

# Link logger, threadname, and utils to peer
# Note: Boost.Asio is header-only, no linking required
target_link_libraries(bwpeer
    PUBLIC bwutils
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# ============================================================
# Peer Message Library (libbwpeermsg.so / .dylib / .dll)
# Peer message format (no dependencies on bwpeer or bwblockcore)
# This breaks the circular dependency between bwpeer and bwblockcore
# Includes:
# - peer_message.cpp: Abstract base class for P2P messages
# - peer_message_legacy.cpp: Legacy concrete implementation (Phase 1)
# - peer_message_factory.cpp: Factory for creating message instances
# - messages/ping_message.cpp: Type-safe PING message (Phase 2)
# - messages/pong_message.cpp: Type-safe PONG message (Phase 2)
# - messages/inventory_message.cpp: Type-safe INVENTORY message (Phase 3)
# - messages/getdata_message.cpp: Type-safe GETDATA message (Phase 3)
# ============================================================
add_library(bwpeermsg SHARED
    src/peer/peer_message.cpp
    src/peer/peer_message_factory.cpp
    src/peer/messages/ping_message.cpp
    src/peer/messages/pong_message.cpp
    src/peer/messages/inventory_message.cpp
    src/peer/messages/getdata_message.cpp
    src/peer/messages/version_message.cpp
    src/peer/messages/verack_message.cpp
    src/peer/messages/tx_message.cpp
    src/peer/messages/block_message.cpp
    src/peer/messages/getpeers_message.cpp
    src/peer/messages/peers_message.cpp
    src/peer/messages/getchain_message.cpp
    src/peer/messages/chaininfo_message.cpp
    src/peer/messages/unknown_message.cpp
)

set_target_properties(bwpeermsg PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwpeermsg PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link dependencies to bwpeermsg
# Note: Added bwblockcore dependency for CBlock and CTransaction serialization
target_link_libraries(bwpeermsg
    PUBLIC bwutils
    PUBLIC bwlogger
    PUBLIC bwthreadname
    PUBLIC bwblockcore
)

# Now link bwpeermsg to bwpeer
target_link_libraries(bwpeer
    PUBLIC bwpeermsg
)

# ============================================================
# Blockcore Library (libbwblockcore.so / .dylib / .dll)
# Core blockchain implementation: block, block_weave, transaction, daemon, mining, block_file
# Note: win_service.cpp is compiled directly into bweave.exe, not this library
# ============================================================
add_library(bwblockcore SHARED
    src/blockcore/block.cpp
    src/blockcore/block_weave.cpp
    src/blockcore/transaction.cpp
    src/blockcore/transaction_payload.cpp
    src/blockcore/daemon.cpp
    src/blockcore/mining.cpp
    src/blockcore/block_file.cpp
)

# Define BUILDING_BWBLOCKCORE when compiling this library (for DLL export on Windows)
target_compile_definitions(bwblockcore PRIVATE BUILDING_BWBLOCKCORE)

# Windows-specific compile definitions
if(MSVC)
    target_compile_definitions(bwblockcore PRIVATE
        _CRT_SECURE_NO_WARNINGS  # Suppress gmtime warnings
        NOMINMAX                  # Prevent Windows min/max macro conflicts with std::min/std::max
    )
endif()

set_target_properties(bwblockcore PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwblockcore PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
        LINK_FLAGS "-undefined dynamic_lookup"
    )
endif()

# Link dependencies to bwblockcore
# Note: Removed bwpeermsg dependency to break circular dependency
# bwpeermsg now depends on bwblockcore for CBlock/CTransaction serialization
target_link_libraries(bwblockcore
    PUBLIC bwutils
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# Now link bwblockcore to bwpeer so it can use CTransaction and CBlock
target_link_libraries(bwpeer
    PUBLIC bwblockcore
)

# ============================================================
# REST API Library (libbwrest.so / .dylib / .dll)
# REST API server
# ============================================================
add_library(bwrest SHARED
    src/rest/rest_api_server.cpp
    src/rest/request_queue.cpp
)

set_target_properties(bwrest PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(APPLE)
    set_target_properties(bwrest PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link dependencies to bwrest
target_link_libraries(bwrest
    PUBLIC bwblockcore
    PUBLIC bwpeer
    PUBLIC bwutils
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# ============================================================
# Main Executable: bweave
# Links with all libraries
# Windows: Also includes win_service.cpp for Windows Service support
# ============================================================
if(WIN32)
    add_executable(bweave
        src/main.cpp
        src/wallet/wallet.cpp
        src/blockcore/win_service.cpp
    )
else()
    add_executable(bweave
        src/main.cpp
        src/wallet/wallet.cpp
    )
endif()

# Link bweave with all libraries
target_link_libraries(bweave
    PRIVATE bwcrypto
    PRIVATE bwblockcore
    PRIVATE bwrest
    PRIVATE bwpeer
    PRIVATE bwutils
    PRIVATE bwlogger
    PRIVATE bwthreadname
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

# MSVC Release: Enable link-time code generation for executables
if(MSVC)
    target_compile_options(bweave PRIVATE $<$<CONFIG:Release>:/GL>)
    target_link_options(bweave PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# Set rpath so bweave can find the shared libraries
if(APPLE)
    set_target_properties(bweave PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "@executable_path/../lib;${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(UNIX)
    set_target_properties(bweave PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "\$ORIGIN/../lib:${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
endif()

# ============================================================
# Wallet Executable: wallet
# Links with bwblockcore and its dependencies
# ============================================================
add_executable(wallet
    src/wallet/wallet_main.cpp
    src/wallet/wallet.cpp
)

# Link wallet executable with libraries
target_link_libraries(wallet
    PRIVATE bwcrypto
    PRIVATE bwblockcore
    PRIVATE bwutils
    PRIVATE bwlogger
    PRIVATE bwthreadname
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

# MSVC Release: Enable link-time code generation for executables
if(MSVC)
    target_compile_options(wallet PRIVATE $<$<CONFIG:Release>:/GL>)
    target_link_options(wallet PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# Set rpath so wallet can find the shared libraries
if(APPLE)
    set_target_properties(wallet PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "@executable_path/../lib;${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(UNIX)
    set_target_properties(wallet PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "\$ORIGIN/../lib:${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
endif()

# ============================================================
# Daemon CLI: bweave_cli
# Control utility for starting/stopping bweave
# Lightweight - standalone executable
# Cross-platform: Works on Windows (service control) and POSIX (daemon control)
# ============================================================
add_executable(bweave_cli
    src/cli/bweave_cli.cpp
)

# MSVC Release: Enable link-time code generation for executables
if(MSVC)
    target_compile_options(bweave_cli PRIVATE $<$<CONFIG:Release>:/GL>)
    target_link_options(bweave_cli PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# bweave_cli doesn't need libraries on POSIX - it only starts/stops bweave
# On Windows, it needs advapi32 for Service Control Manager APIs

# Platform-specific libraries
if(WIN32)
    # Windows specific settings - Define _WIN32_WINNT for all targets to suppress warnings
    # 0x0601 = Windows 7, required for Boost.Asio and Windows Service APIs
    target_compile_definitions(bwutils PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(bwpeer PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(bwrest PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(bweave PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(wallet PRIVATE _WIN32_WINNT=0x0601)
    if(TARGET bweave_cli)
        target_compile_definitions(bweave_cli PRIVATE _WIN32_WINNT=0x0601)
    endif()

    # Link Windows socket library (ws2_32), mswsock for AcceptEx, and service library (advapi32)
    target_link_libraries(bwutils PUBLIC ws2_32)  # For get_public_ip.cpp network functions
    target_link_libraries(bwpeermsg PUBLIC ws2_32)
    target_link_libraries(bwpeer PUBLIC ws2_32 mswsock)
    target_link_libraries(bwblockcore PUBLIC ws2_32 advapi32)  # For network functions and Windows Service APIs
    target_link_libraries(bweave PRIVATE ws2_32 advapi32)
    target_link_libraries(wallet PRIVATE ws2_32)
    if(TARGET bweave_cli)
        target_link_libraries(bweave_cli PRIVATE advapi32)  # For Service Control Manager APIs
    endif()
else()
    # Linux/macOS: Use POSIX threads for threading support
    find_package(Threads REQUIRED)
    target_link_libraries(bwthreadname PUBLIC Threads::Threads)
    target_link_libraries(bwpeermsg PUBLIC Threads::Threads)
    target_link_libraries(bwblockcore PUBLIC Threads::Threads)
    target_link_libraries(bwrest PUBLIC Threads::Threads)
    target_link_libraries(bwpeer PUBLIC Threads::Threads)
    target_link_libraries(bweave PRIVATE Threads::Threads)
    target_link_libraries(wallet PRIVATE Threads::Threads)
    # bweave_cli doesn't use threads - no linking needed
endif()

# Platform-specific compile options
if(MSVC)
    # Visual Studio
    target_compile_options(bwthreadname PRIVATE /W4)
    target_compile_options(bwlogger PRIVATE /W4)
    target_compile_options(bwutils PRIVATE /W4)
    target_compile_options(bwpeermsg PRIVATE /W4)
    target_compile_options(bwpeer PRIVATE /W4)
    target_compile_options(bwblockcore PRIVATE /W4)
    target_compile_options(bwrest PRIVATE /W4)
    target_compile_options(bweave PRIVATE /W4)
    target_compile_options(wallet PRIVATE /W4)
    if(TARGET bweave_cli)
        target_compile_options(bweave_cli PRIVATE /W4)
    endif()
else()
    # GCC/Clang
    target_compile_options(bwthreadname PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwlogger PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwutils PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwpeermsg PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwpeer PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwblockcore PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwrest PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bweave PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(wallet PRIVATE -Wall -Wextra -pedantic)
    if(TARGET bweave_cli)
        target_compile_options(bweave_cli PRIVATE -Wall -Wextra -pedantic)
    endif()
endif()

# ============================================================
# Copy test directory to build/test after build
# ============================================================
add_custom_command(TARGET bweave POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test
        ${CMAKE_BINARY_DIR}/test
    COMMENT "Copying test directory to build/test"
)

# ============================================================
# Copy bweave.conf to build directory after build
# Windows: Use bweave_win.conf and replace %APPDATA% with actual value
# POSIX: Use bweave.conf as-is
# ============================================================
if(WIN32)
    # On Windows, copy bweave_win.conf and replace %APPDATA% with actual value
    # Use CMake's file(GENERATE) to replace %APPDATA% at build time
    file(READ ${CMAKE_SOURCE_DIR}/src/conf/bweave_win.conf BWEAVE_CONF_CONTENT)

    # Get APPDATA environment variable
    if(DEFINED ENV{APPDATA})
        set(APPDATA_PATH "$ENV{APPDATA}")
    else()
        # Fallback if APPDATA not set
        set(APPDATA_PATH "C:/Users/$ENV{USERNAME}/AppData/Roaming")
    endif()

    # Replace backslashes with forward slashes for consistency
    string(REPLACE "\\" "/" APPDATA_PATH "${APPDATA_PATH}")

    # Replace %APPDATA% with actual value
    string(REPLACE "%APPDATA%" "${APPDATA_PATH}" BWEAVE_CONF_CONTENT "${BWEAVE_CONF_CONTENT}")

    # Write to build directory
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/bweave.conf CONTENT "${BWEAVE_CONF_CONTENT}")

    # Also copy to %APPDATA%\bweave for Windows service access
    # Services run with different working directory and need config in known location
    add_custom_command(TARGET bweave POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APPDATA_PATH}/bweave"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/bweave.conf
            "${APPDATA_PATH}/bweave/bweave.conf"
        COMMENT "Copying bweave.conf to ${APPDATA_PATH}/bweave for Windows service"
    )

    message(STATUS "Windows config: Using bweave_win.conf with APPDATA=${APPDATA_PATH}")
    message(STATUS "Windows config: Will copy to ${APPDATA_PATH}/bweave/bweave.conf for service")
else()
    # On POSIX, use bweave.conf as-is
    add_custom_command(TARGET bweave POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/src/conf/bweave.conf
            ${CMAKE_BINARY_DIR}/bweave.conf
        COMMENT "Copying bweave.conf to build directory"
    )
endif()

# Installation
install(TARGETS bwthreadname bwlogger bwutils bwcrypto bwpeermsg bwpeer bwblockcore bwrest
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
if(TARGET bweave_cli)
    install(TARGETS bweave wallet bweave_cli DESTINATION bin)
else()
    install(TARGETS bweave wallet DESTINATION bin)
endif()

# ============================================================
# Unit Tests
# Add test subdirectory to build unit tests alongside main project
# Use -DBUILD_TESTS=ON to enable test building (default: OFF)
# ============================================================
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    message(STATUS "Unit tests: Enabled")
    add_subdirectory(src/test)

    # Enable CTest for running tests
    enable_testing()
    add_test(NAME unit_tests COMMAND test_all WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/test)
else()
    message(STATUS "Unit tests: Disabled (use -DBUILD_TESTS=ON to enable)")
endif()
