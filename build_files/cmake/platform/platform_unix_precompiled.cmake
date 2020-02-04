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
# The Original Code is Copyright (C) 2020, Blender Foundation
# All rights reserved.
#
# ***** END GPL LICENSE BLOCK *****

# Libraries configuration using hard-coded paths to pre-compiled libraries
# and headers.

# ZLIB
set(ZLIB_FOUND TRUE)
set(ZLIB_ROOT ${LIBDIR}/zlib)
set(ZLIB_INCLUDE_DIR ${ZLIB_ROOT}/include)
set(ZLIB_LIBRARY ${ZLIB_ROOT}/lib/libz.a)
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})

# PNG
set(PNG_FOUND TRUE)
set(PNG_ROOT ${LIBDIR}/png)
set(PNG_INCLUDE_DIR ${PNG_ROOT}/include ${ZLIB_INCLUDE_DIR})
set(PNG_LIBRARY ${PNG_ROOT}/lib/libpng16.a ${ZLIB_LIBRARY})
set(PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIR})
set(PNG_LIBRARIES ${PNG_LIBRARY})

# TIFF
set(TIFF_FOUND TRUE)
set(TIFF_ROOT ${LIBDIR}/tiff)
set(TIFF_INCLUDE_DIR ${TIFF_ROOT}/include)
set(TIFF_LIBRARY ${TIFF_ROOT}/lib/libtiff.a)
set(TIFF_INCLUDE_DIRS ${TIFF_INCLUDE_DIR})
set(TIFF_LIBRARIES ${TIFF_LIBRARY})

# ALEMBIC
set(ALEMBIC_FOUND TRUE)
set(ALEMBIC_ROOT ${LIBDIR}/alembic)
set(ALEMBIC_INCLUDE_DIR ${ALEMBIC_ROOT}/include)
set(ALEMBIC_LIBRARY ${ALEMBIC_ROOT}/lib/libAlembic.a)
set(ALEMBIC_INCLUDE_DIRS ${ALEMBIC_INCLUDE_DIR})
set(ALEMBIC_LIBRARIES ${ALEMBIC_LIBRARY})

# BLOSC
set(BLOSC_FOUND TRUE)
set(BLOSC_ROOT ${LIBDIR}/blosc)
set(BLOSC_INCLUDE_DIR ${BLOSC_ROOT}/include)
set(BLOSC_LIBRARY ${BLOSC_ROOT}/lib/libblosc.a)
set(BLOSC_INCLUDE_DIRS ${BLOSC_INCLUDE_DIR})
set(BLOSC_LIBRARIES ${BLOSC_LIBRARY})

# BOOST
set(BOOST_FOUND TRUE)
set(BOOST_ROOT ${LIBDIR}/boost)
set(BOOST_INCLUDE_DIR ${BOOST_ROOT}/include)
set(BOOST_LIBPATH ${BOOST_ROOT}/lib)
set(BOOST_LIBRARIES
  ${BOOST_ROOT}/lib/libboost_filesystem.a
  ${BOOST_ROOT}/lib/libboost_regex.a
  ${BOOST_ROOT}/lib/libboost_thread.a
  ${BOOST_ROOT}/lib/libboost_date_time.a
  ${BOOST_ROOT}/lib/libboost_wave.a
  ${BOOST_ROOT}/lib/libboost_locale.a
  ${BOOST_ROOT}/lib/libboost_iostreams.a
  ${BOOST_ROOT}/lib/libboost_system.a
  ${BOOST_ROOT}/lib/libboost_chrono.a
  ${BOOST_ROOT}/lib/libboost_atomic.a
  ${BOOST_ROOT}/lib/libboost_serialization.a)

