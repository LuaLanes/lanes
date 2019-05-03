# Copyright (C) 2007-2009 LuaDist.
# Created by Peter Kapec
# Redistribution and use of this file is allowed according to the terms of the MIT license.
# For details see the COPYRIGHT file distributed with LuaDist.
# Please note that the package source code is licensed under its own license.

PROJECT(lanes C)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)


FIND_PACKAGE(Lua51 REQUIRED)
INCLUDE_DIRECTORIES(${LUA_INCLUDE_DIR})

if(USE_PTHREAD)
ADD_DEFINITIONS(-DHAVE_WIN32_PTHREAD)
endif(USE_PTHREAD)
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


# Build
INCLUDE_DIRECTORIES(src)
aux_source_directory(./src LANES_SRC)
ADD_LIBRARY(core MODULE ${LANES_SRC})

IF(UNIX AND NOT CYGWIN)
  SET(LIBS pthread)
ENDIF(UNIX AND NOT CYGWIN)


if(WIN32)
TARGET_LINK_LIBRARIES(core ${LUA_LIBRARY} ${LIBS})
else(WIN32)
TARGET_LINK_LIBRARIES(core ${LIBS})
endif(WIN32)

SET_TARGET_PROPERTIES(core PROPERTIES PREFIX "")

# Install all files and documentation
set(INSTALL_LMOD share/lua/lmod CACHE PATH "Directory to install Lua modules.")
set(INSTALL_CMOD share/lua/cmod CACHE PATH "Directory to install Lua binary modules.")
set(INSTALL_DATA share/${PROJECT_NAME} CACHE PATH "Directory the package can store documentation, tests or other data in.")
set(INSTALL_DOC ${INSTALL_DATA}/doc CACHE PATH "Recommended directory to install documentation into.")
set(INSTALL_TEST ${INSTALL_DATA}/test CACHE PATH "Recommended directory to install tests into.")

INSTALL (TARGETS core DESTINATION ${INSTALL_CMOD})
INSTALL (FILES src/lanes.lua DESTINATION ${INSTALL_LMOD})

INSTALL (FILES ABOUT BUGS COPYRIGHT CHANGES README TODO DESTINATION ${INSTALL_DATA})
INSTALL (DIRECTORY docs/ DESTINATION ${INSTALL_DOC})
INSTALL (DIRECTORY tests/ DESTINATION ${INSTALL_TEST})
