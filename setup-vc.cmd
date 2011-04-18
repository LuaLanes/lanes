@echo off
REM
REM Setting up command line to use Visual C++ 2005/2008 Express
REM
REM Visual C++ 2005:
REM 	VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio 8\VC
REM 	VS80COMNTOOLS=C:\Program Files\Microsoft Visual Studio 8\Common7\Tools\
REM 	VSINSTALLDIR=C:\Program Files\Microsoft Visual Studio 8
REM
REM Visual C++ 2008:
REM 	VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio 9.0\VC
REM 	VS90COMNTOOLS=C:\Program Files\Microsoft Visual Studio 9.0\Common7\Tools\
REM 	VSINSTALLDIR=C:\Program Files\Microsoft Visual Studio 9.0
REM

REM Test for VC++2005 FIRST, because it is the norm with Lua 5.1.4
REM LuaBinaries and LfW. All prebuilt modules and lua.exe are built
REM with it.
REM
set VSINSTALLDIR=%ProgramFiles%\Microsoft Visual Studio 8
if not exist "%VSINSTALLDIR%\VC\vcvarsall.bat" goto TRY_VC9

REM Win32 headers must be separately downloaded for VC++2005
REM (VC++2008 SP1 carries an SDK with it)
REM
set _SDK=%ProgramFiles%\Microsoft Platform SDK for Windows Server 2003 R2\SetEnv.cmd
if not exist "%_SDK%" goto ERR_NOSDK
call "%_SDK%"
goto FOUND_VC

:TRY_VC9
set VSINSTALLDIR=%ProgramFiles%\Microsoft Visual Studio 9.0
if exist "%VSINSTALLDIR%\VC\vcvarsall.bat" goto WARN_VC
set VSINSTALLDIR=%ProgramFiles(x86)%\Microsoft Visual Studio 9.0
if exist "%VSINSTALLDIR%\VC\vcvarsall.bat" goto WARN_VC

:TRY_VC10
set VSINSTALLDIR=%ProgramFiles%\Microsoft Visual Studio 10.0
if exist "%VSINSTALLDIR%\VC\vcvarsall.bat" goto WARN_VC
set VSINSTALLDIR=%ProgramFiles(x86)%\Microsoft Visual Studio 10.0
if exist "%VSINSTALLDIR%\VC\vcvarsall.bat" goto WARN_VC

:WARN_VC
echo.
echo *** Warning: Visual C++ 2008/2010 in use ***
echo.
echo Using VC++2005 is recommended for runtime compatibility issues
echo (LuaBinaries and LfW use it; if you compile everything from
echo scratch, ignore this message)
echo.

:FOUND_VC
set VCINSTALLDIR=%VSINSTALLDIR%\vc

REM vcvars.bat sets the following values right:
REM
REM PATH=...
REM INCLUDE=%VCINSTALLDIR%\ATLMFC\INCLUDE;%VCINSTALLDIR%\INCLUDE;%VCINSTALLDIR%\PlatformSDK\include;%FrameworkSDKDir%\include;%INCLUDE%
REM LIB=%VCINSTALLDIR%\ATLMFC\LIB;%VCINSTALLDIR%\LIB;%VCINSTALLDIR%\PlatformSDK\lib;%FrameworkSDKDir%\lib;%LIB%
REM LIBPATH=%FrameworkDir%\%FrameworkVersion%;%VCINSTALLDIR%\ATLMFC\LIB
REM
call "%VSINSTALLDIR%\VC\vcvarsall.bat"

REM 'timeit.exe' is part of the MS Server Res Kit Tools (needed for "make perftest")
REM
set _RESKIT=%ProgramFiles%\Windows Resource Kits\Tools\
if not exist "%_RESKIT%\timeit.exe" goto WARN_NOTIMEIT
PATH=%PATH%;%_RESKIT%
goto EXIT

:WARN_NOTIMEIT
echo.
echo ** WARNING: Windows Server 2003 Resource Kit Tools - not detected
echo             You will need the 'timeit' utility to run 'make perftest'
echo             http://www.microsoft.com/downloads/details.aspx?familyid=9D467A69-57FF-4AE7-96EE-B18C4790CFFD
echo.
goto EXIT

REM ---
:ERR_NOVC
echo.
echo ** ERROR: Visual C++ 2005/08/10 Express - not detected
echo           You can set the environment variables separately, and run 'make-vc.cmd'
echo           or download the compiler from:
echo           http://msdn.microsoft.com/vstudio/express/downloads/
echo.
goto EXIT

:ERR_NOSDK
echo.
echo ** ERROR: Windows Server 2003 Platform SDK - not detected
echo           You will need the core API's of it to compile Win32 applications.
echo           http://www.microsoft.com/downloads/details.aspx?familyid=0BAF2B35-C656-4969-ACE8-E4C0C0716ADB
echo.
goto EXIT

:EXIT
set _SDK=
set _RESKIT=
