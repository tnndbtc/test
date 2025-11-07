@echo off
REM ============= build.bat =============
REM Build script for unit tests on Windows
REM
REM This script builds the unit tests in standalone mode.
REM It ensures the main project libraries are built first.
REM
REM Automatic setup:
REM - If ..\..\build doesn't exist, runs configure.bat to set up the project
REM - If libraries are missing, builds them automatically
REM - Detects Boost and OpenSSL automatically
REM - Then builds and links the test executable
REM
REM Usage:
REM   build.bat           - Build tests
REM   build.bat clean     - Clean and rebuild
REM   build.bat -j4       - Build with 4 parallel jobs
REM   build.bat clean -j8 - Clean rebuild with 8 parallel jobs
REM
REM Environment Variables (optional):
REM   BOOST_ROOT          - Path to Boost installation (e.g., C:\local\boost_1_89_0)
REM   OPENSSL_ROOT_DIR    - Path to OpenSSL installation
REM
REM Examples:
REM   build.bat
REM   build.bat -j4
REM   build.bat clean -j8
REM   set BOOST_ROOT=C:\local\boost_1_89_0 && build.bat -j4

setlocal enabledelayedexpansion

REM Parse command line arguments
set CLEAN_BUILD=0
set PARALLEL_JOBS=

:parse_loop
if "%~1"=="" goto :end_parse

if "%~1"=="clean" (
    set CLEAN_BUILD=1
    shift
    goto :parse_loop
)

REM Check for -jN format (e.g., -j4, -j8)
set arg=%~1
if "%arg:~0,2%"=="-j" (
    set PARALLEL_JOBS=%arg:~2%
    shift
    goto :parse_loop
)

REM Unknown argument
echo Unknown option: %~1
echo.
echo Usage: build.bat [options]
echo   clean       Clean and rebuild
echo   -jN         Use N parallel jobs (e.g., -j4, -j8)
echo.
echo Examples:
echo   build.bat
echo   build.bat clean
echo   build.bat -j4
echo   build.bat clean -j8
exit /b 1

:end_parse

REM Determine script directory and project paths
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
set PROJECT_ROOT=%SCRIPT_DIR%\..\..
set BUILD_DIR=%PROJECT_ROOT%\build
set TEST_BUILD_DIR=%SCRIPT_DIR%\build

echo ========================================
echo Building Blockweave Unit Tests
echo ========================================
echo.

REM Check if main project build directory exists
if not exist "%BUILD_DIR%" (
    echo Build directory not found: %BUILD_DIR%
    echo Setting up main project...
    echo.

    REM Check if configure script exists
    if not exist "%PROJECT_ROOT%\configure.bat" (
        echo Error: configure.bat not found at %PROJECT_ROOT%\configure.bat
        echo Please ensure you are in the correct project directory.
        exit /b 1
    )

    REM Run configure script
    echo Running configure.bat...
    cd "%PROJECT_ROOT%"
    call configure.bat
    if %ERRORLEVEL% neq 0 (
        echo Error: configure.bat failed
        exit /b 1
    )
    echo.

    REM Check if build directory was created
    if not exist "%BUILD_DIR%" (
        echo Error: configure script did not create build directory
        exit /b 1
    )

    echo [OK] Project configured successfully
    echo.
)

REM Check if required libraries exist
set LIBS_EXIST=1
set REQUIRED_LIBS=libbwpeer.dll libbwlogger.dll libbwthreadname.dll libbwblockcore.dll libbwrest.dll libbwutils.dll libbwpeermsg.dll

echo Checking for required libraries in: %BUILD_DIR%
for %%L in (%REQUIRED_LIBS%) do (
    if not exist "%BUILD_DIR%\%%L" (
        echo   [X] Missing: %%L
        set LIBS_EXIST=0
    ) else (
        echo   [OK] Found: %%L
    )
)
echo.

