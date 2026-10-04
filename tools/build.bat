@echo off
setlocal EnableDelayedExpansion
:: ============================================================
:: TaskManagerPlusPlus - portable build script
::
:: Usage:
::   build.bat [Debug|Release]         build (default: Debug)
::   build.bat [Debug|Release] deps    install vcpkg dependencies
::   build.bat [Debug|Release] test    build, then run the tests
::
:: Unlike the developer scripts that live beside this checkout, this
:: one makes no assumption about where Visual Studio or vcpkg are
:: installed: both are discovered, and a useful message is printed
:: when either is missing. That is what makes it usable by someone
:: who has just cloned the repository.
::
:: The developer environment is established through VsDevCmd.bat
:: rather than vcvars64.bat. That matters for vcpkg: its compiler
:: probe uses the Ninja generator and reads INCLUDE / LIB /
:: WindowsSdkDir from the environment rather than querying a
:: registered Visual Studio instance, so vcpkg works even on a
:: machine where vswhere reports no instances.
:: ============================================================

set "ROOT=%~dp0.."
set "SLN=%ROOT%\TaskManagerPlusPlus.sln"

set "CFG=%~1"
if "%CFG%"=="" set "CFG=Debug"

set "ACTION=%~2"

:: --- Locate Visual Studio ------------------------------------
:: vswhere is shipped with every VS 2017 and later installer and is
:: the supported way to find an installation. It is tried first; a
:: short list of conventional paths is the fallback for a machine
:: where it is missing.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSBASE="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSBASE=%%i"
)

if not defined VSBASE (
    for %%p in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
        "D:\Program Files\Microsoft Visual Studio\18\Community"
    ) do (
        if not defined VSBASE if exist "%%~p\Common7\Tools\VsDevCmd.bat" set "VSBASE=%%~p"
    )
)

if not defined VSBASE (
    echo ERROR: Visual Studio with the C++ toolset was not found.
    echo.
    echo Install the "Desktop development with C++" workload, or set VSBASE
    echo to the installation directory before running this script:
    echo     set VSBASE^=C:\Path\To\Visual Studio\2022\Community
    exit /b 1
)

set "VSDEVCMD=%VSBASE%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
    echo ERROR: VsDevCmd.bat not found under "%VSBASE%".
    exit /b 1
)

echo === ENVIRONMENT ===
echo   Visual Studio : %VSBASE%

call "%VSDEVCMD%" -arch=amd64 -host_arch=amd64 -no_logo >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to establish the Visual Studio developer environment.
    exit /b 1
)

:: --- Locate vcpkg --------------------------------------------
:: VCPKG_ROOT is honoured when it is already set, so a user with a
:: vcpkg checkout can point at it. Otherwise the manifest is restored
:: with the copy NuGet fetches for the build, which needs no separate
:: installation.
if defined VCPKG_ROOT (
    echo   vcpkg root    : %VCPKG_ROOT%
) else (
    echo   vcpkg root    : not set ^(the build restores from NuGet^)
)

:: --- Locate MSBuild ------------------------------------------
set "MSB=%VSBASE%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSB%" set "MSB=%VSBASE%\MSBuild\15.0\Bin\MSBuild.exe"

if not exist "%MSB%" (
    echo ERROR: MSBuild not found under "%VSBASE%".
    exit /b 1
)

echo   MSBuild       : %MSB%
echo   Configuration : %CFG%
echo.

:: --- Optional: install vcpkg dependencies --------------------
if /i "%ACTION%"=="deps" (
    if not defined VCPKG_ROOT (
        echo ERROR: VCPKG_ROOT is not set, so dependencies cannot be installed.
        echo Set it to your vcpkg checkout:
        echo     set VCPKG_ROOT^=C:\src\vcpkg
        exit /b 1
    )

    echo === INSTALL VCPKG DEPENDENCIES ===
    pushd "%ROOT%"
    "%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows --x-manifest-root=. --vcpkg-root="%VCPKG_ROOT%"
    set "RC=!ERRORLEVEL!"
    popd
    if not "!RC!"=="0" ( echo VCPKG_FAILED & exit /b 1 )
    echo === VCPKG_OK ===
    exit /b 0
)

:: --- Restore and build ---------------------------------------
:: The vcpkg dependencies are installed into vcpkg_installed, which is
:: not committed: it is a build artefact of the manifest. A fresh clone
:: therefore has none, and the projects are configured not to install it
:: automatically (VcpkgManifestInstall is false, so that a build never
:: silently spends minutes fetching packages). It is installed here
:: instead, once, and the condition makes a later build skip it.
if not exist "%ROOT%\vcpkg_installed\x64-windows" (
    if defined VCPKG_ROOT (
        echo === INSTALLING VCPKG DEPENDENCIES ^(first build^) ===
        pushd "%ROOT%"
        "%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows --x-manifest-root=. --vcpkg-root="%VCPKG_ROOT%"
        set "RC=!ERRORLEVEL!"
        popd
        if not "!RC!"=="0" (
            echo VCPKG_FAILED
            echo Run "build.bat %CFG% deps" to see the full error.
            exit /b 1
        )
        echo === VCPKG_OK ===
    ) else (
        echo ERROR: vcpkg dependencies are not installed and VCPKG_ROOT is not set.
        echo.
        echo Either set VCPKG_ROOT to a vcpkg checkout and re-run this script:
        echo     set VCPKG_ROOT^=C:\src\vcpkg
        echo or install the dependencies once by hand:
        echo     vcpkg install --triplet x64-windows --x-manifest-root="%ROOT%"
        exit /b 1
    )
)

echo === RESTORE (%CFG%) ===
"%MSB%" "%SLN%" /t:Restore /p:Configuration=%CFG% /p:Platform=x64 /v:minimal /nologo
if errorlevel 1 ( echo RESTORE_FAILED & exit /b 1 )

echo === BUILD (%CFG%) ===
"%MSB%" "%SLN%" /p:Configuration=%CFG% /p:Platform=x64 /v:minimal /nologo /m
if errorlevel 1 ( echo BUILD_FAILED & exit /b 1 )

echo === BUILD_OK ===

:: --- Optional: run the tests ---------------------------------
if /i "%ACTION%"=="test" (
    set "TESTS=%ROOT%\build\x64\%CFG%\tmpp_tests.exe"
    if not exist "!TESTS!" (
        echo ERROR: test binary not found at "!TESTS!".
        exit /b 1
    )

    echo === TESTS ===
    "!TESTS!" --gtest_brief=1
    if errorlevel 1 ( echo TESTS_FAILED & exit /b 1 )
    echo === TESTS_OK ===
)

exit /b 0
