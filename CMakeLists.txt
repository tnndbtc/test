cmake_minimum_required(VERSION 3.10)
project(RestDaemon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenSSL
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    message(STATUS "OpenSSL include: ${OPENSSL_INCLUDE_DIR}")
    message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
endif()

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
endif()

include_directories(include src ${OPENSSL_INCLUDE_DIR})

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
)

if(APPLE)
    set_target_properties(bwthreadname PROPERTIES
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

set_target_properties(bwlogger PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
)

if(APPLE)
    set_target_properties(bwlogger PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link threadname to logger (logger needs GetThreadName)
target_link_libraries(bwlogger
    PUBLIC bwthreadname
)

# ============================================================
# Utils Library (libbwutils.so / .dylib / .dll)
# Hash and configuration utilities
# ============================================================
add_library(bwutils SHARED
    src/utils/hash.cpp
    src/utils/config.cpp
)

set_target_properties(bwutils PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
)

if(APPLE)
    set_target_properties(bwutils PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link OpenSSL and threadname to bwutils (needed for hash and config)
target_link_libraries(bwutils
    PUBLIC OpenSSL::SSL
    PUBLIC OpenSSL::Crypto
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# ============================================================
# Peer Library (libbwpeer.so / .dylib / .dll)
# P2P networking
# ============================================================
add_library(bwpeer SHARED
    src/peer/peer.cpp
)

set_target_properties(bwpeer PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
)

if(APPLE)
    set_target_properties(bwpeer PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link logger and threadname to peer
target_link_libraries(bwpeer
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# ============================================================
# Blockcore Library (libbwblockcore.so / .dylib / .dll)
# Core blockchain implementation: block, blockweave, transaction, daemon, mining, blockfile
# ============================================================
add_library(bwblockcore SHARED
    src/blockcore/block.cpp
    src/blockcore/blockweave.cpp
    src/blockcore/transaction.cpp
    src/blockcore/daemon.cpp
    src/blockcore/mining.cpp
    src/blockcore/blockfile.cpp
)

set_target_properties(bwblockcore PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
)

if(APPLE)
    set_target_properties(bwblockcore PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link dependencies to bwblockcore
target_link_libraries(bwblockcore
    PUBLIC bwutils
    PUBLIC bwlogger
    PUBLIC bwthreadname
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
    PUBLIC bwutils
    PUBLIC bwlogger
    PUBLIC bwthreadname
)

# ============================================================
# Main Executable: rest_daemon
# Links with all libraries
# ============================================================
add_executable(rest_daemon
    src/main.cpp
    src/wallet/wallet.cpp
)

# Link rest_daemon with all libraries
target_link_libraries(rest_daemon
    PRIVATE bwblockcore
    PRIVATE bwrest
    PRIVATE bwpeer
    PRIVATE bwutils
    PRIVATE bwlogger
    PRIVATE bwthreadname
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

# Set rpath so rest_daemon can find the shared libraries
if(APPLE)
    set_target_properties(rest_daemon PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "@executable_path/../lib;${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(UNIX)
    set_target_properties(rest_daemon PROPERTIES
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
    PRIVATE bwblockcore
    PRIVATE bwutils
    PRIVATE bwlogger
    PRIVATE bwthreadname
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

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
# Daemon CLI: daemon_cli
# Control utility for starting/stopping rest_daemon
# Lightweight - standalone executable
# ============================================================
add_executable(daemon_cli
    src/cli/daemon_cli.cpp
)

# daemon_cli doesn't need libraries - it only starts/stops rest_daemon
# No linking needed - standalone executable

# Platform-specific libraries
if(WIN32)
    # Windows specific settings
    target_compile_definitions(rest_daemon PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(wallet PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(daemon_cli PRIVATE _WIN32_WINNT=0x0601)
else()
    # Linux/macOS: Use POSIX threads for threading support
    find_package(Threads REQUIRED)
    target_link_libraries(bwthreadname PUBLIC Threads::Threads)
    target_link_libraries(bwblockcore PUBLIC Threads::Threads)
    target_link_libraries(bwrest PUBLIC Threads::Threads)
    target_link_libraries(bwpeer PUBLIC Threads::Threads)
    target_link_libraries(rest_daemon PRIVATE Threads::Threads)
    target_link_libraries(wallet PRIVATE Threads::Threads)
    # daemon_cli doesn't use threads - no linking needed
endif()

# Platform-specific compile options
if(MSVC)
    # Visual Studio
    target_compile_options(bwthreadname PRIVATE /W4)
    target_compile_options(bwlogger PRIVATE /W4)
    target_compile_options(bwutils PRIVATE /W4)
    target_compile_options(bwpeer PRIVATE /W4)
    target_compile_options(bwblockcore PRIVATE /W4)
    target_compile_options(bwrest PRIVATE /W4)
    target_compile_options(rest_daemon PRIVATE /W4)
    target_compile_options(wallet PRIVATE /W4)
    target_compile_options(daemon_cli PRIVATE /W4)
else()
    # GCC/Clang
    target_compile_options(bwthreadname PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwlogger PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwutils PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwpeer PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwblockcore PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(bwrest PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(rest_daemon PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(wallet PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(daemon_cli PRIVATE -Wall -Wextra -pedantic)
endif()

# ============================================================
# Copy test directory to build/test after build
# ============================================================
add_custom_command(TARGET rest_daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/test
        ${CMAKE_BINARY_DIR}/test
    COMMENT "Copying test directory to build/test"
)

# ============================================================
# Copy blockweave.conf to build directory after build
# ============================================================
add_custom_command(TARGET rest_daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/src/conf/blockweave.conf
        ${CMAKE_BINARY_DIR}/blockweave.conf
    COMMENT "Copying blockweave.conf to build directory"
)

# Installation
install(TARGETS bwthreadname bwlogger bwutils bwpeer bwblockcore bwrest
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(TARGETS rest_daemon wallet daemon_cli DESTINATION bin)
