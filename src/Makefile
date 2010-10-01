#
# Lanes/src/Makefile
#
#   make [LUA=... LUAC=...]                                   Manual build
#   make LUAROCKS=1 CFLAGS=... LIBFLAG=... LUA=... LUAC=...   LuaRocks automated build
#

MODULE=lanes

SRC=lanes.c threading.c tools.c

OBJ=$(SRC:.c=.o)

# LuaRocks gives 'LIBFLAG' from the outside
#
LIBFLAG=-shared

OPT_FLAGS=-O2
    # -O0 -g

LUA=lua
LUAC=luac

_SO=.so
ifeq "$(findstring MINGW32,$(shell uname -s))" "MINGW32"
  _SO=.dll
endif

ifeq "$(LUAROCKS)" ""
  ifeq "$(findstring MINGW32,$(shell uname -s))" "MINGW32"
    # MinGW MSYS on Windows
    #
    # - 'lua' and 'luac' expected to be on the path
    # - %LUA_DEV% must lead to include files and libraries (Lua for Windows >= 5.1.3.14)
    # - %MSCVR80% must be the full pathname of 'msvcr80.dll'
    #
    ifeq "$(LUA_DEV)" ""
      $(error LUA_DEV not defined - try i.e. 'make LUA_DEV=/c/Program\ Files/Lua/5.1')
    endif
    ifeq "$(MSVCR80)" ""
      MSVCR80:=$(LUA_DEV)/install/support/Microsoft.VC80.CRT.SP1/MSVCR80.DLL
      ifneq '$(shell test -f "$(MSVCR80)" && echo found)' 'found'
        $(error MSVCR80 not defined - set it to full path of msvcr80.dll')
      endif
      $(warning MSVCR80=$(MSVCR80))
    endif
    LUA_FLAGS:=-I "$(LUA_DEV)/include"
    LUA_LIBS:="$(LUA_DEV)/lua5.1.dll" -lgcc -lmsvcr80 "$(MSVCR80)"
    LIBFLAG=-shared -Wl,-Map,lanes.map
  else
    # Autodetect LUA_FLAGS and/or LUA_LIBS
    #
    ifneq "$(shell which pkg-config)" ""
      ifeq "$(shell pkg-config --exists lua5.1 && echo 1)" "1"
        LUA_FLAGS:=$(shell pkg-config --cflags lua5.1)
        LUA_LIBS:=$(shell pkg-config --libs lua5.1)
          #
          # Ubuntu: -I/usr/include/lua5.1 
          #         -llua5.1
      else
        ifeq "$(shell pkg-config --exists lua && echo 1)" "1"
          LUA_FLAGS:=$(shell pkg-config --cflags lua)
          LUA_LIBS:=$(shell pkg-config --libs lua)
            #
            # OS X fink with pkg-config:
            #      -I/sw/include 
            #      -L/sw/lib -llua -lm
        else
          $(warning *** 'pkg-config' existed but did not know of 'lua[5.1]' - Good luck!)
          LUA_FLAGS:=
          LUA_LIBS:=-llua
        endif
      endif
    else
      # No 'pkg-config'; try defaults
      #
      ifeq "$(shell uname -s)" "Darwin"
        $(warning *** Assuming 'fink' at default path)
        LUA_FLAGS:=-I/sw/include
        LUA_LIBS:=-L/sw/lib -llua
      else
        $(warning *** Assuming an arbitrary Lua installation; try installing 'pkg-config')
        LUA_FLAGS:=
        LUA_LIBS:=-llua
      endif
    endif
  endif

  ifeq "$(shell uname -s)" "Darwin"
    # Some machines need 'MACOSX_DEPLOYMENT_TARGET=10.3' for using '-undefined dynamic_lookup'
    # (at least PowerPC running 10.4.11); does not harm the others
    #
    CC = MACOSX_DEPLOYMENT_TARGET=10.3 gcc
    LIBFLAG = -bundle -undefined dynamic_lookup
  endif
  
  CFLAGS=-Wall -Werror $(OPT_FLAGS) $(LUA_FLAGS)
  LIBS=$(LUA_LIBS)
endif

#---
# PThread platform specifics
#
ifeq "$(shell uname -s)" "Linux"
  # -D_GNU_SOURCE needed for 'pthread_mutexattr_settype'
  CFLAGS += -D_GNU_SOURCE -fPIC

  # Use of -DUSE_PTHREAD_TIMEDJOIN is possible, but not recommended (slower & keeps threads
  # unreleased somewhat longer)
  #CFLAGS += -DUSE_PTHREAD_TIMEDJOIN

  LIBS += -lpthread
endif

ifeq "$(shell uname -s)" "BSD"
  LIBS += -lpthread
endif

#---
all: lua51-$(MODULE)$(_SO)

%.o: %.c *.h Makefile

# Note: Don't put $(LUA_LIBS) ahead of $^; MSYS will not like that (I think)
#
lua51-$(MODULE)$(_SO): $(OBJ)
	$(CC) $(LIBFLAG) $(LIBS) $^ $(LUA_LIBS) -o $@

clean:
	-rm -rf lua51-$(MODULE)$(_SO) *.lch *.o *.tmp *.map

lanes.o: keeper.lch

# Note: 'luac -o -' could be used on systems other than Windows (where pipes
#       are binary). We need to support MinGW as well, so a temporary file.
#
%.lch: %.lua
	$(LUAC) -o $@.tmp $<
	$(LUA) ../tools/bin2c.lua $@.tmp -o $@
	-rm $@.tmp

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
MINGW_GCC=mingw32-gcc
    # i686-pc-mingw32-gcc

win32: $(WIN32_LUA51)/include/lua.h
	$(MAKE) build CC=$(MINGW_GCC) \
            LUA_FLAGS=-I$(WIN32_LUA51)/include \
            LUA_LIBS="-L$(WIN32_LUA51) -llua51" \
            _SO=.dll \
            SO_FLAGS=-shared \
            LUA=lua51 \
            LUAC=luac51

$(WIN32_LUA51)/include/lua.h:
	@echo "Usage: make win32 WIN32_LUA51=<path of extracted LuaBinaries dll8 and dev packages>"
	@echo "                  [MINGW_GCC=...mingw32-gcc]"
	@false

.PROXY:	all clean nslu2 win32

