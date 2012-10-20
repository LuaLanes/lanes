# Copyright (C) 2007-2009 LuaDist.
# Created by Peter Kapec
# Redistribution and use of this file is allowed according to the terms of the MIT license.
# For details see the COPYRIGHT file distributed with LuaDist.
# Please note that the package source code is licensed under its own license.

PROJECT(lanes C)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
INCLUDE(dist.cmake)

FIND_PACKAGE(Lua51 REQUIRED)
FIND_PROGRAM(LUA NAMES lua5.1 lua51 lua)
FIND_PROGRAM(LUAC NAMES luac5.1 luac51 luac)
INCLUDE_DIRECTORIES(${LUA_INCLUDE_DIR})

#2DO - patch threading.c to suppot cygwin.
# The following values are just a guess.
# WARNING: test segfault under Cygwin
IF(CYGWIN)
  ADD_DEFINITIONS(-D_PRIO_MODE=SCHED_FIFO)
  ADD_DEFINITIONS(-D_PRIO_HI=15) # maximum that doesn't crash
  ADD_DEFINITIONS(-D_PRIO_0=0)
  ADD_DEFINITIONS(-D_PRIO_LO=-15) # ???
  ADD_DEFINITIONS(-Dpthread_yield=sched_yield)
ENDIF(CYGWIN)

#2DO - use provided bin2c
# Embed keeper.lua in text form in C (embedding bytecode is not LuaJIT2-compatible)
#ADD_CUSTOM_COMMAND(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/keeper.lch
#  DEPENDS src/keeper.lua
#    COMMAND "${LUA}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.lua"
#            "${CMAKE_CURRENT_SOURCE_DIR}/src/keeper.lua"
#            "-o" "${CMAKE_CURRENT_BINARY_DIR}/keeper.lch")
#SET_SOURCE_FILES_PROPERTIES(src/lanes.c PROPERTIES OBJECT_DEPENDS
#  ${CMAKE_CURRENT_BINARY_DIR}/keeper.lch)
#INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})


# Build
INCLUDE_DIRECTORIES(src)
ADD_LIBRARY(lua51-lanes MODULE src/lanes.c src/threading.c src/tools.c src/keeper.c)

IF(UNIX AND NOT CYGWIN)
  SET(LIBS pthread)
ENDIF(UNIX AND NOT CYGWIN)



TARGET_LINK_LIBRARIES(core ${LUA_LIBRARY} ${LIBS})
SET_TARGET_PROPERTIES(core PROPERTIES PREFIX "")

# Install all files and documentation
INSTALL (TARGETS core DESTINATION ${INSTALL_CMOD})
INSTALL (FILES src/lanes.lua DESTINATION ${INSTALL_LMOD})

INSTALL (FILES ABOUT BUGS COPYRIGHT CHANGES README TODO DESTINATION ${INSTALL_DATA})
INSTALL (DIRECTORY docs/ DESTINATION ${INSTALL_DOC})
INSTALL (DIRECTORY tests/ DESTINATION ${INSTALL_TEST})
