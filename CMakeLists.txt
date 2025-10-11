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

# Main executable: rest_daemon
add_executable(rest_daemon
    src/main.cpp
    src/block.cpp
    src/blockweave.cpp
    src/hash.cpp
    src/transaction.cpp
    src/wallet/wallet.cpp
)

# Wallet executable
add_executable(wallet
    src/wallet/wallet_main.cpp
    src/wallet/wallet.cpp
    src/hash.cpp
    src/transaction.cpp
)

# Link OpenSSL
target_link_libraries(rest_daemon OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(wallet OpenSSL::SSL OpenSSL::Crypto)

# Platform-specific libraries
if(WIN32)
    # Windows specific settings
    target_compile_definitions(rest_daemon PRIVATE _WIN32_WINNT=0x0601)
else()
    # Linux/macOS: Use POSIX threads if needed
    find_package(Threads REQUIRED)
    target_link_libraries(rest_daemon Threads::Threads)
endif()

# Platform-specific compile options
if(MSVC)
    # Visual Studio
    target_compile_options(rest_daemon PRIVATE /W4)
    target_compile_options(wallet PRIVATE /W4)
else()
    # GCC/Clang
    target_compile_options(rest_daemon PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(wallet PRIVATE -Wall -Wextra -pedantic)
endif()

# Installation
install(TARGETS rest_daemon wallet DESTINATION bin)
