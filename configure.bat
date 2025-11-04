@echo off
REM Configure script for RestDaemon - Windows

setlocal enabledelayedexpansion

REM Default values
set BUILD_DIR=build
set BUILD_TYPE=Release
set INSTALL_PREFIX=C:\Program Files\RestDaemon
set GENERATOR=
if not defined OPENSSL_ROOT_DIR set OPENSSL_ROOT_DIR=
if not defined BOOST_ROOT set BOOST_ROOT=
set SHOW_HELP=0

REM Parse arguments
:parse_args
if "%~1"=="" goto :end_parse_args
set arg=%~1

if "%arg:~0,9%"=="--prefix=" (
    set INSTALL_PREFIX=%arg:~9%
    shift
    goto :parse_args
)
if "%arg:~0,12%"=="--build-dir=" (
    set BUILD_DIR=%arg:~12%
    shift
    goto :parse_args
)
if "%arg%"=="--debug" (
    set BUILD_TYPE=Debug
    shift
    goto :parse_args
)
if "%arg%"=="--release" (
    set BUILD_TYPE=Release
    shift
    goto :parse_args
)
if "%arg:~0,12%"=="--generator=" (
    set GENERATOR=%arg:~12%
    shift
    goto :parse_args
)
if "%arg:~0,16%"=="--openssl-root=" (
    set OPENSSL_ROOT_DIR=%arg:~16%
    shift
    goto :parse_args
)
if "%arg:~0,13%"=="--boost-root=" (
    set BOOST_ROOT=%arg:~13%
    shift
    goto :parse_args
)
if "%arg%"=="--help" (
    set SHOW_HELP=1
    shift
    goto :parse_args
)

echo Unknown option: %arg%
echo Use --help for usage information
exit /b 1

:end_parse_args

if "%SHOW_HELP%"=="1" (
    call :usage
    exit /b 0
)

REM Detect platform
echo.
echo ========================================
echo Detecting platform...
echo ========================================
echo Platform: Windows
echo.

REM Check for CMake
echo ========================================
echo Checking for CMake...
echo ========================================
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake 3.10 or later.
    echo Download from: https://cmake.org/download/
    exit /b 1
)
for /f "tokens=3" %%i in ('cmake --version ^| findstr /C:"cmake version"') do set CMAKE_VERSION=%%i
echo CMake version: %CMAKE_VERSION%
echo.

REM Check for compiler
echo ========================================
echo Checking for C++ compiler...
echo ========================================
set COMPILER_FOUND=0

REM Check for MSVC (Visual Studio)
where cl.exe >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set COMPILER_FOUND=1
    set CXX_COMPILER=MSVC
    for /f "tokens=*" %%i in ('cl.exe 2^>^&1 ^| findstr /C:"Version"') do set CXX_VERSION=%%i
    echo C++ Compiler: !CXX_VERSION!
) else (
    REM Check for MinGW
    where g++.exe >nul 2>nul
    if !ERRORLEVEL! equ 0 (
        set COMPILER_FOUND=1
        set CXX_COMPILER=MinGW
        for /f "tokens=*" %%i in ('g++ --version ^| findstr /R "^g++"') do set CXX_VERSION=%%i
        echo C++ Compiler: !CXX_VERSION!
    ) else (
        REM Check for Clang
        where clang++.exe >nul 2>nul
        if !ERRORLEVEL! equ 0 (
            set COMPILER_FOUND=1
            set CXX_COMPILER=Clang
            for /f "tokens=*" %%i in ('clang++ --version ^| findstr /R "clang"') do set CXX_VERSION=%%i
            echo C++ Compiler: !CXX_VERSION!
        )
    )
)

if %COMPILER_FOUND% equ 0 (
    echo ERROR: No C++ compiler found.
    echo Please install Visual Studio, MinGW, or Clang.
    exit /b 1
)
echo.

REM Check for OpenSSL
echo ========================================
echo Checking for OpenSSL...
echo ========================================
set OPENSSL_FOUND=0

