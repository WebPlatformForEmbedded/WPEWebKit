#============================================================================
# Copyright (c) 2023 Liberty Global
#============================================================================

# - Try to find ODH telemetry
#
# Once done this will define
#  OdhErrTelemetry_FOUND           - System has the component
#  OdhErrTelemetry_INCLUDE_DIRS    - Component include directories
#  OdhErrTelemetry_LIBRARIES       - Libraries needed to use the component

find_package(PkgConfig QUIET)

find_path(OdhErrTelemetry_INCLUDE_DIR
          NAMES odherr.h
          HINTS ${TARGET_SYS_ROOT}/usr/include/rdk/libodherr
          )

find_library(OdhErrTelemetry_LIBRARY
             NAMES odherr
             HINTS ${TARGET_SYS_ROOT}/usr/lib
             )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OdhErrTelemetry
    FOUND_VAR OdhErrTelemetry_FOUND
    REQUIRED_VARS OdhErrTelemetry_LIBRARY OdhErrTelemetry_INCLUDE_DIR
)

if (OdhErrTelemetry_LIBRARY AND NOT TARGET OdhErrTelemetry::OdhErrTelemetry)
    add_library(OdhErrTelemetry::OdhErrTelemetry UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OdhErrTelemetry::OdhErrTelemetry PROPERTIES
        IMPORTED_LOCATION "${OdhErrTelemetry_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${OdhErrTelemetry_COMPILE_OPTIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${OdhErrTelemetry_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced(OdhErrTelemetry_INCLUDE_DIR OdhErrTelemetry_LIBRARY)

if (OdhErrTelemetry_FOUND)
    set(OdhErrTelemetry_INCLUDE_DIRS ${OdhErrTelemetry_INCLUDE_DIR})
    set(OdhErrTelemetry_LIBRARIES ${OdhErrTelemetry_LIBRARY})
    set(OdhErrTelemetry_PKG_EXTRA_LIBS "-lodherr")
endif ()