# FREETYPE
set(FREETYPE_FOUND TRUE)
set(FREETYPE_ROOT ${LIBDIR}/freetype)
set(FREETYPE_INCLUDE_DIRS
  ${FREETYPE_ROOT}/include
  ${FREETYPE_ROOT}/include/freetype2
)
set(FREETYPE_LIBRARIES ${FREETYPE_ROOT}/lib/libfreetype.a)
set(FREETYPE_LIBRARY ${FREETYPE_ROOT}/lib/libfreetype.a)

# FFMPEG
set(FFMPEG_ROOT ${LIBDIR}/ffmpeg)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_ROOT}/include)
set(FFMPEG_LIBRARIES
  ${FFMPEG_ROOT}/lib/libavformat.a
  ${FFMPEG_ROOT}/lib/libavcodec.a
  ${FFMPEG_ROOT}/lib/libavdevice.a
  ${FFMPEG_ROOT}/lib/libavutil.a
  ${FFMPEG_ROOT}/lib/libswresample.a
  ${FFMPEG_ROOT}/lib/libswscale.a

  ${FFMPEG_ROOT}/lib/libtheora.a
  ${FFMPEG_ROOT}/lib/libtheoradec.a
  ${FFMPEG_ROOT}/lib/libtheoraenc.a

  ${FFMPEG_ROOT}/lib/libvorbis.a
  ${FFMPEG_ROOT}/lib/libvorbisenc.a
  ${FFMPEG_ROOT}/lib/libvorbisfile.a

  ${FFMPEG_ROOT}/lib/libvpx.a
  ${FFMPEG_ROOT}/lib/libx264.a
  ${FFMPEG_ROOT}/lib/libmp3lame.a
  ${FFMPEG_ROOT}/lib/libogg.a
  ${FFMPEG_ROOT}/lib/libopus.a
  ${FFMPEG_ROOT}/lib/libxvidcore.a
)
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})

# FFTW3
set(FFTW3_ROOT ${LIBDIR}/fftw3)
set(FFTW3_INCLUDE_DIR ${FFTW3_ROOT}/include)
set(FFTW3_LIBRARY ${FFTW3_ROOT}/lib/libfftw3.a)
set(FFTW3_INCLUDE_DIRS ${FFTW3_INCLUDE_DIR})
set(FFTW3_LIBRARIES ${FFTW3_LIBRARY})

# JEMALLOC
set(JEMALLOC_FOUND TRUE)
set(JEMALLOC_ROOT ${LIBDIR}/jemalloc)
set(JEMALLOC_INCLUDE_DIR ${JEMALLOC_ROOT}/include/jemalloc)
set(JEMALLOC_LIBRARY ${JEMALLOC_ROOT}/lib/libjemalloc.a)
set(JEMALLOC_INCLUDE_DIRS ${JEMALLOC_INCLUDE_DIR})
set(JEMALLOC_LIBRARIES ${JEMALLOC_LIBRARY})

# JPEG
set(JPEG_FOUND TRUE)
set(JPEG_ROOT ${LIBDIR}/jpeg)
set(JPEG_INCLUDE_DIR ${JPEG_ROOT}/include)
set(JPEG_LIBRARY ${JPEG_ROOT}/lib/libjpeg.a)
set(JPEG_INCLUDE_DIRS ${JPEJPEG_INCLUDE_DIR})
set(JPEG_LIBRARIES ${JPEG_LIBRARY})

