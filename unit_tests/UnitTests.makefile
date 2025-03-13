#
# Lanes/unit_tests/UnitTests.makefile
#

include ../Shared.makefile

_SRC := $(wildcard *.cpp) ../src/deep.cpp ../src/compat.cpp

_OBJ := $(_SRC:.cpp=.o)


_UNITTEST_TARGET := UnitTests$(_LUAEXT)

#---
all: $(_UNITTEST_TARGET)
	$(info CC: $(CC))
	$(info _SRC: $(_SRC))

_pch.hpp.gch: _pch.hpp
	$(CC) -I "../.." $(CFLAGS) -x c++-header _pch.hpp -o _pch.hpp.gch

%.o: %.cpp _pch.hpp.gch *.h *.hpp UnitTests.makefile
	$(CC) -I "../.." $(CFLAGS) -c $<

# Note: Don't put $(LUA_LIBS) ahead of $^; MSYS will not like that (I think)
#
$(_UNITTEST_TARGET): $(_OBJ)
	$(CC) $^ $(LIBS) $(LUA_LIBS) -o $@

clean:
	-rm -rf $(_UNITTEST_TARGET) *.o *.map *.gch

.PHONY: all clean
