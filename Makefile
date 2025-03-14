#
# Lanes/Makefile
#
#   make
#   make test
#   make basic|fifo|keeper|...
#
#   make perftest[-odd|-even|-plain]
#   make launchtest
#
#   make install DESTDIR=path
#   make tar|tgz VERSION=x.x
#   make clean
#

MODULE = lanes

N = 1000

TIME = time

ifeq "$(findstring MINGW,$(shell uname -s))" "MINGW"
  # MinGW MSYS on Windows
  #
  _SO := dll
  _LUAEXT := .exe
  TIME := timeit.exe
else
  _SO := so
  _LUAEXT :=
endif

# Autodetect LUA
#
LUA := $(word 1,$(shell which lua5.1$(_LUAEXT) 2>/dev/null) $(shell which lua51$(_LUAEXT) 2>/dev/null) $(shell which lua$(_LUAEXT) 2>/dev/null) $(shell which luajit$(_LUAEXT) 2>/dev/null))
LUA_VERSION := $(shell $(LUA) -e "print(string.sub(_VERSION,5,7))")

$(info LUA: $(LUA))
$(info LUA_VERSION: $(LUA_VERSION))

_LANES_TARGET := src/lanes_core.$(_SO)
$(info _LANES_TARGET: $(_LANES_TARGET))

_UNITTEST_TARGET := unit_tests/UnitTests$(_LUAEXT)
$(info _UNITTEST_TARGET: $(_UNITTEST_TARGET))

_DUE_TARGET := deep_userdata_example/deep_userdata_example.$(_SO)
$(info _DUE_TARGET: $(_DUE_TARGET))

# setup LUA_PATH and LUA_CPATH so that requiring lanes and deep_userdata_example work without having to install them
_PREFIX := LUA_CPATH="./src/?.$(_SO);./deep_userdata_example/?.$(_SO)" LUA_PATH="./src/?.lua;./tests/?.lua"

.PHONY: all build_lanes build_unit_tests build_DUE

# only build lanes itself by default
all: build_lanes

#---

build_lanes:
	@echo =========================================================================================
	cd src && $(MAKE) -f Lanes.makefile LUA=$(LUA)
	@echo ==================== $(_LANES_TARGET): DONE!
	@echo

build_unit_tests:
	@echo =========================================================================================
	cd unit_tests && $(MAKE) -f UnitTests.makefile 
	@echo ==================== $(_UNITTEST_TARGET): DONE!
	@echo

build_DUE:
	@echo =========================================================================================
	cd deep_userdata_example && $(MAKE) -f DUE.makefile
	@echo ==================== $(_DUE_TARGET): DONE!
	@echo

# build the unit_tests and the side deep_userdata_example module
# also run a test that shows whether lanes is successfully loaded or not
run_unit_tests: build_lanes build_unit_tests build_DUE
	@echo =========================================================================================
	$(_PREFIX) $(_UNITTEST_TARGET) "lanes.require 'lanes'"

clean:
	cd src && $(MAKE) -f Lanes.makefile clean
	cd unit_tests && $(MAKE) -f UnitTests.makefile clean
	cd deep_userdata_example && $(MAKE) -f DUE.makefile clean

debug:
	$(MAKE) clean
	cd src && $(MAKE) -f Lanes.makefile LUA=$(LUA) OPT_FLAGS="-O0 -g"
	@echo ""
	@echo "** Now, try 'make repetitive' or something and if it crashes, 'gdb $(LUA) ...core file...'"
	@echo "   Then 'bt' for a backtrace."
	@echo ""
	@echo "   You have enabled core, no?   'ulimit -c unlimited'"
	@echo "   On OS X, core files are under '/cores/'"
	@echo ""

gdb:
	@echo "echo *** To start debugging: 'run tests/basic.lua' ***\n\n" > .gdb.cmd
	$(_PREFIX) gdb -x .gdb.cmd $(LUA)

#--- LuaRocks automated build ---
#
rock:
	cd src && $(MAKE) -f Lanes.makefile LUAROCKS=1 CFLAGS="$(CFLAGS)" LIBFLAG="$(LIBFLAG)" LUA=$(LUA)


#--- Testing ---
#
test:
	$(MAKE) appendud
	$(MAKE) atexit
	$(MAKE) atomic
	$(MAKE) basic
	$(MAKE) cancel
	$(MAKE) cyclic
	$(MAKE) deadlock
	$(MAKE) errhangtest
	$(MAKE) error
	$(MAKE) fibonacci
	$(MAKE) fifo
	$(MAKE) finalizer
	$(MAKE) func_is_string
	$(MAKE) irayo_closure
	$(MAKE) irayo_recursive
	$(MAKE) keeper
	$(MAKE) linda_perf
	$(MAKE) manual_register
	$(MAKE) nameof
	$(MAKE) objects
	$(MAKE) package
	$(MAKE) pingpong
	$(MAKE) recursive
	$(MAKE) require
	$(MAKE) rupval
	$(MAKE) timer
	$(MAKE) track_lanes

appendud: tests/appendud.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

