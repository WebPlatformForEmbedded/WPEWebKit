#============================================================================
# Copyright (c) 2023 Liberty Global
#============================================================================

# - Try to find ODH telemetry
#
# Once done this will define
#  OdhOttTelemetry_FOUND           - System has the component
#  OdhOttTelemetry_INCLUDE_DIRS    - Component include directories
#  OdhOttTelemetry_LIBRARIES       - Libraries needed to use the component

find_package(PkgConfig QUIET)

find_path(OdhOttTelemetry_INCLUDE_DIR
          NAMES odhott_wl.h
          HINTS ${TARGET_SYS_ROOT}/usr/include/odhott
          )

find_library(OdhOttTelemetry_LIBRARY
             NAMES odhott
             HINTS ${TARGET_SYS_ROOT}/usr/lib
             )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OdhOttTelemetry
    FOUND_VAR OdhOttTelemetry_FOUND
    REQUIRED_VARS OdhOttTelemetry_LIBRARY OdhOttTelemetry_INCLUDE_DIR
)

if (OdhOttTelemetry_LIBRARY AND NOT TARGET OdhOttTelemetry::OdhOttTelemetry)
    add_library(OdhOttTelemetry::OdhOttTelemetry UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OdhOttTelemetry::OdhOttTelemetry PROPERTIES
        IMPORTED_LOCATION "${OdhOttTelemetry_LIBRARY}"
        INTERFACE_COMPILE_OPTIONS "${OdhOttTelemetry_COMPILE_OPTIONS}"
        INTERFACE_INCLUDE_DIRECTORIES "${OdhOttTelemetry_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced(OdhOttTelemetry_INCLUDE_DIR OdhOttTelemetry_LIBRARY)

if (OdhOttTelemetry_FOUND)
    set(OdhOttTelemetry_INCLUDE_DIRS ${OdhOttTelemetry_INCLUDE_DIR})
    set(OdhOttTelemetry_LIBRARIES ${OdhOttTelemetry_LIBRARY})
    set(OdhOttTelemetry_PKG_EXTRA_LIBS "-lodhott")
endif ()
