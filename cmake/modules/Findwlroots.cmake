#.rst:
# Findwlroots
# --------------
#
# Try to find the wlroots library.
#
# This will define the following variables:
#
# ``wlroots_FOUND``
#    True if wlroots is available
# ``wlroots_LIBRARIES``
#    This has to be passed to target_link_libraries()
# ``wlroots_INCLUDE_DIRS``
#    This has to be passed to target_include_directories()
# ``wlroots_DEFINITIONS``
#    Compiler switches when using wlroots
#

#==============================================================================
# SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: BSD-3-Clause
#==============================================================================

find_package(PkgConfig)
pkg_check_modules(PKG_wlroots QUIET wlroots)

set(wlroots_VERSION ${PKG_wlroots_VERSION})
set(wlroots_INCLUDE_DIRS ${PKG_wlroots_INCLUDE_DIRS})

find_library(wlroots_LIBRARIES
  NAMES ${PKG_wlroots_LIBRARIES}
  HINTS ${PKG_wlroots_LIBRARY_DIRS}
)

set(wlroots_DEFINITIONS ${PKG_wlroots_CFLAGS_OTHER})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(wlroots
  FOUND_VAR
    wlroots_FOUND
  REQUIRED_VARS
    wlroots_LIBRARIES
    wlroots_INCLUDE_DIRS
  VERSION_VAR
    wlroots_VERSION
)

if(wlroots_FOUND AND NOT TARGET wlroots::wlroots)
  add_library(wlroots::wlroots UNKNOWN IMPORTED)
  set_target_properties(wlroots::wlroots PROPERTIES
    IMPORTED_LOCATION "${wlroots_LIBRARIES}"
    INTERFACE_COMPILE_OPTIONS "${wlroots_DEFINITIONS}"
    INTERFACE_INCLUDE_DIRECTORIES "${wlroots_INCLUDE_DIRS}"
  )
endif()

mark_as_advanced(wlroots_LIBRARIES wlroots_INCLUDE_DIRS)

include(FeatureSummary)
set_package_properties(wlroots PROPERTIES
  URL "https://github.com/swaywm/wlroots"
  DESCRIPTION "Modular Wayland compositor library"
)
