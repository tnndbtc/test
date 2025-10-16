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
# Blockweave Core Library (libblockweave_core.so / .dylib / .dll)
# Contains: block, blockweave, transaction, hash utilities, REST API, CLI, Peer
# Thread-safe with mutex protection
# Built as SHARED library for dynamic linking
# ============================================================
add_library(blockweave_core SHARED
    src/block.cpp
    src/blockweave.cpp
    src/utils/hash.cpp
    src/transaction.cpp
    src/rest/rest_api.cpp
    src/cli/config.cpp
    src/cli/daemon.cpp
    src/logger/logger.cpp
    src/peer/peer.cpp
)

# Set library version and properties
set_target_properties(blockweave_core PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
)

# On macOS, set the install name for the dynamic library
if(APPLE)
    set_target_properties(blockweave_core PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Link OpenSSL to library
target_link_libraries(blockweave_core
    PUBLIC OpenSSL::SSL
    PUBLIC OpenSSL::Crypto
)

# ============================================================
# Main Executable: rest_daemon
# Links with: libblockweave_core.so (shared library)
# ============================================================
add_executable(rest_daemon
    src/main.cpp
    src/wallet/wallet.cpp
)

# Link rest_daemon with blockweave_core library
target_link_libraries(rest_daemon
    PRIVATE blockweave_core
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

# Set rpath so rest_daemon can find the shared library
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
# Links with: libblockweave_core.so (shared library)
# ============================================================
add_executable(wallet
    src/wallet/wallet_main.cpp
    src/wallet/wallet.cpp
)

# Link wallet executable with blockweave_core library
target_link_libraries(wallet
    PRIVATE blockweave_core
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

# Set rpath so wallet can find the shared library
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
# ============================================================
add_executable(daemon_cli
    src/cli/daemon_cli.cpp
)

# Link daemon_cli with blockweave_core library
target_link_libraries(daemon_cli
    PRIVATE blockweave_core
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

# Set rpath so daemon_cli can find the shared library
if(APPLE)
    set_target_properties(daemon_cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "@executable_path/../lib;${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(UNIX)
    set_target_properties(daemon_cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "\$ORIGIN/../lib:${CMAKE_INSTALL_PREFIX}/lib"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
endif()

# Platform-specific libraries
if(WIN32)
    # Windows specific settings
    target_compile_definitions(rest_daemon PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(wallet PRIVATE _WIN32_WINNT=0x0601)
    target_compile_definitions(daemon_cli PRIVATE _WIN32_WINNT=0x0601)
else()
    # Linux/macOS: Use POSIX threads for threading support
    find_package(Threads REQUIRED)
    target_link_libraries(blockweave_core PUBLIC Threads::Threads)
    target_link_libraries(rest_daemon PRIVATE Threads::Threads)
    target_link_libraries(wallet PRIVATE Threads::Threads)
    target_link_libraries(daemon_cli PRIVATE Threads::Threads)
endif()

# Platform-specific compile options
if(MSVC)
    # Visual Studio
    target_compile_options(blockweave_core PRIVATE /W4)
    target_compile_options(rest_daemon PRIVATE /W4)
    target_compile_options(wallet PRIVATE /W4)
    target_compile_options(daemon_cli PRIVATE /W4)
else()
    # GCC/Clang
    target_compile_options(blockweave_core PRIVATE -Wall -Wextra -pedantic)
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
        ${CMAKE_SOURCE_DIR}/blockweave.conf
        ${CMAKE_BINARY_DIR}/blockweave.conf
    COMMENT "Copying blockweave.conf to build directory"
)

# Installation
install(TARGETS blockweave_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(TARGETS rest_daemon wallet daemon_cli DESTINATION bin)
