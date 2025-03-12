#
# Lanes/unit_tests/Makefile
#

include ../Shared.mk

SRC=$(wildcard *.cpp) ../src/deep.cpp ../src/compat.cpp

OBJ=$(SRC:.cpp=.o)


_UNITTEST_TARGET=UnitTests$(_LUAEXT)

#---
all: $(_UNITTEST_TARGET)

_pch.hpp.gch: _pch.hpp
	$(CC) -I "../.." $(CFLAGS) -x c++-header _pch.hpp -o _pch.hpp.gch

%.o: %.cpp _pch.hpp.gch *.h *.hpp Makefile
	$(CC) -I "../.." $(CFLAGS) -c $<

# Note: Don't put $(LUA_LIBS) ahead of $^; MSYS will not like that (I think)
#
$(_UNITTEST_TARGET): $(OBJ)
	$(CC) $^ $(LIBS) $(LUA_LIBS) -o $@

clean:
	-rm -rf $(_UNITTEST_TARGET) *.o *.map *.gch

.PROXY:	all clean