REM Build main project libraries if they don't exist
if %LIBS_EXIST% equ 0 (
    echo Required libraries not found. Building main project...
    echo.

    REM Navigate to build directory
    cd "%BUILD_DIR%"

    REM Build the required libraries
    if defined PARALLEL_JOBS (
        echo Building libraries with %PARALLEL_JOBS% parallel jobs ^(this may take a few minutes^)...
        cmake --build . --target bwthreadname bwlogger bwpeer bwutils bwblockcore bwrest bwpeermsg -j %PARALLEL_JOBS%
    ) else (
        echo Building libraries ^(this may take a few minutes^)...
        cmake --build . --target bwthreadname bwlogger bwpeer bwutils bwblockcore bwrest bwpeermsg
    )
    if %ERRORLEVEL% neq 0 (
        echo.
        echo Error: Build failed. Please check the error messages above.
        exit /b 1
    )
    echo.

    REM Verify libraries were built successfully
    set LIBS_EXIST=1
    for %%L in (%REQUIRED_LIBS%) do (
        if not exist "%BUILD_DIR%\%%L" (
            echo Error: Failed to build %%L
            set LIBS_EXIST=0
        )
    )

    if !LIBS_EXIST! equ 0 (
        echo.
        echo Error: Failed to build required libraries
        exit /b 1
    )

    echo [OK] All libraries built successfully
    echo.
)

REM Detect Boost installation
echo ========================================
echo Detecting Boost installation...
echo ========================================
set BOOST_FOUND=0
set BOOST_ROOT=

REM Check if BOOST_ROOT environment variable is set
if defined BOOST_ROOT (
    if exist "%BOOST_ROOT%\boost\version.hpp" (
        set BOOST_FOUND=1
        echo Boost found: %BOOST_ROOT% ^(from environment variable^)
    )
)

REM Check common installation paths
if %BOOST_FOUND% equ 0 (
    for %%d in (
        "C:\local\boost_1_89_0"
        "C:\local\boost_1_88_0"
        "C:\local\boost_1_87_0"
        "C:\local\boost_1_86_0"
        "C:\local\boost_1_85_0"
        "C:\local\boost"
        "C:\Boost"
        "C:\Program Files\Boost"
    ) do (
        if exist "%%~d\boost\version.hpp" (
            set BOOST_FOUND=1
            set BOOST_ROOT=%%~d
            echo Boost found: !BOOST_ROOT!
            goto :boost_done
        )
    )
)

:boost_done
if %BOOST_FOUND% equ 0 (
    echo Warning: Boost not found. CMake will try to find it automatically.
    echo If build fails, set BOOST_ROOT environment variable or use:
    echo   set BOOST_ROOT=C:\local\boost_1_89_0
    echo   build.bat
    echo.
)
echo.

REM Detect OpenSSL installation (from parent build directory)
set OPENSSL_ROOT_DIR=
set OPENSSL_SSL_LIBRARY=
set OPENSSL_CRYPTO_LIBRARY=
set OPENSSL_INCLUDE_DIR=

if exist "%BUILD_DIR%\CMakeCache.txt" (
    for /f "tokens=2 delims==" %%i in ('findstr /C:"OPENSSL_ROOT_DIR" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
        set OPENSSL_ROOT_DIR=%%i
    )
    for /f "tokens=2 delims==" %%i in ('findstr /C:"OPENSSL_SSL_LIBRARY" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
        set OPENSSL_SSL_LIBRARY=%%i
    )
    for /f "tokens=2 delims==" %%i in ('findstr /C:"OPENSSL_CRYPTO_LIBRARY" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
        set OPENSSL_CRYPTO_LIBRARY=%%i
    )
    for /f "tokens=2 delims==" %%i in ('findstr /C:"OPENSSL_INCLUDE_DIR:PATH=" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do (
        set OPENSSL_INCLUDE_DIR=%%i
    )
)

if defined OPENSSL_ROOT_DIR (
    echo Using OpenSSL from parent build: %OPENSSL_ROOT_DIR%
    echo.
)

REM Build the unit tests
echo Building unit tests...
echo.

REM Remove old test build directory if clean build requested
if %CLEAN_BUILD% equ 1 (
    if exist "%TEST_BUILD_DIR%" (
        echo Removing existing build directory: %TEST_BUILD_DIR%
        rmdir /s /q "%TEST_BUILD_DIR%"
    )
)

REM Create test build directory if it doesn't exist
if not exist "%TEST_BUILD_DIR%" (
    echo Creating build directory: %TEST_BUILD_DIR%
    mkdir "%TEST_BUILD_DIR%"
)

cd "%TEST_BUILD_DIR%"

REM Prepare CMake arguments
set CMAKE_ARGS=-G "MinGW Makefiles"

if %BOOST_FOUND% equ 1 (
    set CMAKE_ARGS=!CMAKE_ARGS! -DBoost_ROOT="%BOOST_ROOT%"
    set CMAKE_ARGS=!CMAKE_ARGS! -DBoost_NO_SYSTEM_PATHS=ON
)

