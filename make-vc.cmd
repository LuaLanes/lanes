@REM
@REM make-vc.cmd to build Lanes on Visual C++ 2005/08
@REM
@REM Requires:  Windows XP or later (cmd.exe)
@REM            Visual C++ 2005/2008 (Express)
@REM            LuaBinaries 5.1.3 or Lua for Windows 5.1.3
@REM

@setlocal
@set LUA_PATH=src\?.lua;tests\?.lua

@if not "%LUA51%"=="" (
  @goto LUA_OK
)

@REM *** Lua for Windows >=5.1.3.14 (%LUA_DEV%) ***
@REM
@if exist "%LUA_DEV%\lua.exe" (
  @set LUA51=%LUA_DEV%
  @goto LUA_OK
)

@REM *** Lua for Windows (default path) ***
@REM
@if exist "%ProgramFiles%\Lua\5.1\lua.exe" (
  @set LUA51=%ProgramFiles:~0,2%\Progra~1\Lua\5.1
  @goto LUA_OK
)

@REM *** LuaBinaries (default path) ***
@REM
@if exist "%ProgramFiles%\Lua5.1\lua5.1.exe" (
  @set LUA51=%ProgramFiles:~0,2%\Progra~1\Lua5.1
  @goto LUA_OK
)

goto ERR_NOLUA
:LUA_OK

@REM ---
@REM %LUA_EXE% = %LUA51%\lua[5.1].exe
@REM %LUAC_EXE% = %LUA51%\luac[5.1].exe
@REM %LUA_LIB% = %LUA51%[\lib]
@REM ---

@set LUA_EXE=%LUA51%\lua5.1.exe
@if exist "%LUA_EXE%" goto LUA_EXE_OK
@set LUA_EXE=%LUA51%\lua.exe
@if exist "%LUA_EXE%" goto LUA_EXE_OK
@echo "Cannot find %LUA51%\lua[5.1].exe
@goto EXIT
:LUA_EXE_OK

@set LUAC_EXE=%LUA51%\luac5.1.exe
@if exist "%LUAC_EXE%" goto LUAC_EXE_OK
@set LUAC_EXE=%LUA51%\luac.exe
@if exist "%LUAC_EXE%" goto LUAC_EXE_OK
@echo "Cannot find %LUA51%\luac[5.1].exe
@goto EXIT
:LUAC_EXE_OK


@if "%1"=="" goto BUILD
@if "%1"=="clean" goto CLEAN
@if "%1"=="test" goto TEST
@if "%1"=="launchtest" goto LAUNCHTEST
@if "%1"=="perftest" goto PERFTEST
@if "%1"=="perftest-plain" goto PERFTEST-PLAIN
@if "%1"=="stress" goto STRESS
@if "%1"=="basic" goto BASIC
@if "%1"=="fifo" goto FIFO
@if "%1"=="keeper" goto KEEPER
@if "%1"=="atomic" goto ATOMIC
@if "%1"=="cyclic" goto CYCLIC
@if "%1"=="timer" goto TIMER
@if "%1"=="recursive" goto RECURSIVE
@if "%1"=="fibonacci" goto FIBONACCI
@if "%1"=="hangtest" goto HANGTEST
@if "%1"=="require" goto REQUIRE

@echo Unknown target: %1
@echo.
@goto EXIT

:BUILD
@REM LuaBinaries: 
@REM 	The current build system does not show 'lanes/core.dll' to
@REM 	be dependent on more than 'KERNEL32.DLL'. Good.
@REM
@REM Lua for Windows:
@REM    Depends on KERNEL32.DLL and LUA5.1.DLL. Good?

@set LUA_LIB=%LUA51%
@if exist "%LUA_LIB%\lua5.1.lib" (
  @echo.
  @echo ***
  @echo *** Using Lua from: %LUA51%
  @echo ***
  @echo.
  @goto LUA_LIB_OK
)

@set LUA_LIB=%LUA51%\lib
@if exist "%LUA_LIB%\lua5.1.lib" (
  @echo.
  @echo ***
  @echo *** Using Lua from: %LUA51%
  @echo ***
  @echo.
  @goto LUA_LIB_OK
)
@echo Cannot find %LUA51%\[lib\]lua5.1.lib
@goto EXIT
:LUA_LIB_OK

@if "%VCINSTALLDIR%"=="" goto ERR_NOVC

