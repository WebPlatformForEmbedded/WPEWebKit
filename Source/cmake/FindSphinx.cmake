# - Try to find CMU Sphinx Package
# Once done this will define
#
#  SPHINX_FOUND - system has Sphinxbase
#  SPHINX_INCLUDE_DIR - the Sphinxbase include directory
#  SPHINX_LIBRARIES - Link these to use Sphinxbase
#  SPHINX_DEFINITIONS - Compiler switches required for using Sphinxbase
#  POCKETSPHINX_FOUND - system has Pocketsphinx
#  POCKETSPHINX_INCLUDE_DIR - the Pocketsphinx include directory
#  POCKETSPHINX_LIBRARIES - Link these to use Pocketsphinx
#  POCKETSPHINX_DEFINITIONS - Compiler switches required for using Pocketsphinx
#
# Copyright (C) 2016 Metrological
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND ITS CONTRIBUTORS ``AS
# IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR ITS
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

if (SPHINX_INCLUDE_DIR AND SPHINX_LIBRARIES)
    # in cache already
    set(Sphinx_FIND_QUIETLY TRUE)
endif (SPHINX_INCLUDE_DIR AND SPHINX_LIBRARIES)

# use pkg-config to get the directories and then use these values
# in the find_path() and find_library() calls
find_package(PkgConfig)
pkg_check_modules(PC_SPHINXBASE sphinxbase)
set(SPHINX_DEFINITIONS ${PC_SPHINXBASE_CFLAGS_OTHER})

find_path(SPHINX_INCLUDE_DIR NAMES sphinxbase_export.h
    PATHS
    ${PC_SPHINXBASE_INCLUDEDIR}
    ${PC_SPHINXBASE_INCLUDE_DIRS}
)

find_library(SPHINX_BASE_LIBRARY NAMES sphinxbase
    PATHS
    ${PC_SPHINXBASE_LIBDIR}
    ${PC_SPHINXBASE_LIBRARY_DIRS}
)

find_library(SPHINX_AD_LIBRARY NAMES sphinxad
    PATHS
    ${PC_SPHINXBASE_LIBDIR}
    ${PC_SPHINXBASE_LIBRARY_DIRS}
)

set (SPHINX_LIBRARY ${SPHINX_BASE_LIBRARY}  ${SPHINX_AD_LIBRARY})

if (SPHINX_INCLUDE_DIR AND SPHINX_LIBRARY)
    set (SPHINX_FOUND 1)
    set (SPHINX_LIBRARIES ${SPHINX_LIBRARY})
    set (SPHINX_INCLUDE_DIRS ${SPHINX_INCLUDE_DIR})

    pkg_check_modules(PC_POCKETSPHINX pocketsphinx)
    set(POCKETSPHINX_DEFINITIONS ${PC_POCKETSPHINX_CFLAGS_OTHER})

    find_path(POCKETSPHINX_INCLUDE_DIR NAMES pocketsphinx.h
        PATHS
        ${PC_POCKETSPHINX_INCLUDEDIR}
        ${PC_POCKETSPHINX_INCLUDE_DIRS}
    ) 

    find_library(POCKETSPHINX_LIBRARY NAMES pocketsphinx
        PATHS
        ${PC_POCKETSPHINX_LIBDIR}
        ${PC_POCKETSPHINX_LIBRARY_DIRS}
    )

    if (POCKETSPHINX_INCLUDE_DIR AND POCKETSPHINX_LIBRARY)
        set (POCKETSPHINX_FOUND 1)
        set (POCKETSPHINX_LIBRARIES ${POCKETSPHINX_LIBRARY})
        set (POCKETSPHINX_INCLUDE_DIRS ${POCKETSPHINX_INCLUDE_DIR})
    endif ()
endif ()

if (SPHINX_FOUND)
    if (NOT SPHNIX_FIND_QUIETLY)
        message(STATUS "Found Sphnixbase header files in ${SPHNIX_INCLUDE_DIRS}")
        message(STATUS "Found Sphnixbase libraries: ${SPHNIX_LIBRARIES}")
    endif ()
    if (POCKETSPHINX_FOUND)
        if (NOT POCKETSPHNIX_FIND_QUIETLY)
            message(STATUS "Found Pocketsphnix header files in ${POCKETSPHNIX_INCLUDE_DIRS}")
            message(STATUS "Found Pocketsphnix libraries: ${POCKETSPHNIX_LIBRARIES}")
        endif ()
    else ()
         message(FATAL_ERROR "Could not find Pocketsphinx")          
    endif ()
else ()
    message(FATAL_ERROR "Could not find Sphinxbase")
endif ()
