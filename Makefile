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

N=1000

_TARGET_DIR=src/lanes
TIME=time

ifeq "$(findstring MINGW32,$(shell uname -s))" "MINGW32"
  # MinGW MSYS on XP
  #
  _SO=dll
  _LUAEXT=.exe
  TIME=timeit.exe
else
  _SO=so
  _LUAEXT=
endif

# Autodetect LUA
#
LUA=$(word 1,$(shell which lua5.1$(_LUAEXT)) $(shell which lua51$(_LUAEXT)) lua$(_LUAEXT))

_TARGET_SO=$(_TARGET_DIR)/core.$(_SO)

_PREFIX=LUA_CPATH="./src/?.$(_SO)" LUA_PATH="./src/?.lua;./tests/?.lua"

#---
all: $(_TARGET_SO)

$(_TARGET_SO): src/*.lua src/*.c src/*.h
	cd src && $(MAKE) LUA=$(LUA)

clean:
	cd src && $(MAKE) clean

debug:
	$(MAKE) clean
	cd src && $(MAKE) LUA=$(LUA) OPT_FLAGS="-O0 -g"
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
	cd src && $(MAKE) LUAROCKS=1 CFLAGS="$(CFLAGS)" LIBFLAG="$(LIBFLAG)" LUA=$(LUA)


#--- Testing ---
#
test:
	$(MAKE) errhangtest
	$(MAKE) irayo_recursive
	$(MAKE) irayo_closure
	$(MAKE) basic
	$(MAKE) fifo
	$(MAKE) keeper
	$(MAKE) timer
	$(MAKE) atomic
	$(MAKE) cyclic
	$(MAKE) objects
	$(MAKE) fibonacci
	$(MAKE) recursive
	$(MAKE) func_is_string
	$(MAKE) atexit
	$(MAKE) linda_perf
	$(MAKE) rupval
	$(MAKE) package
	$(MAKE) pingpong

basic: tests/basic.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

#
# This tries to show out a bug which happens in lane cleanup (multicore CPU's only)
#
REP_ARGS=-llanes -e "print'say aaa'; for i=1,10 do print(i) end"
repetitive: $(_TARGET_SO)
	for i in 1 2 3 4 5 6 7 8 9 10 a b c d e f g h i j k l m n o p q r s t u v w x y z; \
	   do $(_PREFIX) $(LUA) $(REP_ARGS); \
    done

repetitive1: $(_TARGET_SO)
	$(_PREFIX) $(LUA) $(REP_ARGS)

fifo: tests/fifo.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

keeper: tests/keeper.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

fibonacci: tests/fibonacci.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

timer: tests/timer.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

atomic: tests/atomic.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

cyclic: tests/cyclic.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

recursive: tests/recursive.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

hangtest: tests/hangtest.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

ehynes: tests/ehynes.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

#require: tests/require.lua $(_TARGET_SO)
#	$(_PREFIX) $(LUA) $<

objects: tests/objects.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

irayo_recursive: tests/irayo_recursive.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

irayo_closure: tests/irayo_closure.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

finalizer: tests/finalizer.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

errhangtest: tests/errhangtest.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

error-test: tests/error.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

appendud: tests/appendud.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

func_is_string: tests/func_is_string.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

linda_perf: tests/linda_perf.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

atexit: tests/atexit.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

rupval: tests/rupval.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

package: tests/package.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

pingpong: tests/pingpong.lua $(_TARGET_SO)
	$(_PREFIX) $(LUA) $<

#---
perftest-plain: tests/perftest.lua $(_TARGET_SO)
	$(MAKE) _perftest ARGS="$< $(N) -plain"

perftest: tests/perftest.lua $(_TARGET_SO)
	$(MAKE) _perftest ARGS="$< $(N)"

perftest-odd: tests/perftest.lua $(_TARGET_SO)
	$(MAKE) _perftest ARGS="$< $(N) -prio=+2"

perftest-even: tests/perftest.lua $(_TARGET_SO)
	$(MAKE) _perftest ARGS="$< $(N) -prio=-2"

#---
launchtest: tests/launchtest.lua $(_TARGET_SO)
	$(MAKE) _perftest ARGS="$< $(N)"

_perftest:
	$(_PREFIX) $(TIME) $(LUA) $(ARGS)


#--- Installing ---
#
# This is for LuaRocks automatic install, mainly
#
# LUA_LIBDIR and LUA_SHAREDIR are used by the .rockspec (don't change the names!)
#
DESTDIR=/usr/local
LUA_LIBDIR=$(DESTDIR)/lib/lua/5.1
LUA_SHAREDIR=$(DESTDIR)/share/lua/5.1

#
# AKa 17-Oct: changed to use 'install -m 644' and 'cp -p'
#
install: $(_TARGET_SO) src/lanes.lua
	mkdir -p $(LUA_LIBDIR) $(LUA_LIBDIR)/lanes $(LUA_SHAREDIR)
	install -m 644 $(_TARGET_SO) $(LUA_LIBDIR)/lanes
	cp -p src/lanes.lua $(LUA_SHAREDIR)


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
require: $(_TARGET_SO)
	$(_PREFIX) $(LUA) -e "require '$(MODULE)'"

run: $(_TARGET_SO)
	$(_PREFIX) $(LUA) -e "require '$(MODULE)'" -i

echo:
	@echo $(PROGRAMFILES:C=X)

.PROXY:	all clean test require debug _nodemo _notest

