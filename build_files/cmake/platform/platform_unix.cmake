# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# The Original Code is Copyright (C) 2016, Blender Foundation
# All rights reserved.
# ***** END GPL LICENSE BLOCK *****

# Libraries configuration for any *nix system including Linux and Unix.

# Detect precompiled library directory
if(NOT DEFINED LIBDIR)
  # Path to a locally compiled libraries.
  set(LIBDIR_NAME ${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR})
  string(TOLOWER ${LIBDIR_NAME} LIBDIR_NAME)
  set(LIBDIR_NATIVE_ABI ${CMAKE_SOURCE_DIR}/../lib/${LIBDIR_NAME})

  # Path to precompiled libraries with known CentOS 7 ABI.
  set(LIBDIR_CENTOS7_ABI ${CMAKE_SOURCE_DIR}/../lib/linux_centos7_x86_64)

  # Choose the best suitable libraries.
  if(EXISTS ${LIBDIR_NATIVE_ABI})
    set(LIBDIR ${LIBDIR_NATIVE_ABI})
  elseif(EXISTS ${LIBDIR_CENTOS7_ABI})
    set(LIBDIR ${LIBDIR_CENTOS7_ABI})
    set(WITH_CXX11_ABI OFF)
  endif()

  # Avoid namespace pollustion.
  unset(LIBDIR_NATIVE_ABI)
  unset(LIBDIR_CENTOS7_ABI)
endif()

# Wrapper to prefer static libraries
macro(find_package_wrapper)
  if(WITH_STATIC_LIBS)
    find_package_static(${ARGV})
  else()
    find_package(${ARGV})
  endif()
endmacro()

if(EXISTS ${LIBDIR})
  message(STATUS "Using pre-compiled LIBDIR: ${LIBDIR}")

  set(WITH_STATIC_LIBS ON)
  set(WITH_OPENMP_STATIC ON)

  include(platform_unix_precompiled)
else()
  include(platform_unix_package)
endif()

if(WITH_STATIC_LIBS)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
endif()

# OpenSuse needs lutil, ArchLinux not, for now keep, can avoid by using --as-needed
if(HAIKU)
  list(APPEND PLATFORM_LINKLIBS -lnetwork)
else()
  list(APPEND PLATFORM_LINKLIBS -lutil -lc -lm)
endif()

find_package(Threads REQUIRED)
list(APPEND PLATFORM_LINKLIBS ${CMAKE_THREAD_LIBS_INIT})
# used by other platforms
set(PTHREADS_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

if(CMAKE_DL_LIBS)
  list(APPEND PLATFORM_LINKLIBS ${CMAKE_DL_LIBS})
endif()

# lfs on glibc, all compilers should use
add_definitions(-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE)

# GNU Compiler
if(CMAKE_COMPILER_IS_GNUCC)
  set(PLATFORM_CFLAGS "-pipe -fPIC -funsigned-char -fno-strict-aliasing")

  if(WITH_LINKER_GOLD)
    execute_process(
      COMMAND ${CMAKE_C_COMPILER} -fuse-ld=gold -Wl,--version
      ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
    if("${LD_VERSION}" MATCHES "GNU gold")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fuse-ld=gold")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
    else()
      message(STATUS "GNU gold linker isn't available, using the default system linker.")
    endif()
    unset(LD_VERSION)
  endif()

# CLang is the same as GCC for now.
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
  set(PLATFORM_CFLAGS "-pipe -fPIC -funsigned-char -fno-strict-aliasing")
# Intel C++ Compiler
elseif(CMAKE_C_COMPILER_ID MATCHES "Intel")
  # think these next two are broken
  find_program(XIAR xiar)
  if(XIAR)
    set(CMAKE_AR "${XIAR}")
  endif()
  mark_as_advanced(XIAR)

  find_program(XILD xild)
  if(XILD)
    set(CMAKE_LINKER "${XILD}")
  endif()
  mark_as_advanced(XILD)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fp-model precise -prec_div -parallel")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fp-model precise -prec_div -parallel")

  # set(PLATFORM_CFLAGS "${PLATFORM_CFLAGS} -diag-enable sc3")
  set(PLATFORM_CFLAGS "-pipe -fPIC -funsigned-char -fno-strict-aliasing")
  set(PLATFORM_LINKFLAGS "${PLATFORM_LINKFLAGS} -static-intel")
endif()
