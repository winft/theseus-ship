# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(KWinFT VERSION 5.27.80)

set(QT_MIN_VERSION "6.4.0")
set(KF6_MIN_VERSION "5.240.0")
set(KDE_COMPILERSETTINGS_LEVEL "5.84")

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DQT_DISABLE_DEPRECATED_BEFORE=0
  -DQT_NO_KEYWORDS
  -DQT_USE_QSTRINGBUILDER
  -DQT_NO_URL_CAST_FROM_STRING
  -DQT_NO_CAST_TO_ASCII
)

# Prevent EGL headers from including platform headers, in particular Xlib.h.
add_definitions(-DMESA_EGL_NO_X11_HEADERS)
add_definitions(-DEGL_NO_X11)
add_definitions(-DEGL_NO_PLATFORM_SPECIFIC_TYPES)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)

# where to look first for cmake modules, before ${CMAKE_ROOT}/Modules/ is checked
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/cmake/modules ${ECM_MODULE_PATH})

include(FeatureSummary)
include(WriteBasicConfigVersionFile)
include(GenerateExportHeader)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Concurrent
    Core
    DBus
    Quick
    UiTools
    Widgets
)

find_package(Qt6Test ${QT_MIN_VERSION} CONFIG QUIET)
set_package_properties(Qt6Test PROPERTIES
    PURPOSE "Required for tests"
    TYPE OPTIONAL
)
add_feature_info("Qt6Test" Qt6Test_FOUND "Required for building tests")
if (NOT Qt6Test_FOUND)
    set(BUILD_TESTING OFF CACHE BOOL "Build the testing tree.")
endif()

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(ECMInstallIcons)
include(ECMOptionalAddSubdirectory)
include(ECMConfiguredInstall)

# required frameworks by Core
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Auth
    Config
    ConfigWidgets
    CoreAddons
    Crash
    GlobalAccel
    I18n
    IdleTime
    Notifications
    Package
    Plasma
    WidgetsAddons
    WindowSystem
)
# required frameworks by config modules
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Declarative
    KCMUtils
    KIO
    NewStuff
    Service
    XmlGui
)

# Required frameworks by Wayland binary
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    DBusAddons
)

# optional frameworks
find_package(KF6DocTools ${KF6_MIN_VERSION} CONFIG)
set_package_properties(KF6DocTools PROPERTIES
    PURPOSE "Enable building documentation"
    TYPE OPTIONAL
)
add_feature_info("KF6DocTools" KF6DocTools_FOUND "Enable building documentation")