# LLVM
set(LLVM_FOUND TRUE)
set(LLVM_ROOT ${LIBDIR}/llvm)
set(LLVM_LIBPATH ${LIBDIR}/llvm/lib)
set(LLVM_LIBRARY
  ${LLVM_ROOT}/lib/libLLVMLTO.a
  ${LLVM_ROOT}/lib/libLLVMPasses.a
  ${LLVM_ROOT}/lib/libLLVMObjCARCOpts.a
  ${LLVM_ROOT}/lib/libLLVMSymbolize.a
  ${LLVM_ROOT}/lib/libLLVMDebugInfoPDB.a
  ${LLVM_ROOT}/lib/libLLVMDebugInfoDWARF.a
  ${LLVM_ROOT}/lib/libLLVMFuzzMutate.a
  ${LLVM_ROOT}/lib/libLLVMTableGen.a
  ${LLVM_ROOT}/lib/libLLVMDlltoolDriver.a
  ${LLVM_ROOT}/lib/libLLVMLineEditor.a
  ${LLVM_ROOT}/lib/libLLVMOrcJIT.a
  ${LLVM_ROOT}/lib/libLLVMCoverage.a
  ${LLVM_ROOT}/lib/libLLVMMIRParser.a
  ${LLVM_ROOT}/lib/libLLVMObjectYAML.a
  ${LLVM_ROOT}/lib/libLLVMLibDriver.a
  ${LLVM_ROOT}/lib/libLLVMOption.a
  ${LLVM_ROOT}/lib/libLLVMWindowsManifest.a
  ${LLVM_ROOT}/lib/libLLVMX86Disassembler.a
  ${LLVM_ROOT}/lib/libLLVMX86AsmParser.a
  ${LLVM_ROOT}/lib/libLLVMX86CodeGen.a
  ${LLVM_ROOT}/lib/libLLVMGlobalISel.a
  ${LLVM_ROOT}/lib/libLLVMSelectionDAG.a
  ${LLVM_ROOT}/lib/libLLVMAsmPrinter.a
  ${LLVM_ROOT}/lib/libLLVMDebugInfoCodeView.a
  ${LLVM_ROOT}/lib/libLLVMDebugInfoMSF.a
  ${LLVM_ROOT}/lib/libLLVMX86Desc.a
  ${LLVM_ROOT}/lib/libLLVMMCDisassembler.a
  ${LLVM_ROOT}/lib/libLLVMX86Info.a
  ${LLVM_ROOT}/lib/libLLVMX86AsmPrinter.a
  ${LLVM_ROOT}/lib/libLLVMX86Utils.a
  ${LLVM_ROOT}/lib/libLLVMMCJIT.a
  ${LLVM_ROOT}/lib/libLLVMInterpreter.a
  ${LLVM_ROOT}/lib/libLLVMExecutionEngine.a
  ${LLVM_ROOT}/lib/libLLVMRuntimeDyld.a
  ${LLVM_ROOT}/lib/libLLVMCodeGen.a
  ${LLVM_ROOT}/lib/libLLVMTarget.a
  ${LLVM_ROOT}/lib/libLLVMCoroutines.a
  ${LLVM_ROOT}/lib/libLLVMipo.a
  ${LLVM_ROOT}/lib/libLLVMInstrumentation.a
  ${LLVM_ROOT}/lib/libLLVMVectorize.a
  ${LLVM_ROOT}/lib/libLLVMScalarOpts.a
  ${LLVM_ROOT}/lib/libLLVMLinker.a
  ${LLVM_ROOT}/lib/libLLVMIRReader.a
  ${LLVM_ROOT}/lib/libLLVMAsmParser.a
  ${LLVM_ROOT}/lib/libLLVMInstCombine.a
  ${LLVM_ROOT}/lib/libLLVMTransformUtils.a
  ${LLVM_ROOT}/lib/libLLVMBitWriter.a
  ${LLVM_ROOT}/lib/libLLVMAnalysis.a
  ${LLVM_ROOT}/lib/libLLVMProfileData.a
  ${LLVM_ROOT}/lib/libLLVMObject.a
  ${LLVM_ROOT}/lib/libLLVMMCParser.a
  ${LLVM_ROOT}/lib/libLLVMMC.a
  ${LLVM_ROOT}/lib/libLLVMBitReader.a
  ${LLVM_ROOT}/lib/libLLVMCore.a
  ${LLVM_ROOT}/lib/libLLVMBinaryFormat.a
  ${LLVM_ROOT}/lib/libLLVMSupport.a
  ${LLVM_ROOT}/lib/libLLVMDemangle.a
)