if defined OPENSSL_ROOT_DIR (
    set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT_DIR%"
)
if defined OPENSSL_SSL_LIBRARY (
    set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_SSL_LIBRARY="%OPENSSL_SSL_LIBRARY%"
)
if defined OPENSSL_CRYPTO_LIBRARY (
    set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_CRYPTO_LIBRARY%"
)
if defined OPENSSL_INCLUDE_DIR (
    set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_INCLUDE_DIR="%OPENSSL_INCLUDE_DIR%"
)

REM Run cmake
echo Running cmake with arguments: %CMAKE_ARGS%
echo.
cmake .. %CMAKE_ARGS%
if %ERRORLEVEL% neq 0 (
    echo Error: CMake configuration failed
    echo.
    echo Troubleshooting:
    echo   1. Check if Boost is installed: dir C:\local
    echo   2. Set BOOST_ROOT if needed: set BOOST_ROOT=C:\local\boost_1_89_0
    echo   3. Run configure.bat in parent directory first
    exit /b 1
)
echo.

REM Build test executable
if defined PARALLEL_JOBS (
    echo Building test executable with %PARALLEL_JOBS% parallel jobs...
    cmake --build . -j %PARALLEL_JOBS%
) else (
    echo Building test executable...
    cmake --build .
)
if %ERRORLEVEL% neq 0 (
    echo Error: Build failed
    exit /b 1
)
echo.

REM Check if test executable was created
if exist "test_all.exe" (
    echo ========================================
    echo [OK] Build successful!
    echo ========================================
    echo.

    REM Copy required DLLs from parent build directory
    echo Copying required DLLs...
    copy "%BUILD_DIR%\libbwpeer.dll" . >nul 2>&1
    copy "%BUILD_DIR%\libbwlogger.dll" . >nul 2>&1
    copy "%BUILD_DIR%\libbwthreadname.dll" . >nul 2>&1
    copy "%BUILD_DIR%\libbwblockcore.dll" . >nul 2>&1
    copy "%BUILD_DIR%\libbwrest.dll" . >nul 2>&1
    copy "%BUILD_DIR%\libbwutils.dll" . >nul 2>&1
    copy "%BUILD_DIR%\libbwpeermsg.dll" . >nul 2>&1

    REM Copy OpenSSL DLLs if they exist in the OpenSSL bin directory
    if defined OPENSSL_ROOT_DIR (
        if exist "%OPENSSL_ROOT_DIR%\bin\libssl-3-x64.dll" (
            copy "%OPENSSL_ROOT_DIR%\bin\libssl-3-x64.dll" . >nul 2>&1
            copy "%OPENSSL_ROOT_DIR%\bin\libcrypto-3-x64.dll" . >nul 2>&1
        )
    )

    echo [OK] DLLs copied successfully
    echo.
    echo Test executable created: %TEST_BUILD_DIR%\test_all.exe
    echo.
    echo Test modules included:
    echo   - test_peer_manager.cpp  ^(Peer manager - 18 tests^)
    echo   - test_peer_message.cpp  ^(Peer message protocol - 21 tests^)
    echo   - test_peer_filter.cpp   ^(Peer filter - 18 tests^)
    echo   - test_request_queue.cpp ^(Request queue - 10 tests^)
    echo   - test_api_server.cpp    ^(API server - 5 tests^)
    echo   - test_block.cpp         ^(Block - 4 tests^)
    echo   - test_transaction.cpp   ^(Transaction - 5 tests^)
    echo   - test_block_weave.cpp   ^(Block weave - 7 tests^)
    echo   - test_wallet.cpp        ^(Wallet - 3 tests^)
    echo   - test_hash.cpp          ^(Hash - 2 tests^)
    echo   - test_block_file.cpp    ^(Block file persistence - 17 tests^)
    echo   - test_logger.cpp        ^(Logger - 26 tests^)
    echo   Total: 136 tests
    echo.
    echo To run all tests:
    echo   cd %TEST_BUILD_DIR%
    echo   test_all.exe
    echo.
    echo Or run directly:
    echo   %TEST_BUILD_DIR%\test_all.exe
    echo.
) else (
    echo ========================================
    echo [X] Build failed
    echo ========================================
    echo.
    echo The test executable was not created.
    exit /b 1
)

exit /b 0
