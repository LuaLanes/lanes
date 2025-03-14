#
# Lanes/src/Lanes.makefile
#
#   make                                    Manual build
#   make LUAROCKS=1 CFLAGS=... LIBFLAG=...  LuaRocks automated build
#

include ../Shared.makefile

_TARGET := lanes_core.$(_SO)

_SRC := $(wildcard *.cpp)

_OBJ := $(_SRC:.cpp=.o)

#---
all: info $(_TARGET)

info:
	$(info CC: $(CC))
	$(info _SRC: $(_SRC))

_pch.hpp.gch: _pch.hpp
	$(CC) $(CFLAGS) -x c++-header _pch.hpp -o _pch.hpp.gch

%.o: %.cpp _pch.hpp.gch *.h *.hpp Lanes.makefile
	$(CC) $(CFLAGS) -c $< -o $@

# Note: Don't put $(LUA_LIBS) ahead of $^; MSYS will not like that (I think)
#
$(_TARGET): $(_OBJ)
	$(CC) $(LIBFLAG) $^ $(LIBS) $(LUA_LIBS) -o $@

clean:
	-rm -rf $(_TARGET) *.o *.map *.gch

#---
# NSLU2 "slug" Linux ARM
#
nslu2:
	$(MAKE) all CFLAGS="$(CFLAGS) -I/opt/include -L/opt/lib -D_GNU_SOURCE -lpthread"

#---
# Cross compiling to Win32 (MinGW on OS X Intel)
#
# Point WIN32_LUA51 to an extraction of LuaBinaries dll8 and dev packages.
#
# Note: Only works on platforms with same endianess (i.e. not from PowerPC OS X,
#       since 'luac' uses the host endianess)
#
# EXPERIMENTAL; NOT TESTED OF LATE.
#
MINGW_GCC = mingw32-gcc
# i686-pc-mingw32-gcc

win32: $(WIN32_LUA51)/include/lua.h
	$(MAKE) build CC=$(MINGW_GCC) \
		LUA_FLAGS=-I$(WIN32_LUA51)/include \
		LUA_LIBS="-L$(WIN32_LUA51) -llua51" \
		_SO=dll \
		SO_FLAGS=-shared

$(WIN32_LUA51)/include/lua.h:
	@echo "Usage: make win32 WIN32_LUA51=<path of extracted LuaBinaries dll8 and dev packages>"
	@echo "                  [MINGW_GCC=...mingw32-gcc]"
	@false

.PHONY: all info clean nslu2 win32