find_package(KF6Kirigami2 ${KF6_MIN_VERSION} CONFIG)
set_package_properties(KF6Kirigami2 PROPERTIES
    DESCRIPTION "A QtQuick based components set"
    PURPOSE "Required at runtime for Virtual desktop KCM and the virtual keyboard"
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

find_package(Pixman)
set_package_properties(Pixman PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building wlroots backend."
)
find_package(wlroots 0.16.0)
set_package_properties(wlroots PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building wlroots backend."
)

if (${wlroots_VERSION} VERSION_GREATER_EQUAL 0.17)
  set(HAVE_WLR_SESSION_ON_AUTOCREATE 1)
endif()

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

# and the optional XCB dependencies
if (XCB_ICCCM_VERSION VERSION_LESS "0.4")
    set(XCB_ICCCM_FOUND FALSE)
endif()
add_feature_info("XCB-ICCCM" XCB_ICCCM_FOUND "Required for building test applications for KWin")

find_package(Freetype REQUIRED)
set_package_properties(Freetype PROPERTIES
    DESCRIPTION "A font rendering engine"
    URL "https://www.freetype.org"
    TYPE REQUIRED
    PURPOSE "Needed for KWin's QPA plugin."
)
find_package(Fontconfig REQUIRED)
set_package_properties(Fontconfig PROPERTIES
    TYPE REQUIRED
    PURPOSE "Needed for KWin's QPA plugin."
)

find_package(Wrapland REQUIRED)
set_package_properties(Wrapland PROPERTIES
    TYPE REQUIRED
    PURPOSE "Used as Wrapper library for Wayland protocol objects."
)

find_package(Xwayland)
set_package_properties(Xwayland PROPERTIES
    URL "https://x.org"
    DESCRIPTION "Xwayland X server"
    TYPE RUNTIME
    PURPOSE "Needed for running kwin_wayland"
)

find_package(Libcap)
set_package_properties(Libcap PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Needed for running kwin_wayland with real-time scheduling policy"
)
set(HAVE_LIBCAP ${Libcap_FOUND})

find_package(QAccessibilityClient6 CONFIG)
set_package_properties(QAccessibilityClient6 PROPERTIES
    URL "https://www.kde.org"
    DESCRIPTION "KDE client-side accessibility library"
    TYPE OPTIONAL
    PURPOSE "Required to enable accessibility features"
)
set(HAVE_ACCESSIBILITY ${QAccessibilityClient6_FOUND})

include(ECMFindQmlModule)
ecm_find_qmlmodule(QtQuick 2.3)
ecm_find_qmlmodule(QtQuick.Controls 1.2)
ecm_find_qmlmodule(QtQuick.Layouts 1.3)
ecm_find_qmlmodule(QtQuick.Window 2.1)
ecm_find_qmlmodule(QtMultimedia 5.0)
ecm_find_qmlmodule(org.kde.kquickcontrolsaddons 2.0)
ecm_find_qmlmodule(org.kde.plasma.core 2.0)
ecm_find_qmlmodule(org.kde.plasma.components 2.0)

########### configure tests ###############
option(KWIN_BUILD_DECORATIONS "Enable building of KWin decorations." ON)
option(KWIN_BUILD_KCMS "Enable building of KWin configuration modules." ON)
option(KWIN_BUILD_TABBOX "Enable building of KWin Tabbox functionality" ON)
option(KWIN_BUILD_PERF "Build internal tools for performance analysis at runtime." ON)

# Binary name of KWin
set(KWIN_NAME "kwin")
set(KWIN_INTERNAL_NAME_X11 "kwin_x11")
set(KWIN_INTERNAL_NAME_WAYLAND "kwin_wayland")
set(KWIN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(HAVE_PERF ${KWIN_BUILD_PERF})

include_directories(${XKB_INCLUDE_DIR})

set(HAVE_EPOXY_GLX ${epoxy_HAS_GLX})

# for things that are also used by kwin libraries
configure_file(lib/effect/kwinconfig.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/lib/effect/kwinconfig.h )

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckSymbolExists)

check_symbol_exists(SCHED_RESET_ON_FORK "sched.h" HAVE_SCHED_RESET_ON_FORK)
add_feature_info("SCHED_RESET_ON_FORK"
                 HAVE_SCHED_RESET_ON_FORK
                 "Required for running kwin_wayland with real-time scheduling")

configure_file(lib/cmake/config-kwin.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kwin.h)

########### global ###############
set(kwin_effects_dbus_xml ${CMAKE_CURRENT_SOURCE_DIR}/lib/render/dbus/org.kde.kwin.Effects.xml)
qt6_add_dbus_interface(effects_interface_SRCS ${kwin_effects_dbus_xml} kwineffects_interface)
add_library(KWinEffectsInterface STATIC ${effects_interface_SRCS})
target_link_libraries(KWinEffectsInterface Qt::DBus)
set_target_properties(KWinEffectsInterface PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

include_directories(BEFORE
    ${CMAKE_CURRENT_BINARY_DIR}/lib/effect
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/effect
)

add_subdirectory(lib)
if (KWIN_BUILD_KCMS)
    add_subdirectory(kcms)
endif()

include(ECMSetupQtPluginMacroNames)
ecm_setup_qtplugin_macro_names(
    JSON_ARG2
        "KWIN_EFFECT_FACTORY"
    JSON_ARG3
        "KWIN_EFFECT_FACTORY_ENABLED"
        "KWIN_EFFECT_FACTORY_SUPPORTED"
    JSON_ARG4
        "KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED"
    CONFIG_CODE_VARIABLE
        PACKAGE_SETUP_KWINEFFECTS_AUTOMOC_VARIABLES
)

########### next target ###############
add_executable(kwin_killer_helper lib/win/x11/helpers/killer.cpp)

target_link_libraries(kwin_killer_helper
  KF6::AuthCore
  KF6::I18n
  KF6::WidgetsAddons
  Qt::GuiPrivate
  Qt::Widgets
)
########### next target ###############

set(kwin_SRCS
    lib/base/dbus/kwin.cpp
    lib/base/os/clock/skew_notifier.cpp
    lib/base/os/clock/skew_notifier_engine.cpp
    lib/base/seat/session.cpp
    lib/base/seat/backend/logind/session.cpp
    lib/base/singleton_interface.cpp
    lib/base/x11/event_filter.cpp
    lib/base/x11/event_filter_container.cpp
    lib/base/x11/event_filter_manager.cpp
    lib/base/x11/grabs.cpp
    lib/base/x11/selection_owner.cpp
    lib/base/x11/xcb/extensions.cpp
    lib/base/logging.cpp
    lib/base/options.cpp
    lib/base/platform.cpp
    lib/desktop/screen_locker_watcher.cpp
    lib/render/post/color_correct_dbus_interface.cpp
    lib/render/post/suncalc.cpp
    lib/debug/console/console.cpp
    lib/debug/perf/ftrace.cpp
    lib/input/keyboard.cpp
    lib/input/keyboard_redirect.cpp
    lib/input/platform_qobject.cpp
    lib/input/redirect_qobject.cpp
    lib/input/singleton_interface.cpp
    lib/input/spies/modifier_only_shortcuts.cpp
    lib/input/switch.cpp
    lib/input/cursor.cpp
    lib/input/cursor_shape.cpp
    lib/input/gestures.cpp
    lib/input/global_shortcut.cpp
    lib/input/logging.cpp
    lib/input/pointer.cpp
    lib/input/xkb/keyboard.cpp
    lib/input/xkb/keymap.cpp
    main.cpp
    lib/render/compositor_qobject.cpp
    lib/render/dbus/compositing.cpp
    lib/render/effect/frame.cpp
    lib/render/effect_loader.cpp
    lib/render/effects.cpp
    lib/render/gl/context_attribute_builder.cpp
    lib/render/gl/egl_context_attribute_builder.cpp
    lib/render/outline.cpp
    lib/render/singleton_interface.cpp
    lib/render/window_thumbnail_item.cpp
    lib/render/x11/compositor_selection_owner.cpp
    lib/script/dbus_call.cpp
    lib/script/desktop_background_item.cpp
    lib/script/js_engine_global_methods_wrapper.cpp
    lib/script/screen_edge_handler.cpp
    lib/script/effect.cpp
    lib/script/output.cpp
    lib/script/platform.cpp
    lib/script/script.cpp
    lib/script/script_timer.cpp
    lib/script/scripting_logging.cpp
    lib/script/shortcut_handler.cpp
    lib/script/singleton_interface.cpp
    lib/script/utils.cpp
    lib/script/virtual_desktop_model.cpp
    lib/script/window.cpp
    lib/script/window_model.cpp
    lib/script/space.cpp
    lib/win/dbus/appmenu.cpp
    lib/win/dbus/virtual_desktop_manager.cpp
    lib/win/dbus/virtual_desktop_types.cpp
    lib/win/deco/palette.cpp
    lib/win/deco/decorations_logging.cpp
    lib/win/osd_notification.cpp
    lib/win/property_window.cpp
    lib/win/rules/book.cpp
    lib/win/rules/book_settings.cpp
    lib/win/rules/ruling.cpp
    lib/win/rules/window.cpp
    lib/win/session_manager.cpp
    lib/win/screen_edges.cpp
    lib/win/shortcut_dialog.cpp
    lib/win/singleton_interface.cpp
    lib/win/space_qobject.cpp
    lib/win/virtual_desktops.cpp
    lib/win/x11/client_machine.cpp
    lib/win/x11/extras.cpp
    lib/win/x11/key_server.cpp
    lib/win/x11/net/atoms.cpp
    lib/win/x11/net/root_info.cpp
    lib/win/x11/net/win_info.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    lib/base/output.h
    lib/render/cursor.h
    lib/win/deco/bridge_qobject.h
    lib/win/deco/client_impl_qobject.h
    lib/win/deco/renderer.h
    lib/win/stacking_order.h
    lib/win/window_qobject.h
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(kwin_SRCS
        ${kwin_SRCS}
        lib/base/os/clock/linux_skew_notifier_engine.cpp
    )
endif()

qt6_add_resources(kwin_SRCS lib/render/gl/resources.qrc)

qt6_add_dbus_adaptor(kwin_SRCS
  lib/script/org.kde.kwin.Script.xml
  lib/script/script.h
  KWin::scripting::abstract_script
)

kconfig_add_kcfg_files(kwin_SRCS lib/base/kconfig/options_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS lib/render/post/kconfig/color_correct_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS lib/win/rules/kconfig/rules_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS lib/win/rules/kconfig/rules_book_settings_base.kcfgc)

qt6_add_dbus_adaptor(kwin_SRCS lib/base/dbus/org.kde.KWin.xml lib/base/dbus/kwin.h KWin::base::dbus::kwin)
qt6_add_dbus_adaptor(kwin_SRCS
  lib/render/dbus/org.kde.kwin.Compositing.xml
  lib/render/dbus/compositing.h
  KWin::render::dbus::compositing_qobject
)
qt6_add_dbus_adaptor(kwin_SRCS
  lib/render/dbus/org.kde.kwin.ColorCorrect.xml
  lib/render/post/color_correct_dbus_interface.h
  KWin::render::post::color_correct_dbus_interface
)
qt6_add_dbus_adaptor(kwin_SRCS ${kwin_effects_dbus_xml} lib/render/effects.h KWin::render::effects_handler_wrap)
qt6_add_dbus_adaptor(kwin_SRCS
    lib/win/dbus/org.kde.KWin.VirtualDesktopManager.xml
    lib/win/dbus/virtual_desktop_manager.h
    KWin::win::dbus::virtual_desktop_manager
)
qt6_add_dbus_adaptor(kwin_SRCS
  lib/win/dbus/org.kde.KWin.Session.xml
  lib/win/session_manager.h
  KWin::win::session_manager
)

qt6_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/kf6_org.freedesktop.ScreenSaver.xml screenlocker_interface)
qt6_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/org.kde.screensaver.xml kscreenlocker_interface)
qt6_add_dbus_interface(kwin_SRCS lib/win/dbus/org.kde.kappmenu.xml appmenu_interface)

ki18n_wrap_ui(kwin_SRCS
    lib/debug/console/debug_console.ui
    lib/win/shortcut_dialog.ui
)

########### target link libraries ###############

set(kwin_OWN_LIBS
    kwineffects
)

set(kwin_QT_LIBS
    Qt::Concurrent
    Qt::DBus
    Qt::Quick
)

set(kwin_KDE_LIBS
  KF6::ConfigCore
  KF6::ConfigWidgets
  KF6::CoreAddons
  KF6::I18n
  KF6::Notifications
  KF6::Package
  KF6::Plasma
  KF6::Service

  KDecoration2::KDecoration
  KDecoration2::KDecoration2Private
)

set(kwin_X11_LIBS
  ${X11_LIBRARIES}
)

set(kwin_XCB_LIBS
    XCB::COMPOSITE
    XCB::DAMAGE
    XCB::GLX
    XCB::CURSOR
    XCB::ICCCM
    XCB::KEYSYMS
    XCB::RANDR
    XCB::RENDER
    XCB::SHAPE
    XCB::SHM
    XCB::SYNC
    XCB::XFIXES
    XCB::XKB
)

set(kwin_INPUT_LIBS
  XKB::XKB
)

set(kwinLibs
    ${kwin_OWN_LIBS}
    ${kwin_QT_LIBS}
    ${kwin_KDE_LIBS}
    ${kwin_X11_LIBS}
    ${kwin_XCB_LIBS}
    ${kwin_INPUT_LIBS}

    # For render code
    kwinxrenderutils
)

add_library(kwin SHARED ${kwin_SRCS})

set_target_properties(kwin PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
)

target_precompile_headers(kwin PRIVATE
    lib/cmake/precompiled/qt.h
    lib/cmake/precompiled/std.h
    lib/cmake/precompiled/xcb.h
)
target_link_libraries(kwin ${kwinLibs} kwinglutils epoxy::epoxy)

if(HAVE_PERF)
    target_sources(kwin PRIVATE lib/debug/perf/ftrace_impl.cpp)
endif()

if (KWIN_BUILD_TABBOX)
  target_sources(kwin PRIVATE
    lib/win/tabbox/tabbox_client_model.cpp
    lib/win/tabbox/tabbox_config.cpp
    lib/win/tabbox/tabbox_desktop_chain.cpp
    lib/win/tabbox/tabbox_desktop_model.cpp
    lib/win/tabbox/tabbox_handler.cpp
    lib/win/tabbox/tabbox_logging.cpp
    lib/win/tabbox/tabbox_switcher_item.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    lib/win/tabbox/tabbox.h
  )
  target_link_libraries(kwin Qt::GuiPrivate)
endif()

generate_export_header(kwin EXPORT_FILE_NAME kwin_export.h)

set(kwin_X11_SRCS
   lib/base/backend/x11/platform.cpp
   lib/base/backend/x11/randr_filter.cpp
   lib/base/x11/output.cpp
   lib/input/x11/cursor.cpp
   lib/input/x11/global_shortcuts_manager.cpp
   lib/input/x11/xfixes_cursor_event_filter.cpp
   lib/input/logging.cpp
   lib/render/backend/x11/non_composited_outline.cpp
)

if (HAVE_EPOXY_GLX)
    set(kwin_X11_SRCS
        ${kwin_X11_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/render/backend/x11/glx_context_attribute_builder.cpp
    )
endif()

include(ECMQtDeclareLoggingCategory)
ecm_qt_declare_logging_category(kwin_X11_SRCS
    HEADER
        x11_logging.h
    IDENTIFIER
        KWIN_X11
    CATEGORY_NAME
        kwin_x11
    DEFAULT_SEVERITY
        Critical
)

add_library(kwin_x11_lib STATIC
  ${kwin_X11_SRCS}
)

target_link_libraries(kwin_x11_lib
PUBLIC
  kwin
  KF6::Crash
  KF6::GlobalAccel
PRIVATE
  Qt::GuiPrivate
  X11::Xi
)

if (HAVE_DL_LIBRARY)
    target_link_libraries(kwin_x11_lib PRIVATE ${DL_LIBRARY})
endif()

add_executable(kwin_x11 ${kwin_X11_SRCS} main_x11.cpp)
target_link_libraries(kwin_x11
  kwin_x11_lib
  ${XKBX11_LIBRARIES}
)

install(TARGETS kwin ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS kwin_x11 ${INSTALL_TARGETS_DEFAULT_ARGS})
install(TARGETS kwin_killer_helper DESTINATION ${KDE_INSTALL_LIBEXECDIR})

set(kwin_XWAYLAND_SRCS
   lib/xwl/sources_ext.cpp
   lib/xwl/transfer.cpp

   # These headers need to be compiled for the helper QObjects they contain.
   lib/xwl/drag.h
   lib/xwl/selection_data.h
   lib/xwl/sources.h
   lib/xwl/wl_visit.h
   lib/xwl/x11_visit.h
)

add_subdirectory(plugins)

kcoreaddons_target_static_plugins(kwin_x11 "kwin/effects/plugins")

set(kwin_WAYLAND_SRCS
   lib/base/backend/wlroots/drm_lease.cpp
   lib/base/backend/wlroots/non_desktop_output.cpp
   lib/base/backend/wlroots/output.cpp
   lib/base/backend/wlroots/platform.cpp
   lib/base/seat/backend/wlroots/session.cpp
   lib/base/wayland/filtered_display.cpp
   lib/base/app_singleton.cpp
   lib/debug/console/wayland/input_device_model.cpp
   lib/debug/console/wayland/wayland_console.cpp
   lib/input/backend/wlroots/platform.cpp
   lib/input/backend/wlroots/control/headless/keyboard.cpp
   lib/input/backend/wlroots/control/keyboard.cpp
   lib/input/backend/wlroots/control/pointer.cpp
   lib/input/backend/wlroots/control/switch.cpp
   lib/input/backend/wlroots/control/touch.cpp
   lib/input/control/device.cpp
   lib/input/control/keyboard.cpp
   lib/input/control/pointer.cpp
   lib/input/control/switch.cpp
   lib/input/control/touch.cpp
   lib/input/dbus/device.cpp
   lib/input/dbus/device_manager.cpp
   lib/input/dbus/keyboard_layout.cpp
   lib/input/dbus/keyboard_layouts_v2.cpp
   lib/input/dbus/tablet_mode_manager.cpp
   lib/input/spies/keyboard_repeat.cpp
   lib/input/wayland/global_shortcuts_manager.cpp
   lib/input/logging.cpp
   lib/input/wayland/kglobalaccel/runtime/component.cpp
   lib/input/wayland/kglobalaccel/runtime/global_accel_d.cpp
   lib/input/wayland/kglobalaccel/runtime/global_shortcut.cpp
   lib/input/wayland/kglobalaccel/runtime/global_shortcut_context.cpp
   lib/input/wayland/kglobalaccel/runtime/global_shortcuts_registry.cpp
   lib/input/wayland/kglobalaccel/runtime/service_action_component.cpp
   lib/input/wayland/kglobalaccel/runtime/sequence_helpers.cpp
   lib/input/xkb/layout_manager.cpp
   lib/input/xkb/layout_policies.cpp

   # These headers need to be compiled for the helper QObjects they contain.
   lib/base/wayland/server.h
   lib/input/touch.h
   lib/input/wayland/cursor_image.h
   lib/input/wayland/cursor_theme.h
)

if (KWIN_BUILD_TABBOX)
  set(kwin_WAYLAND_SRCS
    ${kwin_WAYLAND_SRCS}
  )
endif()

ecm_qt_declare_logging_category(kwin_WAYLAND_SRCS
    HEADER
        wayland_logging.h
    IDENTIFIER
        KWIN_WL
    CATEGORY_NAME
        kwin_wl
    DEFAULT_SEVERITY
        Critical
)

add_library(kwin_wayland_lib SHARED
  ${kwin_XWAYLAND_SRCS}
  ${kwin_WAYLAND_SRCS}
)

target_link_libraries(kwin_wayland_lib
PUBLIC
  kwin
  KF6::GlobalAccel
  WraplandClient
  WraplandServer
  Wayland::Server
  Wayland::Cursor
  wlroots::wlroots
  Pixman::Pixman
  Libinput::Libinput
  Threads::Threads
  PW::KScreenLocker
)

kcoreaddons_target_static_plugins(
  kwin_wayland_lib "kwin/effects/plugins"
  LINK_OPTION "PRIVATE"
)

set_target_properties(kwin_wayland_lib PROPERTIES
   VERSION ${CMAKE_PROJECT_VERSION}
   SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin_wayland"
)
target_compile_definitions(kwin_wayland_lib PUBLIC WLR_USE_UNSTABLE)

add_executable(kwin_wayland main_wayland.cpp)
target_link_libraries(kwin_wayland
  kwin_wayland_lib
  KF6IdleTimeKWinPlugin
  KF6WindowSystemKWinPlugin
  KWinQpaPlugin
  KF6::Crash
  KF6::DBusAddons
)

install(TARGETS kwin_wayland_lib ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS kwin_wayland ${INSTALL_TARGETS_DEFAULT_ARGS})
if (HAVE_LIBCAP)
    install(
    CODE "execute_process(
            COMMAND
                ${SETCAP_EXECUTABLE}
                CAP_SYS_NICE=+ep
                \$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/kwin_wayland)"
    )
endif()

# Required for Plasma Wayland session. It expects a binary with this name to launch on startup.
add_custom_target(kwin_wayland_wrapper ALL COMMAND
    ${CMAKE_COMMAND} -E create_symlink
    kwin_wayland ${CMAKE_CURRENT_BINARY_DIR}/bin/kwin_wayland_wrapper
)

########### install files ###############

install(FILES lib/base/kconfig/options_settings.kcfg
  DESTINATION ${KDE_INSTALL_KCFGDIR}
  RENAME ${KWIN_NAME}.kcfg
)
install(FILES lib/render/post/kconfig/color_correct_settings.kcfg
    DESTINATION ${KDE_INSTALL_KCFGDIR}
    RENAME ${KWIN_NAME}_colorcorrect.kcfg
)
install(FILES lib/desktop/kde/kwin.notifyrc
  DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
  RENAME ${KWIN_NAME}.notifyrc
)
install(
  FILES
    lib/win/dbus/org.kde.KWin.VirtualDesktopManager.xml
    lib/base/dbus/org.kde.KWin.xml
    lib/render/dbus/org.kde.kwin.ColorCorrect.xml
    lib/render/dbus/org.kde.kwin.Compositing.xml
    lib/render/dbus/org.kde.kwin.Effects.xml

    # Still needs to be installed as plasma-workspace expects it at compile time.
    lib/data/org.kde.kwin.VirtualKeyboard.xml
  DESTINATION
    ${KDE_INSTALL_DBUSINTERFACEDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kwin_export.h DESTINATION ${KDE_INSTALL_INCLUDEDIR} COMPONENT Devel)

# Install QML files
install(DIRECTORY lib/render/qml/outline/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/outline)
install(DIRECTORY lib/render/qml/frames/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/frames)
install(
  DIRECTORY lib/win/qml/onscreennotification/plasma
  DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/onscreennotification
)

if (BUILD_TESTING)
    add_subdirectory(autotests)
    add_subdirectory(tests)
endif()

if (KF6DocTools_FOUND)
    add_subdirectory(docs)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(CMakePackageConfigHelpers)
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KWinDBusInterface")
configure_package_config_file(
  lib/desktop/kde/KWinDBusInterfaceConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/lib/desktop/kde/KWinDBusInterfaceConfig.cmake"
  PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
  INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/lib/desktop/kde/KWinDBusInterfaceConfig.cmake
  DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

ecm_install_configured_files(
  INPUT
    lib/desktop/kde/plasma-kwin_x11.service.in
    lib/desktop/kde/plasma-kwin_wayland.service.in
  @ONLY DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/bin/kwin_wayland_wrapper
    DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
)

qt6_generate_dbus_interface(lib/input/dbus/device.h org.kde.kwin.InputDevice.xml OPTIONS -A)

add_custom_target(KWinInputDBusInterfaces ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
  DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR}
)
