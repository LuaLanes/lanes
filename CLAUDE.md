# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Lua Lanes is a portable, message-passing multithreading library for Lua, implemented as a C++20 shared library (`lanes_core.dll/.so`) with a Lua wrapper (`lanes.lua`). It allows running multiple independent Lua states in parallel threads. Supported Lua versions: 5.1, 5.2, 5.3, 5.4, 5.5, and LuaJIT.

## Build Commands

The primary build system is GNU Make (MSYS/MinGW on Windows). There is also a Visual Studio solution (`Lanes.sln`) and a `CMakeLists.txt`.

```bash
# Build lanes_core shared library only (default)
make

# Build C++ unit tests
make build_unit_tests

# Build deep_userdata_example side module
make build_DUE

# Build and run all unit tests
make run_unit_tests

# Debug build (unoptimized)
make debug

# Clean all build artifacts
make clean

# LuaRocks build
luarocks make
```

On Mac OS X, building requires explicit C++20 flags:
```bash
luarocks make CC="env MACOSX_DEPLOYMENT_TARGET=13.3 gcc" LD="env MACOSX_DEPLOYMENT_TARGET=13.3 gcc" CFLAGS="-O2 -fPIC -std=c++20"
```

## Running Tests

### Integration tests (Lua scripts in `tests/`)
```bash
# Run all integration tests
make test

# Run a single test by name (e.g., basic, linda_perf, cancel, timer)
make basic
make linda_perf
make cancel
```

### C++ unit tests (Catch2 in `unit_tests/`)
```bash
# Build and run all unit tests
make run_unit_tests

# List available test cases
unit_tests/UnitTests.exe --list-tests

# Run a specific test case
LUA_CPATH="./src/?.dll;./deep_userdata_example/?.dll" LUA_PATH="./src/?.lua;./tests/?.lua" unit_tests/UnitTests.exe -s scripted_tests.lane.tasking_cancelling

# Debug a specific unit test
make debug_unit_tests
```

Unit test Lua scripts live in `unit_tests/scripts/` organized by category: `lane/`, `linda/`, `misc/`, `coro/`.

## Architecture

### Module Structure

The library consists of two components that must work together:
- **`src/lanes.lua`** — Lua-side wrapper; users `require "lanes"` which in turn `require "lanes_core"`.
- **`src/lanes_core.dll/.so`** — C++20 compiled module; the actual implementation.

### Core C++ Classes

**`Universe`** (`universe.hpp`) — Process-wide singleton, stored as a full userdata in the master Lua state. Holds everything shared across all lanes: keeper states, lane tracker, allocator, configuration, timer Linda, and the selfdestruct chain. Retrieved via `Universe::Get(L_)`.

**`Lane`** (`lane.hpp`) — One instance per Lua thread. Wraps a `std::thread` and two lua_States (`S` = master state, `L` = running state; differ only in coroutine mode). Status progression: `Pending → Running/Waiting/Suspended → Done/Error/Cancelled`.

**`Linda`** (`linda.hpp`) — Inter-lane communication primitive, implemented as deep userdata. Has a keeper state for queued message storage, plus `readHappened`/`writeHappened` condition variables.

**`Keeper`** / **`Keepers`** (`keeper.hpp`) — Dedicated Lua states acting as mailboxes for Linda send/receive operations. Keepers are mutex-protected; `keeper_call()` routes operations into a keeper state.

**`DeepPrelude`** / **`DeepFactory`** (`deep.hpp`) — The "deep userdata" system enabling objects to be shared (not copied) across lanes. External modules can implement `DeepFactory` to expose their own shared userdata types. A deep object stores a `refcount` and a pointer to its factory.

**`InterCopyContext`** (`intercopycontext.hpp`) — Handles cross-state value copying. Manages the lookup table mechanism that allows functions and registered userdata to be looked up by name in the destination state rather than copied as bytecode.

### Key Supporting Files

- `tools.hpp/cpp` — Function lookup tables (`kLookupRegKey`), bytecode serialization, `require` serialization.
- `threading.hpp/cpp` — Platform threading abstraction; `threading_osx.h` for macOS specifics.
- `allocator.hpp/cpp` — Custom allocator support; `ProtectedAllocator` wraps an allocator with a mutex.
- `cancel.hpp/cpp` — Cancellation request types and hook-based cancellation.
- `tracker.hpp/cpp` — Lane lifecycle tracking (for `lanes.track_lanes()`).
- `state.hpp/cpp` — Lua state creation and initialization helpers.
- `compat.hpp/cpp` — Cross-version Lua API compatibility helpers.
- `macros_and_utils.hpp` — Stack-check debug macros (`STACK_CHECK_START_REL`, `STACK_CHECK`).
- `uniquekey.hpp` — `UniqueKey`/`RegistryUniqueKey` types using xxh64 hashes to avoid registry key collisions.

### Data Flow: Send/Receive

When a lane calls `linda:send(key, value)`:
1. The value is inter-copied via `InterCopyContext` into the keeper state.
2. `keeper_call(keepercall_send, ...)` runs inside the keeper's mutex.
3. Waiting receivers are notified via `Linda::writeHappened`.

## Coding Conventions

From `lanesconf.h` (authoritative source):

| Category | Convention |
|---|---|
| Indentation | Spaces only |
| Constants | `k` prefix + uppercase (e.g., `kUniverseLightRegKey`) |
| Function arguments | `_` suffix (e.g., `lua_State* L_`) |
| Local variables | `_` prefix + uppercase (e.g., `_universe`) |
| Static file-level variables | `s` prefix + uppercase |
| Static file-level functions | Uppercase start |
| Class/struct/enum types | Uppercase start |
| Static class members/methods | Uppercase start |
| Regular class members/methods | Lowercase start |
| Named lambda captures | Lowercase start |
| External API | In `lanes` namespace |

Registry keys use xxh64 hashes (generated at https://www.pelock.com/products/hash-calculator) to prevent name collisions across modules.

`DECLARE_UNIQUE_TYPE(Name, BaseType)` creates strong typedefs to prevent accidental implicit conversions (e.g., `KeeperIndex`, `StackIndex`, `DestState`).

`[[nodiscard]]` is used extensively on return values that must be checked.
