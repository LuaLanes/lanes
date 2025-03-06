CC= g++ -std=c++20

# LuaRocks gives 'LIBFLAG' from the outside
#
LIBFLAG=-shared

OPT_FLAGS=-O2
#         -O0 -g

ifeq "$(findstring MINGW,$(shell uname -s))" "MINGW"
  # MinGW MSYS on Windows
  #
  _SO=dll
  _LUAEXT=.exe
  TIME=timeit.exe
else
  _SO=so
  _LUAEXT=
endif

ifeq "$(LUAROCKS)" ""
  ifeq "$(findstring MINGW,$(shell uname -s))" "MINGW"
    # MinGW MSYS on Windows
    #
    # - 'lua' and 'luac' expected to be on the path
    # - %LUA_DEV% must lead to include files and libraries (Lua for Windows >= 5.1.3.14)
    # - %MSCVR80% must be the full pathname of 'msvcr80.dll'
    #
    ifeq "$(LUA_DEV)" ""
      $(warning LUA_DEV not defined - try i.e. 'make LUA_DEV=/c/Program\ Files/Lua/5.1')
      # this assumes Lua was built and installed from source and everything is located in default folders (/usr/local/include and /usr/local/bin)
      LUA_FLAGS:=-I "/usr/local/include"
      LUA_LIBS:=$(word 1,$(shell which lua54.$(_SO)) $(shell which lua53.$(_SO)) $(shell which lua52.$(_SO)) $(shell which lua51$(_SO)))
    else
      LUA_FLAGS:=-I "$(LUA_DEV)/include"
      LUA_LIBS:="$(LUA_DEV)/lua5.1.dll" -lgcc
    endif
    LIBFLAG=-shared -Wl,-Map,lanes.map
  else
    # Autodetect LUA_FLAGS and/or LUA_LIBS
    #
    ifneq "$(shell which pkg-config)" ""
      ifeq "$(shell pkg-config --exists luajit && echo 1)" "1"
        LUA_FLAGS:=$(shell pkg-config --cflags luajit)
        LUA_LIBS:=$(shell pkg-config --libs luajit)
          #
          # Debian: -I/usr/include/luajit-2.0
          #         -lluajit-5.1
      else
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