# PCRE
set(PCRE_ROOT ${LIBDIR}/opencollada)
set(PCRE_INCLUDE_DIR)
set(PCRE_LIBRARY ${PCRE_ROOT}/lib/libpcre.a)
set(PCRE_INCLUDE_DIRS ${PCRE_INCLUDE_DIR})
set(PCRE_LIBRARIES ${PCRE_LIBRARY})

# OPENAL
set(OPENAL_FOUND TRUE)
set(OPENAL_ROOT ${LIBDIR}/openal)
set(OPENAL_INCLUDE_DIR ${LIBDIR}/openal/include/AL)
set(OPENAL_LIBRARY ${LIBDIR}/openal/lib/libopenal.a)
set(OPENAL_INCLUDE_DIRS ${OPENAL_INCLUDE_DIR})
set(OPENAL_LIBRARIES ${OPENAL_LIBRARY})

# OPENCOLLADA
set(OPENCOLLADA_FOUND TRUE)
set(OPENCOLLADA_ROOT ${LIBDIR}/opencollada)
set(OPENCOLLADA_INCLUDE_DIRS
  ${OPENCOLLADA_ROOT}/include/COLLADAStreamWriter
  ${OPENCOLLADA_ROOT}/include/COLLADABaseUtils
  ${OPENCOLLADA_ROOT}/include/COLLADAFramework
  ${OPENCOLLADA_ROOT}/include/COLLADASaxFrameworkLoader
  ${OPENCOLLADA_ROOT}/include/GeneratedSaxParser)
set(OPENCOLLADA_LIBRARIES
  ${OPENCOLLADA_ROOT}/lib/libOpenCOLLADAStreamWriter.a
  ${OPENCOLLADA_ROOT}/lib/libOpenCOLLADASaxFrameworkLoader.a
  ${OPENCOLLADA_ROOT}/lib/libOpenCOLLADAFramework.a
  ${OPENCOLLADA_ROOT}/lib/libOpenCOLLADABaseUtils.a
  ${OPENCOLLADA_ROOT}/lib/libGeneratedSaxParser.a
  ${OPENCOLLADA_ROOT}/lib/libMathMLSolver.a
  ${OPENCOLLADA_ROOT}/lib/libbuffer.a
  ${OPENCOLLADA_ROOT}/lib/libftoa.a
  ${OPENCOLLADA_ROOT}/lib/libUTF.a
)

# OPENCOLORIO
set(OPENCOLORIO_FOUND TRUE)
set(OPENCOLORIO_ROOT ${LIBDIR}/opencolorio)
set(OPENCOLORIO_INCLUDE_DIRS ${OPENCOLORIO_ROOT}/include)
set(OPENCOLORIO_LIBRARIES
  ${OPENCOLORIO_ROOT}/lib/libOpenColorIO.a
  ${OPENCOLORIO_ROOT}/lib/libyaml-cpp.a
  ${OPENCOLORIO_ROOT}/lib/libtinyxml.a
)

# OPENEXR
set(OPENEXR_FOUND TRUE)
set(OPENEXR_ROOT ${LIBDIR}/openexr)
set(OPENEXR_INCLUDE_DIRS
  ${OPENEXR_ROOT}/include
  ${OPENEXR_ROOT}/include/OpenEXR
)
set(OPENEXR_LIBRARIES
  ${OPENEXR_ROOT}/lib/libHalf.a
  ${OPENEXR_ROOT}/lib/libIex.a
  ${OPENEXR_ROOT}/lib/libIlmImf.a
  ${OPENEXR_ROOT}/lib/libIlmThread.a
  ${OPENEXR_ROOT}/lib/libImath.a
)