atexit: tests/atexit.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

atomic: tests/atomic.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

basic: tests/basic.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

cancel: tests/cancel.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

cyclic: tests/cyclic.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

deadlock: tests/deadlock.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

ehynes: tests/ehynes.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

errhangtest: tests/errhangtest.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

error: tests/error.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

fibonacci: tests/fibonacci.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

fifo: tests/fifo.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

finalizer: tests/finalizer.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

func_is_string: tests/func_is_string.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

hangtest: tests/hangtest.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

irayo_closure: tests/irayo_closure.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

irayo_recursive: tests/irayo_recursive.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

keeper: tests/keeper.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

launchtest: tests/launchtest.lua $(_LANES_TARGET)
	$(MAKE) _perftest ARGS="$< $(N)"

linda_perf: tests/linda_perf.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

manual_register: tests/manual_register.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

nameof: tests/nameof.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

objects: tests/objects.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

package: tests/package.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

perftest: tests/perftest.lua $(_LANES_TARGET)
	$(MAKE) _perftest ARGS="$< $(N)"

perftest-even: tests/perftest.lua $(_LANES_TARGET)
	$(MAKE) _perftest ARGS="$< $(N) -prio=-2"

perftest-odd: tests/perftest.lua $(_LANES_TARGET)
	$(MAKE) _perftest ARGS="$< $(N) -prio=+2"

perftest-plain: tests/perftest.lua $(_LANES_TARGET)
	$(MAKE) _perftest ARGS="$< $(N) -plain"

pingpong: tests/pingpong.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

recursive: tests/recursive.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

require: tests/require.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

rupval: tests/rupval.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

timer: tests/timer.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<

track_lanes: tests/track_lanes.lua $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $<
#
# This tries to show out a bug which happens in lane cleanup (multicore CPU's only)
#
REP_ARGS=-llanes -e "print'say aaa'; for i=1,10 do print(i) end"
repetitive: $(_LANES_TARGET)
	for i in 1 2 3 4 5 6 7 8 9 10 a b c d e f g h i j k l m n o p q r s t u v w x y z; \
		do $(_PREFIX) $(LUA) $(REP_ARGS); \
	done

repetitive1: $(_LANES_TARGET)
	$(_PREFIX) $(LUA) $(REP_ARGS)

#---
#---
_perftest:
	$(_PREFIX) $(TIME) $(LUA) $(ARGS)


#--- Installing ---
#
# This is for LuaRocks automatic install, mainly
#
# LUA_LIBDIR and LUA_SHAREDIR are used by the .rockspec (don't change the names!)
#
DESTDIR:=/usr/local
LUA_LIBDIR:=$(DESTDIR)/lib/lua/$(LUA_VERSION)
LUA_SHAREDIR:=$(DESTDIR)/share/lua/$(LUA_VERSION)

#
# AKa 17-Oct: changed to use 'install -m 644' and 'cp -p'
#
install: $(_LANES_TARGET) src/lanes.lua
	mkdir -p $(LUA_LIBDIR) $(LUA_SHAREDIR)
	install -m 644 $(_LANES_TARGET) $(LUA_LIBDIR)
	cp -p src/lanes.lua $(LUA_SHAREDIR)

uninstall:
	rm $(LUA_LIBDIR)/lanes_core.$(_SO)
	rm $(LUA_SHAREDIR)/lanes.lua
	rm $(LUA_LIBDIR)/deep_userdata_example.$(_SO)

#--- Packaging ---
#
# Make a folder of the same name as tgz, good manners (for the manual
# expander)
#
# "make tgz VERSION=yyyymmdd"
#
VERSION=

tar tgz:
ifeq "$(VERSION)" ""
	echo "Usage: make tar VERSION=x.x"; false
else
	$(MAKE) clean
	-rm -rf $(MODULE)-$(VERSION)
	mkdir $(MODULE)-$(VERSION)
	tar c * --exclude=.svn --exclude=.DS_Store --exclude="_*" \
			--exclude="*.tgz" --exclude="*.rockspec" \
			--exclude=lanes.dev --exclude="$(MODULE)-*" --exclude=xcode \
			--exclude="*.obj" --exclude="*.dll" --exclude=timeit.dat \
	   | (cd $(MODULE)-$(VERSION) && tar x)
	tar czvf $(MODULE)-$(VERSION).tgz $(MODULE)-$(VERSION)
	rm -rf $(MODULE)-$(VERSION)
	md5sum $(MODULE)-$(VERSION).tgz
endif


#--- Undocumented ---
#

# 2.0.1: Running this (instant exit of the main Lua state) occasionally
#        segfaults (1:15 or so on OS X PowerPC G4).
#
require_module: $(_LANES_TARGET)
	$(_PREFIX) $(LUA) -e "require '$(MODULE)'"

run: $(_LANES_TARGET)
	$(_PREFIX) $(LUA) -e "require '$(MODULE)'" -i

echo:
	@echo $(PROGRAMFILES:C=X)

.PHONY: clean debug gdb rock test require install uninstall _nodemo _notest

