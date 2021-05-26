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
# The Original Code is Copyright (C) 2021, Blender Foundation
# All rights reserved.
# ***** END GPL LICENSE BLOCK *****

# avoid noisy warnings
if(CMAKE_COMPILER_IS_GNUCC)
  remove_cc_flag(
    "-Wmissing-declarations"
  )
endif()

set(INC
  .
  include
)

set(INC_SYS
)

set(SRC
  src/benchmark.cc
  src/benchmark_runner.cc
  src/benchmark_register.cc
  src/benchmark_name.cc
  src/benchmark_main.cc
  src/benchmark_api_internal.cc
  src/colorprint.cc
  src/commandlineflags.cc
  src/complexity.cc
  src/console_reporter.cc
  src/counter.cc
  src/csv_reporter.cc
  src/json_reporter.cc
  src/reporter.cc
  src/sleep.cc
  src/statistics.cc
  src/string_util.cc
  src/sysinfo.cc
  src/timers.cc
)

set(LIB
)

blender_add_lib(extern_gbenchmark "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