REM If user specified OpenSSL root, use it
if not "%OPENSSL_ROOT_DIR%"=="" (
    if exist "%OPENSSL_ROOT_DIR%\include\openssl\ssl.h" (
        set OPENSSL_FOUND=1
        echo OpenSSL found: %OPENSSL_ROOT_DIR% ^(user-specified^)
    ) else (
        echo WARNING: Specified OpenSSL root not found: %OPENSSL_ROOT_DIR%
    )
)

REM Check vcpkg
if %OPENSSL_FOUND% equ 0 (
    if defined VCPKG_ROOT (
        if exist "%VCPKG_ROOT%\installed\x64-windows\include\openssl\ssl.h" (
            set OPENSSL_FOUND=1
            set OPENSSL_ROOT_DIR=%VCPKG_ROOT%\installed\x64-windows
            echo OpenSSL found: !OPENSSL_ROOT_DIR! ^(vcpkg^)
        )
    )
)

REM Check common installation paths
if %OPENSSL_FOUND% equ 0 (
    for %%d in (
        "C:\OpenSSL-Win64"
        "C:\Program Files\OpenSSL-Win64"
        "C:\Program Files\OpenSSL"
        "C:\OpenSSL"
    ) do (
        if exist "%%~d\include\openssl\ssl.h" (
            set OPENSSL_FOUND=1
            set OPENSSL_ROOT_DIR=%%~d
            echo OpenSSL found: !OPENSSL_ROOT_DIR!
            goto :openssl_done
        )
    )
)

:openssl_done
if %OPENSSL_FOUND% equ 0 (
    echo WARNING: OpenSSL not found. Install with:
    echo   - vcpkg: vcpkg install openssl:x64-windows
    echo   - Or download from: https://slproweb.com/products/Win32OpenSSL.html
    echo   - Or use: --openssl-root=PATH to specify location
    echo.
)
echo.

REM Check for Boost
echo ========================================
echo Checking for Boost...
echo ========================================
set BOOST_FOUND=0

REM If user specified Boost root, use it
if not "%BOOST_ROOT%"=="" (
    if exist "%BOOST_ROOT%\boost\version.hpp" (
        set BOOST_FOUND=1
        echo Boost found: %BOOST_ROOT% ^(user-specified^)
    ) else (
        echo WARNING: Specified Boost root not found: %BOOST_ROOT%
    )
)

