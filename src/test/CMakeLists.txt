cmake_minimum_required(VERSION 3.10)
project(BlockweaveUnitTests VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set policy to use <PackageName>_ROOT variables
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

# Use legacy FindBoost.cmake for header-only Boost libraries
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# Find OpenSSL
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
endif()

# Find Boost
find_package(Boost 1.70 REQUIRED)
if(Boost_FOUND)
    message(STATUS "Boost found: ${Boost_VERSION}")
endif()

# Find Threads
find_package(Threads REQUIRED)

# Platform detection
if(WIN32)
    message(STATUS "Configuring tests for Windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    message(STATUS "Configuring tests for macOS")
    add_definitions(-DPLATFORM_MACOS)
elseif(UNIX)
    message(STATUS "Configuring tests for Linux")
    add_definitions(-DPLATFORM_LINUX)
    add_definitions(-D__LINUX__)  # Linux-specific macro for conditional compilation
endif()

# Determine if building standalone or as part of parent project
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone build
    message(STATUS "Building unit tests standalone")

    # Set paths to parent project
    set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    set(BUILD_DIR "${PROJECT_ROOT}/build")

    # Check if libraries exist
    if(APPLE)
        set(LIB_EXT "dylib")
    elseif(WIN32)
        set(LIB_EXT "dll")
    else()
        set(LIB_EXT "so")
    endif()

    set(REQUIRED_LIBS
        "${BUILD_DIR}/libbwpeer.${LIB_EXT}"
        "${BUILD_DIR}/libbwlogger.${LIB_EXT}"
        "${BUILD_DIR}/libbwthreadname.${LIB_EXT}"
        "${BUILD_DIR}/libbwblockcore.${LIB_EXT}"
        "${BUILD_DIR}/libbwrest.${LIB_EXT}"
        "${BUILD_DIR}/libbwutils.${LIB_EXT}"
    )

    set(LIBS_MISSING FALSE)
    foreach(lib ${REQUIRED_LIBS})
        if(NOT EXISTS ${lib})
            message(WARNING "Library not found: ${lib}")
            set(LIBS_MISSING TRUE)
        endif()
    endforeach()

    if(LIBS_MISSING)
        message(FATAL_ERROR
            "\n=========================================================\n"
            "Required libraries not found!\n"
            "Please build the main project first:\n"
            "  cd ${BUILD_DIR}\n"
            "  cmake ..\n"
            "  make\n"
            "Then return here and rebuild the tests.\n"
            "=========================================================\n")
    endif()

    message(STATUS "Found all required libraries in ${BUILD_DIR}")

    # Include directories
    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}  # For unit_test.h
        ${PROJECT_ROOT}/include
        ${PROJECT_ROOT}/src
        ${OPENSSL_INCLUDE_DIR}
        ${Boost_INCLUDE_DIRS}
    )

    # Link directories for shared libraries
    link_directories(${BUILD_DIR})

    # Define the unified test executable (includes all test modules)
    add_executable(test_all
        test_all.cpp
        test_peer_manager.cpp
        test_peer_message.cpp
        test_peer_filter.cpp
        test_request_queue.cpp
        test_api_server.cpp
        test_block.cpp
        test_transaction.cpp
        test_block_weave.cpp
        test_wallet.cpp
        test_hash.cpp
        test_block_file.cpp
        test_logger.cpp
        ${PROJECT_ROOT}/src/wallet/wallet.cpp
    )

    # Link with all required libraries from parent build
    target_link_libraries(test_all
        PRIVATE ${BUILD_DIR}/libbwrest.${LIB_EXT}
        PRIVATE ${BUILD_DIR}/libbwblockcore.${LIB_EXT}
        PRIVATE ${BUILD_DIR}/libbwutils.${LIB_EXT}
        PRIVATE ${BUILD_DIR}/libbwpeer.${LIB_EXT}
        PRIVATE ${BUILD_DIR}/libbwlogger.${LIB_EXT}
        PRIVATE ${BUILD_DIR}/libbwthreadname.${LIB_EXT}
        PRIVATE Threads::Threads
    )

    # Set rpath to find libraries
    if(APPLE)
        set_target_properties(test_all PROPERTIES
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH "${BUILD_DIR}"
            INSTALL_RPATH "@executable_path/../../build;@executable_path/../lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )
    elseif(UNIX)
        set_target_properties(test_all PROPERTIES
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH "${BUILD_DIR}"
            INSTALL_RPATH "\$ORIGIN/../../build:\$ORIGIN/../lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )
    endif()

else()
    # Building as part of parent project
    message(STATUS "Building unit tests as part of main project")

    # Include current directory for unit_test.h
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})

    # Define the unified test executable (includes all test modules)
    add_executable(test_all
        test_all.cpp
        test_peer_manager.cpp
        test_peer_message.cpp
        test_peer_filter.cpp
        test_request_queue.cpp
        test_api_server.cpp
        test_block.cpp
        test_transaction.cpp
        test_block_weave.cpp
        test_wallet.cpp
        test_hash.cpp
        test_block_file.cpp
        test_logger.cpp
        ../wallet/wallet.cpp
    )

    # Link with all required libraries (already defined in parent)
    target_link_libraries(test_all
        PRIVATE bwrest
        PRIVATE bwblockcore
        PRIVATE bwutils
        PRIVATE bwpeer
        PRIVATE bwlogger
        PRIVATE bwthreadname
    )

    # Platform-specific thread linking
    if(NOT WIN32)
        target_link_libraries(test_all PRIVATE Threads::Threads)
    endif()

    # Set rpath (same as parent project settings)
    if(APPLE)
        set_target_properties(test_all PROPERTIES
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "@executable_path/../lib;${CMAKE_INSTALL_PREFIX}/lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )
    elseif(UNIX)
        set_target_properties(test_all PROPERTIES
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "\$ORIGIN/../lib:${CMAKE_INSTALL_PREFIX}/lib"
            INSTALL_RPATH_USE_LINK_PATH TRUE
        )
    endif()
endif()

# Platform-specific compile options
if(MSVC)
    target_compile_options(test_all PRIVATE /W4)
else()
    target_compile_options(test_all PRIVATE -Wall -Wextra -pedantic)
endif()

# Windows-specific settings
if(WIN32)
    target_compile_definitions(test_all PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(test_all PRIVATE ws2_32)
endif()

# Print build information
message(STATUS "Unit test executable: test_all")
message(STATUS "Test sources: ${CMAKE_CURRENT_SOURCE_DIR}/test_all.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_peer_manager.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_peer_message.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_peer_filter.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_request_queue.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_api_server.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_block.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_transaction.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_block_weave.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_wallet.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_hash.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_block_file.cpp, ${CMAKE_CURRENT_SOURCE_DIR}/test_logger.cpp")
message(STATUS "Total tests: 139 (18 peer manager + 21 peer message + 21 peer filter + 10 request queue + 5 API server + 4 block + 5 transaction + 7 block weave + 3 wallet + 2 hash + 17 block file + 26 logger)")
