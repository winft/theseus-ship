# SPDX-FileCopyrightText: 2024 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.23 FATAL_ERROR)

project("Theseus' Ship" VERSION 5.27.80)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0
  -DQT_NO_KEYWORDS
  -DQT_USE_QSTRINGBUILDER
  -DQT_NO_URL_CAST_FROM_STRING
  -DQT_NO_CAST_TO_ASCII
)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

# where to look first for cmake modules, before ${CMAKE_ROOT}/Modules/ is checked
set(CMAKE_MODULE_PATH cmake/modules ${ECM_MODULE_PATH})

include(FeatureSummary)
include(WriteBasicConfigVersionFile)
include(GenerateExportHeader)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
  UiTools
)

find_package(como REQUIRED 0.1)
find_package(KWinDBusInterface CONFIG REQUIRED)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(ECMInstallIcons)
include(ECMOptionalAddSubdirectory)
include(ECMConfiguredInstall)
include(ECMQmlModule)
include(ECMGenerateQmlTypes)

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
  Crash
  DBusAddons
)

find_package(KF6Kirigami2 ${KF6_MIN_VERSION} CONFIG)
set_package_properties(KF6Kirigami2 PROPERTIES
    DESCRIPTION "A QtQuick based components set"
    PURPOSE "Required at runtime for several QML effects"
    TYPE RUNTIME
)

find_package(Plasma ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}
  CONFIG
)
set_package_properties(Plasma PROPERTIES
    DESCRIPTION "A QtQuick based components set"
    PURPOSE "Required at runtime for several QML effects"
    TYPE RUNTIME
)

find_package(KDecoration2 ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}
  CONFIG REQUIRED
)

find_package(KScreenLocker CONFIG REQUIRED)
set_package_properties(KScreenLocker PROPERTIES
    TYPE REQUIRED
    PURPOSE "For screenlocker integration in kwin_wayland"
)

find_package(Breeze 5.9.0 CONFIG)
set_package_properties(Breeze PROPERTIES
    TYPE OPTIONAL
    PURPOSE "For setting the default window decoration plugin"
)
if (${Breeze_FOUND})
    if (${BREEZE_WITH_KDECORATION})
        set(HAVE_BREEZE_DECO true)
    else()
        set(HAVE_BREEZE_DECO FALSE)
    endif()
else()
    set(HAVE_BREEZE_DECO FALSE)
endif()
add_feature_info("Breeze-Decoration" HAVE_BREEZE_DECO "Default decoration plugin Breeze")

find_package(EGL)
set_package_properties(EGL PROPERTIES
    TYPE RUNTIME
    PURPOSE "Required to build KWin with EGL support"
)

find_package(epoxy 1.3)
set_package_properties(epoxy PROPERTIES
    DESCRIPTION "libepoxy"
    URL "https://github.com/anholt/libepoxy"
    TYPE REQUIRED
    PURPOSE "OpenGL dispatch library"
)

set(HAVE_DL_LIBRARY FALSE)
if (epoxy_HAS_GLX)
    find_library(DL_LIBRARY dl)
    if (DL_LIBRARY)
        set(HAVE_DL_LIBRARY TRUE)
    endif()
endif()

find_package(Wayland 1.2 REQUIRED COMPONENTS Cursor Server OPTIONAL_COMPONENTS Egl)
set_package_properties(Wayland PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building KWin with Wayland support"
)

find_package(XKB 0.7.0)
set_package_properties(XKB PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building KWin with Wayland support"
)

# TODO(romangg): Only required for X11 session.
pkg_check_modules(XKBX11 IMPORTED_TARGET xkbcommon-x11 REQUIRED)
add_feature_info(XKBX11 XKBX11_FOUND "Required for handling keyboard events in X11")

find_package(Threads)
set_package_properties(Threads PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building KWin with Wayland support"
)

find_package(Libinput 1.9)
set_package_properties(Libinput PROPERTIES TYPE REQUIRED PURPOSE "Required for input handling on Wayland.")

find_package(X11)
set_package_properties(X11 PROPERTIES
    DESCRIPTION "X11 libraries"
    URL "https://www.x.org"
    TYPE REQUIRED
)

# All the required XCB components
find_package(XCB 1.10 REQUIRED COMPONENTS
    COMPOSITE
    CURSOR
    DAMAGE
    EVENT
    GLX
    ICCCM
    IMAGE
    KEYSYMS
    RANDR
    RENDER
    SHAPE
    SHM
    SYNC
    XFIXES
    XKB
)
set_package_properties(XCB PROPERTIES TYPE REQUIRED)

find_package(Libcap)
set_package_properties(Libcap PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Needed for running kwin_wayland with real-time scheduling policy"
)
set(HAVE_LIBCAP ${Libcap_FOUND})

option(KWIN_BUILD_KCMS "Enable building of KWin configuration modules." ON)

configure_file(config-theseus-ship.h.cmake config-theseus-ship.h)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})

if (KWIN_BUILD_KCMS)
    add_subdirectory(kcms)
endif()

add_executable(kwin_x11 ${kwin_X11_SRCS} main_x11.cpp)
target_link_libraries(kwin_x11
  como::desktop-kde
  como::script
  como::x11
  KF6::Crash
)

install(TARGETS kwin_x11)

kcoreaddons_target_static_plugins(kwin_x11 NAMESPACE "kwin/effects/plugins")

add_executable(kwin_wayland main_wayland.cpp)
target_link_libraries(kwin_wayland
  como::desktop-kde
  como::script
  como::wayland
  como::xwayland
  KF6::DBusAddons
)

install(TARGETS kwin_wayland)
if (HAVE_LIBCAP)
    install(
    CODE "execute_process(
            COMMAND
                ${SETCAP_EXECUTABLE}
                CAP_SYS_NICE=+ep
                \$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/kwin_wayland)"
    )
endif()

kcoreaddons_target_static_plugins(kwin_wayland NAMESPACE "kwin/effects/plugins")

# Required for Plasma Wayland session. It expects a binary with this name to launch on startup.
add_custom_target(kwin_wayland_wrapper ALL COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    kwin_wayland ${CMAKE_CURRENT_BINARY_DIR}/bin/kwin_wayland_wrapper
)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/bin/kwin_wayland_wrapper
    DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
)
