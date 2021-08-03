/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bli
 *
 * Header for a platform compatible replacement of C++17's `std::filesystem`. Access via
 * `blender::bli::filesystem`.
 *
 * The need for this comes from macOS only supporting `std::filesystem` with XCode 11, only
 * available on macOS 10.15.
 */

#pragma once

/* ghc::filesystem includes Windows.h, which by default pollutes the global namespace and impacts
 * compile time quite a bit. Set some defines to avoid this. */
#ifdef WIN32
#  ifndef NOGDI
#    define NOGDI
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOCOMM
#    define NOCOMM
#  endif
#endif /* _WIN32 */

/* Header for ghc::filesystem. */
#include "filesystem.hpp"

namespace blender::bli {
namespace filesystem = ghc::filesystem;
}
