#============================================================================
# Copyright (c) 2023 Liberty Global
#============================================================================

# - Try to find RDK Logger
#
# Once done this will define
#  RdkLogger_FOUND           - System has the component
#  RdkLogger_INCLUDE_DIRS    - Component include directories
#  RdkLogger_LIBRARIES       - Libraries needed to use the component

find_package(PkgConfig QUIET)

find_path(RdkLogger_INCLUDE_DIR
          NAMES rdk_debug.h
          HINTS ${CMAKE_SOURCE_DIR}/x86_builder/src/rdklogger/include
          )

find_library(RdkLogger_LIBRARY
             NAMES rdkloggers
             HINTS ${TARGET_SYS_ROOT}/usr/lib
             )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RdkLogger
    FOUND_VAR RdkLogger_FOUND
    REQUIRED_VARS RdkLogger_LIBRARY RdkLogger_INCLUDE_DIR
)

if (RdkLogger_LIBRARY AND NOT TARGET RdkLogger::RdkLogger)
    add_library(RdkLogger::RdkLogger UNKNOWN IMPORTED GLOBAL)
    set_target_properties(RdkLogger::RdkLogger PROPERTIES
        IMPORTED_LOCATION "${RdkLogger_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${RdkLogger_COMPILE_OPTIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${RdkLogger_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced(RdkLogger_INCLUDE_DIR RdkLogger_LIBRARY)

if (RdkLogger_FOUND)
    set(RdkLogger_INCLUDE_DIRS ${RdkLogger_INCLUDE_DIR})
    set(RdkLogger_LIBRARIES ${RdkLogger_LIBRARY})
    set(RdkLogger_PKG_EXTRA_LIBS "-lrdkloggers")
endif ()