REM Check common installation paths
if %BOOST_FOUND% equ 0 (
    for %%d in (
        "C:\local\boost_1_89_0"
        "C:\local\boost_1_88_0"
        "C:\local\boost_1_87_0"
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
    echo WARNING: Boost not found. Install with:
    echo   - Download from: https://www.boost.org/users/download/
    echo   - Or use: --boost-root=PATH to specify location
    echo.
)
echo.

REM Auto-detect generator if not specified
if "%GENERATOR%"=="" (
    where ninja.exe >nul 2>nul
    if !ERRORLEVEL! equ 0 (
        set GENERATOR=Ninja
        echo Using Ninja build system
    ) else (
        if "%CXX_COMPILER%"=="MSVC" (
            REM Detect Visual Studio version
            if defined VS170COMNTOOLS (
                set GENERATOR=Visual Studio 17 2022
            ) else if defined VS160COMNTOOLS (
                set GENERATOR=Visual Studio 16 2019
            ) else if defined VS150COMNTOOLS (
                set GENERATOR=Visual Studio 15 2017
            ) else if defined VS140COMNTOOLS (
                set GENERATOR=Visual Studio 14 2015
            ) else (
                set GENERATOR=Visual Studio 17 2022
            )
            echo Using !GENERATOR! build system
        ) else (
            set GENERATOR=MinGW Makefiles
            echo Using MinGW Makefiles build system
        )
    )
)
echo.

REM Create build directory
echo ========================================
echo Creating build directory: %BUILD_DIR%
echo ========================================
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
echo.

REM Run CMake
echo ========================================
echo Running CMake configuration...
echo ========================================
cd "%BUILD_DIR%"

set CMAKE_ARGS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE%
set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%"
set CMAKE_ARGS=%CMAKE_ARGS% -G "%GENERATOR%"

if %OPENSSL_FOUND% equ 1 (
    set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT_DIR%"
    REM Use appropriate libraries based on compiler
    if "%CXX_COMPILER%"=="MinGW" (
        REM MinGW: Use .dll.a import libraries
        if exist "%OPENSSL_ROOT_DIR%\lib\mingw\libssl.dll.a" (
            set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_SSL_LIBRARY="%OPENSSL_ROOT_DIR%\lib\mingw\libssl.dll.a"
            set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_ROOT_DIR%\lib\mingw\libcrypto.dll.a"
        )
    ) else (
        REM MSVC: Use .lib files from VC subdirectory
        if exist "%OPENSSL_ROOT_DIR%\lib\VC\x64\MD\libssl.lib" (
            set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_SSL_LIBRARY="%OPENSSL_ROOT_DIR%\lib\VC\x64\MD\libssl.lib"
            set CMAKE_ARGS=!CMAKE_ARGS! -DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_ROOT_DIR%\lib\VC\x64\MD\libcrypto.lib"
        )
    )
)

if %BOOST_FOUND% equ 1 (
    set CMAKE_ARGS=!CMAKE_ARGS! -DBoost_ROOT="%BOOST_ROOT%"
    set CMAKE_ARGS=!CMAKE_ARGS! -DBoost_NO_SYSTEM_PATHS=ON
)

REM Add architecture for Visual Studio generators
if "%GENERATOR:Visual Studio=%"=="%GENERATOR%" (
    REM Not a Visual Studio generator
) else (
    set CMAKE_ARGS=!CMAKE_ARGS! -A x64
)

echo Running: cmake .. %CMAKE_ARGS%
echo.
cmake .. %CMAKE_ARGS%

if %ERRORLEVEL% neq 0 (
    cd ..
    echo.
    echo ERROR: CMake configuration failed!
    exit /b 1
)

cd ..

REM Print summary
echo.
echo ========================================
echo Configuration Summary
echo ========================================
echo Platform:         Windows
echo Build Type:       %BUILD_TYPE%
echo Build Directory:  %BUILD_DIR%
echo Install Prefix:   %INSTALL_PREFIX%
echo Generator:        %GENERATOR%
echo Compiler:         %CXX_COMPILER%
if %OPENSSL_FOUND% equ 1 (
    echo OpenSSL:          %OPENSSL_ROOT_DIR%
) else (
    echo OpenSSL:          Not found
)
if %BOOST_FOUND% equ 1 (
    echo Boost:            %BOOST_ROOT%
) else (
    echo Boost:            Not found
)
echo.
echo Configuration complete!
echo.
echo Next steps:
if "%GENERATOR%"=="Ninja" (
    echo   1. Build:   cd %BUILD_DIR% ^&^& ninja
    echo   2. Test:    %BUILD_DIR%\rest_daemon.exe
    echo   3. Install: cd %BUILD_DIR% ^&^& ninja install
) else if "%GENERATOR:Visual Studio=%"=="%GENERATOR%" (
    echo   1. Build:   cd %BUILD_DIR% ^&^& cmake --build . --config %BUILD_TYPE%
    echo   2. Test:    %BUILD_DIR%\%BUILD_TYPE%\rest_daemon.exe
    echo   3. Install: cd %BUILD_DIR% ^&^& cmake --build . --config %BUILD_TYPE% --target install
) else (
    echo   1. Build:   cd %BUILD_DIR% ^&^& cmake --build .
    echo   2. Test:    %BUILD_DIR%\rest_daemon.exe
    echo   3. Install: cd %BUILD_DIR% ^&^& cmake --build . --target install
)
echo.

exit /b 0

REM Usage function
:usage
echo Usage: configure.bat [OPTIONS]
echo.
echo Options:
echo   --prefix=DIR          Installation prefix (default: C:\Program Files\RestDaemon)
echo   --build-dir=DIR       Build directory (default: build)
echo   --debug               Build with debug symbols
echo   --release             Build with optimizations (default)
echo   --generator=GEN       CMake generator (default: auto-detect)
echo   --openssl-root=DIR    Path to OpenSSL installation
echo   --boost-root=DIR      Path to Boost installation
echo   --help                Show this help message
echo.
echo Examples:
echo   configure.bat --prefix="C:\MyApp"
echo   configure.bat --debug --build-dir=debug_build
echo   configure.bat --generator=Ninja
echo   configure.bat --openssl-root="C:\OpenSSL-Win64"
exit /b 0
