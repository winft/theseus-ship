#.rst:
# FindPixman
# --------------
#
# Try to find the Pixman library.
#
# This will define the following variables:
#
# ``Pixman_FOUND``
#    True if Pixman is available
# ``Pixman_LIBRARIES``
#    This has to be passed to target_link_libraries()
# ``Pixman_INCLUDE_DIRS``
#    This has to be passed to target_include_directories()
# ``Pixman_DEFINITIONS``
#    Compiler switches when using Pixman
#

#==============================================================================
# SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: BSD-3-Clause
#==============================================================================

find_package(PkgConfig)
pkg_check_modules(PKG_Pixman QUIET Pixman)

set(Pixman_VERSION ${PKG_Pixman_VERSION})

find_path(Pixman_INCLUDE_DIRS
  NAMES pixman-1/pixman.h
  HINTS ${PKG_Pixman_INCLUDE_DIRS}
)

find_library(Pixman_LIBRARIES
  NAMES pixman-1
  HINTS ${PKG_Pixman_LIBRARY_DIRS}
)

set(Pixman_DEFINITIONS ${PKG_Pixman_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pixman
  FOUND_VAR
    Pixman_FOUND
  REQUIRED_VARS
    Pixman_LIBRARIES
    Pixman_INCLUDE_DIRS
  VERSION_VAR
    Pixman_VERSION
)

if(Pixman_FOUND AND NOT TARGET Pixman::Pixman)
  add_library(Pixman::Pixman UNKNOWN IMPORTED)
  set_target_properties(Pixman::Pixman PROPERTIES
    IMPORTED_LOCATION "${Pixman_LIBRARIES}"
    INTERFACE_COMPILE_OPTIONS "${Pixman_DEFINITIONS}"
    INTERFACE_INCLUDE_DIRECTORIES "${Pixman_INCLUDE_DIRS}"
  )
endif()

mark_as_advanced(Pixman_LIBRARIES Pixman_INCLUDE_DIRS)

include(FeatureSummary)
set_package_properties(Pixman PROPERTIES
  URL "https://gitlab.freedesktop.org/pixman/pixman"
  DESCRIPTION "Image processing and manipulation library"
)