# OPENIMAGEDENOISE
set(OPENIMAGEDENOISE_FOUND TRUE)
set(OPENIMAGEDENOISE_ROOT ${LIBDIR}/openimagedenoise)
set(OPENIMAGEDENOISE_INCLUDE_DIRS ${OPENIMAGEDENOISE_ROOT}/include)
set(OPENIMAGEDENOISE_LIBRARIES
  ${OPENIMAGEDENOISE_ROOT}/lib/libOpenImageDenoise.a
  ${OPENIMAGEDENOISE_ROOT}/lib/libcommon.a
  ${OPENIMAGEDENOISE_ROOT}/lib/libmkldnn.a
)

# OPENIMAGEIO
set(OPENIMAGEIO_FOUND TRUE)
set(OPENIMAGEIO_ROOT ${LIBDIR}/openimageio)
set(OPENIMAGEIO_INCLUDE_DIRS ${OPENIMAGEIO_ROOT}/include)
set(OPENIMAGEIO_LIBRARIES
  ${OPENIMAGEIO_ROOT}/lib/libOpenImageIO.a
  ${OPENIMAGEIO_ROOT}/lib/libOpenImageIO_Util.a
  ${PNG_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${TIFF_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${BOOST_LIBRARIES}
)
set(OPENIMAGEIO_IDIFF ${OPENIMAGEIO_ROOT}/bin/idiff)

# OPENJPEG
set(OPENJPEG_FOUND TRUE)
set(OPENJPEG_ROOT ${LIBDIR}/openjpeg)
set(OPENJPEG_INCLUDE_DIR ${LIBDIR}/openjpeg/include)
set(OPENJPEG_LIBRARY ${LIBDIR}/openjpeg/lib/libopenjp2.a)
set(OPENJPEG_INCLUDE_DIRS ${OPENJPEG_INCLUDE_DIR})
set(OPENJPEG_LIBRARIES ${OPENJPEG_LIBRARY})

# OPENSUBDIV
set(OPENSUBDIV_FOUND TRUE)
set(OPENSUBDIV_ROOT ${LIBDIR}/opensubdiv)
set(OPENSUBDIV_INCLUDE_DIR ${OPENSUBDIV_ROOT}/include)
set(OPENSUBDIV_INCLUDE_DIRS ${OPENSUBDIV_INCLUDE_DIR})
set(OPENSUBDIV_LIBRARIES
  ${OPENSUBDIV_ROOT}/lib/libosdGPU.a
  ${OPENSUBDIV_ROOT}/lib/libosdCPU.a
)

# OPENVDB
set(OPENVDB_FOUND TRUE)
set(OPENVDB_ROOT ${LIBDIR}/openvdb)
set(OPENVDB_INCLUDE_DIR ${OPENVDB_ROOT}/include)
set(OPENVDB_LIBRARY ${OPENVDB_ROOT}/lib/libopenvdb.a)
set(OPENVDB_INCLUDE_DIRS ${OPENVDB_INCLUDE_DIR})
set(OPENVDB_LIBRARIES ${OPENVDB_LIBRARY})

# OSL
set(OSL_FOUND TRUE)
set(OSL_ROOT ${LIBDIR}/osl)
set(OSL_INCLUDE_DIR ${OSL_ROOT}/include)
set(OSL_LIBRARIES
  ${OSL_ROOT}/lib/liboslcomp.a
  ${OSL_ROOT}/lib/liboslexec.a
  ${OSL_ROOT}/lib/liboslquery.a
)
set(OSL_COMPILER ${OSL_ROOT}/bin/oslc)
set(OSL_INCLUDE_DIRS ${OSL_INCLUDE_DIR})

# PYTHON
set(PYTHON_VERSION 3.7)
set(PYTHON_VERSION_ABI ${PYTHON_VERSION}m)
set(PYTHON_ROOT ${LIBDIR}/python)
set(PYTHON_INCLUDE_DIR ${PYTHON_ROOT}/include/python${PYTHON_VERSION_ABI})
set(PYTHON_LIBPATH ${PYTHON_ROOT}/lib)
set(PYTHON_LIBRARY ${PYTHON_ROOT}/lib/libpython${PYTHON_VERSION_ABI}.a)
set(PYTHON_INCLUDE_DIRS ${PYTHON_INCLUDE_DIR})
set(PYTHON_LIBRARIES ${PYTHON_LIBRARY})
set(PYTHON_EXECUTABLE ${PYTHON_ROOT}/bin/python${PYTHON_VERSION_ABI})
set(PYTHON_LINKFLAGS -Xlinker -export-dynamic)

# SDL
set(SDL_FOUND TRUE)
set(SDL_ROOT ${LIBDIR}/sdl)
set(SDL_INCLUDE_DIR ${SDL_ROOT}/include)
set(SDL_LIBRARY ${SDL_ROOT}/lib/libSDL2.a)
set(SDL_INCLUDE_DIRS ${SDL_INCLUDE_DIR})
set(SDL_LIBRARIES ${SDL_LIBRARY})

# SNDFILE
set(LIBSNDFILE_ROOT ${LIBDIR}/sndfile)
set(LIBSNDFILE_INCLUDE_DIRS ${LIBSNDFILE_ROOT}/include)
set(LIBSNDFILE_LIBRARIES
  ${LIBSNDFILE_ROOT}/lib/libsndfile.a
  ${LIBSNDFILE_ROOT}/lib/libFLAC.a
  ${FFMPEG_ROOT}/lib/libogg.a
)

# SPACENAV
set(SPACENAV_FOUND TRUE)
set(SPACENAV_ROOT ${LIBDIR}/spnav)
set(SPACENAV_INCLUDE_DIR ${SPACENAV_ROOT}/include)
set(SPACENAV_LIBRARY ${SPACENAV_ROOT}/lib/libspnav.a)
set(SPACENAV_INCLUDE_DIRS ${SPACENAV_INCLUDE_DIR})
set(SPACENAV_LIBRARIES ${SPACENAV_LIBRARY})

# TBB
set(TBB_FOUND TRUE)
set(TBB_ROOT ${LIBDIR}/tbb)
set(TBB_INCLUDE_DIR ${TBB_ROOT}/include)
set(TBB_LIBRARY ${TBB_ROOT}/lib/libtbb.a)
set(TBB_INCLUDE_DIRS ${TBB_INCLUDE_DIR})
set(TBB_LIBRARIES ${TBB_LIBRARY})

# USD
set(USD_FOUND TRUE)
set(USD_ROOT ${LIBDIR}/usd)
set(USD_INCLUDE_DIR ${USD_ROOT}/include)
set(USD_LIBRARY ${USD_ROOT}/lib/libusd_m.a)
set(USD_INCLUDE_DIRS ${USD_INCLUDE_DIR})
set(USD_LIBRARIES ${USD_LIBRARY})

# XML2
set(XML2_FOUND TRUE)
set(XML2_ROOT ${LIBDIR}/xml2)
set(XML2_INCLUDE_DIR ${XML2_ROOT}/include)
set(XML2_LIBRARY ${XML2_ROOT}/lib/libxml2.a)
set(XML2_INCLUDE_DIRS ${XML2_INCLUDE_DIR})
set(XML2_LIBRARIES ${XML2_LIBRARY})

# NDOF
set(NDOF_INCLUDE_DIR ${SPACENAV_INCLUDE_DIR})
set(NDOF_LIBRARY ${SPACENAV_LIBRARY})
set(NDOF_INCLUDE_DIRS ${SPACENAV_INCLUDE_DIRS})
set(NDOF_LIBRARIES ${SPACENAV_LIBRARIES})

# TODO(sergey): Currently there is no pre-compiled Jack library, so fall-back
# to find_package() solution.
if(WITH_JACK)
  find_package_wrapper(Jack)
  if(NOT JACK_FOUND)
    set(WITH_JACK OFF)
  endif()
endif()