@REM
@REM Win32 (Visual C++ 2005/08 Express) build commands
@REM
@REM MS itself has warnings in stdlib.h (4255), winbase.h (4668), several (4820, 4826)
@REM 4054: "type cast from function pointer to data pointer"
@REM 4127: "conditional expression is constant"
@REM 4711: ".. selected for automatic inline expansion"
@REM
@set WARN=/Wall /wd4054 /wd4127 /wd4255 /wd4668 /wd4711 /wd4820 /wd4826

@REM /LDd: debug DLL
@REM /O2 /LD: release DLL
@REM
@set FLAGS=/O2 /LD

cl %WARN% %FLAGS% /I "%LUA51%\include" /Felanes\core.dll src\*.c "%LUA_LIB%\lua5.1.lib"
@REM cl %WARN% %FLAGS% /I "%LUA51%\include" /Felanes\core.dll src\*.c "%LUA_LIB%\lua5.1.lib" /link /NODEFAULTLIB:libcmt

@del lanes\core.lib
@del lanes\core.exp
@goto EXIT

:CLEAN
if exist lanes\*.dll del lanes\*.dll
if exist delme del delme
@goto EXIT

:TEST
@REM "make test" does not automatically build/update the dll. We're NOT a makefile. :!
@REM
"%LUA_EXE%" tests\basic.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\fifo.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\keeper.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\fibonacci.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\timer.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\atomic.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\cyclic.lua
@IF errorlevel 1 goto EXIT

"%LUA_EXE%" tests\recursive.lua
@IF errorlevel 1 goto EXIT

@goto EXIT

:BASIC
"%LUA_EXE%" tests\basic.lua
@goto EXIT

:FIFO
"%LUA_EXE%" tests\fifo.lua
@goto EXIT

:KEEPER
"%LUA_EXE%" tests\keeper.lua
@goto EXIT

:ATOMIC
"%LUA_EXE%" tests\atomic.lua
@goto EXIT

:CYCLIC
"%LUA_EXE%" tests\cyclic.lua
@goto EXIT

:TIMER
"%LUA_EXE%" tests\timer.lua
@goto EXIT

:RECURSIVE
"%LUA_EXE%" tests\recursive.lua
@goto EXIT

:FIBONACCI
"%LUA_EXE%" tests\fibonacci.lua
@goto EXIT

:HANGTEST
"%LUA_EXE%" tests\hangtest.lua
@goto EXIT

:REQUIRE
"%LUA_EXE%" -e "require'lanes'"
@goto EXIT

REM ---
REM NOTE: 'timeit' is a funny thing; it does _not_ work with quoted
REM long paths, but it _does_ work without the quotes. I have no idea,
REM how it knows the spaces in paths apart from spaces in between
REM parameters.

:LAUNCHTEST
timeit %LUA_EXE% tests\launchtest.lua %2 %3 %4
@goto EXIT

:PERFTEST
timeit %LUA_EXE% tests\perftest.lua %2 %3 %4
@goto EXIT

:PERFTEST-PLAIN
timeit %LUA_EXE% tests\perftest.lua --plain %2 %3 %4
@goto EXIT

:STRESS
"%LUA_EXE%" tests\test.lua
"%LUA_EXE%" tests\perftest.lua 100
"%LUA_EXE%" tests\perftest.lua 50 -prio=-1,0
"%LUA_EXE%" tests\perftest.lua 50 -prio=0,-1
"%LUA_EXE%" tests\perftest.lua 50 -prio=0,2
"%LUA_EXE%" tests\perftest.lua 50 -prio=2,0

@echo All seems okay!
@goto EXIT

REM ---
:ERR_NOLUA
@echo ***
@echo *** Please set LUA51 to point to either LuaBinaries or
@echo *** Lua for Windows directory.
@echo ***
@echo *** http://luabinaries.luaforge.net/download.html
@echo ***	lua5_1_2_Win32_dll8_lib
@echo ***	lua5_1_2_Win32_bin
@echo ***
@echo *** http://luaforge.net/frs/?group_id=377&release_id=1138
@echo ***
@echo.
@goto EXIT

:ERR_NOVC
@echo ***
@echo *** VCINSTALLDIR not defined; please run 'setup-vc'
@echo ***
@echo.
@goto EXIT

:EXIT
