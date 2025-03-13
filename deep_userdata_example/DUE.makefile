#
# Lanes/deep_userdata_example/DUE.makefile
#

include ../Shared.makefile

_MODULE=deep_userdata_example.$(_SO)

_SRC:=$(wildcard *.cpp) ../src/compat.cpp ../src/deep.cpp

_OBJ:=$(_SRC:.cpp=.o)

#---
all: $(_MODULE)
	$(info CC: $(CC))
	$(info _MODULE: $(_MODULE))
	$(info _SRC: $(_SRC))
	
_pch.hpp.gch: ../src/_pch.hpp
	$(CC) -I "../.." $(CFLAGS) -x c++-header $< -o _pch.hpp.gch

%.o: %.cpp  _pch.hpp.gch DUE.makefile
	$(CC) -I "../.." $(CFLAGS) -c $<

# Note: Don't put $(LUA_LIBS) ahead of $^; MSYS will not like that (I think)
#
$(_MODULE): $(_OBJ)
	$(CC) $(LIBFLAG) $^ $(LIBS) $(LUA_LIBS) -o $@

install:
	install -m 644 $(_MODULE) $(LUA_LIBDIR)/

clean:
	-rm -rf $(_MODULE) *.o *.map *.gch

.PHONY: all clean
