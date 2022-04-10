cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(KWinFT)
set(PROJECT_VERSION "5.24.80")
set(PROJECT_VERSION_MAJOR 5)

set(CMAKE_C_STANDARD 99)

set(QT_MIN_VERSION "5.15.0")
set(KF5_MIN_VERSION "5.91")
set(KDE_COMPILERSETTINGS_LEVEL "5.84")

find_package(ECM ${KF5_MIN_VERSION} REQUIRED NO_MODULE)

include(FeatureSummary)
include(WriteBasicConfigVersionFile)
include(GenerateExportHeader)

# where to look first for cmake modules, before ${CMAKE_ROOT}/Modules/ is checked
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules ${ECM_MODULE_PATH})

find_package(Qt5 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Concurrent
    Core
    DBus
    Quick
    UiTools
    Widgets
    X11Extras
)

find_package(Qt5Test ${QT_MIN_VERSION} CONFIG QUIET)
set_package_properties(Qt5Test PROPERTIES
    PURPOSE "Required for tests"
    TYPE OPTIONAL
)
add_feature_info("Qt5Test" Qt5Test_FOUND "Required for building tests")
if (NOT Qt5Test_FOUND)
    set(BUILD_TESTING OFF CACHE BOOL "Build the testing tree.")
endif()

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

include(ECMInstallIcons)
include(ECMOptionalAddSubdirectory)
include(ECMConfiguredInstall)

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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# required frameworks by Core
find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS
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
    Wayland
    WidgetsAddons
    WindowSystem
)
# required frameworks by config modules
find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS
    Declarative
    KCMUtils
    KIO
    NewStuff
    Service
    XmlGui
)

# Required frameworks by Wayland binary
find_package(KF5 ${KF5_MIN_VERSION} REQUIRED COMPONENTS
    DBusAddons
)

# optional frameworks
find_package(KF5DocTools ${KF5_MIN_VERSION} CONFIG)
set_package_properties(KF5DocTools PROPERTIES
    PURPOSE "Enable building documentation"
    TYPE OPTIONAL
)
add_feature_info("KF5DocTools" KF5DocTools_FOUND "Enable building documentation")

find_package(KF5Kirigami2 ${KF5_MIN_VERSION} CONFIG)
set_package_properties(KF5Kirigami2 PROPERTIES
    DESCRIPTION "A QtQuick based components set"
    PURPOSE "Required at runtime for Virtual desktop KCM and the virtual keyboard"
    TYPE RUNTIME
)

find_package(KDecoration2 ${PROJECT_VERSION} CONFIG REQUIRED)

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

find_package(Qt5XkbCommonSupport REQUIRED)

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
find_package(wlroots 0.15.0)
set_package_properties(wlroots PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required for building wlroots backend."
)

if (${wlroots_VERSION} VERSION_GREATER_EQUAL 0.16)
  set(HAVE_WLR_BASE_INPUT_DEVICES 1)
endif()


find_package(X11)
set_package_properties(X11 PROPERTIES
    DESCRIPTION "X11 libraries"
    URL "https://www.x.org"
    TYPE REQUIRED
)
add_feature_info("XInput" X11_Xinput_FOUND "Required for poll-free mouse cursor updates")
set(HAVE_X11_XINPUT ${X11_Xinput_FOUND})

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
)
set_package_properties(XCB PROPERTIES TYPE REQUIRED)

# and the optional XCB dependencies
if (XCB_ICCCM_VERSION VERSION_LESS "0.4")
    set(XCB_ICCCM_FOUND FALSE)
endif()
add_feature_info("XCB-ICCCM" XCB_ICCCM_FOUND "Required for building test applications for KWin")

# dependencies for QPA plugin
find_package(Qt5FontDatabaseSupport REQUIRED)
find_package(Qt5ThemeSupport REQUIRED)
find_package(Qt5ServiceSupport REQUIRED)
find_package(Qt5EventDispatcherSupport REQUIRED)

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

find_package(QAccessibilityClient CONFIG)
set_package_properties(QAccessibilityClient PROPERTIES
    URL "https://www.kde.org"
    DESCRIPTION "KDE client-side accessibility library"
    TYPE OPTIONAL
    PURPOSE "Required to enable accessibility features"
)
set(HAVE_ACCESSIBILITY ${QAccessibilityClient_FOUND})

include(ECMQMLModules)
ecm_find_qmlmodule(QtQuick 2.3)
ecm_find_qmlmodule(QtQuick.Controls 1.2)
ecm_find_qmlmodule(QtQuick.Layouts 1.3)
ecm_find_qmlmodule(QtQuick.Window 2.1)
ecm_find_qmlmodule(QtMultimedia 5.0)
ecm_find_qmlmodule(org.kde.kquickcontrolsaddons 2.0)
ecm_find_qmlmodule(org.kde.plasma.core 2.0)
ecm_find_qmlmodule(org.kde.plasma.components 2.0)

########### configure tests ###############
include(CMakeDependentOption)

option(KWIN_BUILD_DECORATIONS "Enable building of KWin decorations." ON)
option(KWIN_BUILD_KCMS "Enable building of KWin configuration modules." ON)
option(KWIN_BUILD_TABBOX "Enable building of KWin Tabbox functionality" ON)
option(KWIN_BUILD_PERF "Build internal tools for performance analysis at runtime." ON)
option(KWIN_BUILD_XRENDER_COMPOSITING "Enable building of KWin with XRender Compositing support" ON)

# Binary name of KWin
set(KWIN_NAME "kwin")
set(KWIN_INTERNAL_NAME_X11 "kwin_x11")
set(KWIN_INTERNAL_NAME_WAYLAND "kwin_wayland")
set(KWIN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(HAVE_PERF ${KWIN_BUILD_PERF})

# KWIN_HAVE_XRENDER_COMPOSITING - whether XRender-based compositing support is available: may be disabled
if (KWIN_BUILD_XRENDER_COMPOSITING)
    set(KWIN_HAVE_XRENDER_COMPOSITING 1)
endif()

include_directories(${XKB_INCLUDE_DIR})

set(HAVE_EPOXY_GLX ${epoxy_HAS_GLX})

# for things that are also used by kwin libraries
configure_file(effect/lib/kwinconfig.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/effect/lib/kwinconfig.h )

include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckSymbolExists)

check_symbol_exists(SCHED_RESET_ON_FORK "sched.h" HAVE_SCHED_RESET_ON_FORK)
add_feature_info("SCHED_RESET_ON_FORK"
                 HAVE_SCHED_RESET_ON_FORK
                 "Required for running kwin_wayland with real-time scheduling")

configure_file(cmake/config-kwin.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-kwin.h)

########### global ###############
set(kwin_effects_dbus_xml ${CMAKE_CURRENT_SOURCE_DIR}/render/dbus/org.kde.kwin.Effects.xml)
qt5_add_dbus_interface(effects_interface_SRCS ${kwin_effects_dbus_xml} kwineffects_interface)
add_library(KWinEffectsInterface STATIC ${effects_interface_SRCS})
target_link_libraries(KWinEffectsInterface Qt::DBus)

include_directories(BEFORE
    ${CMAKE_CURRENT_BINARY_DIR}/effect/lib
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/effect/lib
)

add_subdirectory(effect)
if (KWIN_BUILD_KCMS)
    add_subdirectory(kcmkwin)
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
        PACKAGE_SETUP_AUTOMOC_VARIABLES
)

add_subdirectory(data)

add_subdirectory(scripts)
add_subdirectory(scripting)

########### next target ###############
add_executable(kwin_killer_helper win/x11/helpers/killer.cpp)

target_link_libraries(kwin_killer_helper
  KF5::AuthCore
  KF5::I18n
  KF5::WidgetsAddons
  Qt::Widgets
  Qt::X11Extras
)
########### next target ###############

set(kwin_SRCS
    base/dbus/kwin.cpp
    base/os/clock/skew_notifier.cpp
    base/os/clock/skew_notifier_engine.cpp
    base/seat/session.cpp
    base/seat/backend/logind/session.cpp
    base/x11/event_filter.cpp
    base/x11/event_filter_container.cpp
    base/x11/event_filter_manager.cpp
    base/x11/grabs.cpp
    base/x11/xcb/extensions.cpp
    base/logging.cpp
    base/options.cpp
    base/output.cpp
    base/platform.cpp
    desktop/screen_locker_watcher.cpp
    render/post/color_correct_dbus_interface.cpp
    render/post/night_color_manager.cpp
    render/post/suncalc.cpp
    debug/console/console.cpp
    debug/perf/ftrace.cpp
    decorations/decoratedclient.cpp
    decorations/decorationbridge.cpp
    decorations/decorationpalette.cpp
    decorations/decorationrenderer.cpp
    decorations/decorations_logging.cpp
    decorations/settings.cpp
    decorations/window.cpp
    input/device_redirect.cpp
    input/event_filter.cpp
    input/event_spy.cpp
    input/keyboard.cpp
    input/keyboard_redirect.cpp
    input/platform.cpp
    input/pointer_redirect.cpp
    input/redirect.cpp
    input/tablet_redirect.cpp
    input/touch_redirect.cpp
    input/spies/modifier_only_shortcuts.cpp
    input/cursor.cpp
    input/cursor_shape.cpp
    input/gestures.cpp
    input/global_shortcut.cpp
    input/global_shortcuts_manager.cpp
    input/logging.cpp
    input/pointer.cpp
    input/x11/redirect.cpp
    input/qt_event.cpp
    input/xkb/keyboard.cpp
    input/xkb/keymap.cpp
    input/xkb/manager.cpp
    main.cpp
    render/compositor.cpp
    render/cursor.cpp
    render/dbus/compositing.cpp
    render/effect_loader.cpp
    render/effects.cpp
    render/gl/backend.cpp
    render/gl/context_attribute_builder.cpp
    render/gl/deco_renderer.cpp
    render/gl/effect_frame.cpp
    render/gl/egl_context_attribute_builder.cpp
    render/gl/lanczos_filter.cpp
    render/gl/scene.cpp
    render/gl/shadow.cpp
    render/gl/texture.cpp
    render/gl/window.cpp
    render/outline.cpp
    render/platform.cpp
    render/scene.cpp
    render/shadow.cpp
    render/thumbnail_item.cpp
    render/window.cpp
    render/x11/compositor_selection_owner.cpp
    rules/rule_book.cpp
    rules/rule_book_settings.cpp
    rules/rules.cpp
    rules/window_rules.cpp
    scripting/dbus_call.cpp
    scripting/js_engine_global_methods_wrapper.cpp
    scripting/screen_edge_item.cpp
    scripting/effect.cpp
    scripting/platform.cpp
    scripting/script.cpp
    scripting/script_timer.cpp
    scripting/scripting_logging.cpp
    scripting/v2/client_model.cpp
    scripting/v3/client_model.cpp
    scripting/v3/virtual_desktop_model.cpp
    scripting/utils.cpp
    scripting/window.cpp
    scripting/space.cpp
    toplevel.cpp
    win/app_menu.cpp
    win/control.cpp
    win/dbus/virtual_desktop_manager.cpp
    win/dbus/virtual_desktop_types.cpp
    win/focus_chain.cpp
    win/internal_window.cpp
    win/osd_notification.cpp
    win/osd.cpp
    win/remnant.cpp
    win/session_manager.cpp
    win/screen_edges.cpp
    win/shortcut_dialog.cpp
    win/space.cpp
    win/stacking_order.cpp
    win/transient.cpp
    win/user_actions_menu.cpp
    win/virtual_desktops.cpp
    win/x11/client_machine.cpp
    win/x11/color_mapper.cpp
    win/x11/group.cpp
    win/x11/netinfo.cpp
    win/x11/root_info_filter.cpp
    win/x11/screen_edge.cpp
    win/x11/screen_edges_filter.cpp
    win/x11/space.cpp
    win/x11/stacking_tree.cpp
    win/x11/sync_alarm_filter.cpp
    win/x11/window.cpp
    xwl/xwayland_interface.cpp
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    set(kwin_SRCS
        ${kwin_SRCS}
        base/os/clock/linux_skew_notifier_engine.cpp
    )
endif()

qt5_add_resources(kwin_SRCS render/gl/resources.qrc)

qt5_add_dbus_adaptor(kwin_SRCS
  scripting/org.kde.kwin.Script.xml
  scripting/script.h
  KWin::scripting::abstract_script
)

kconfig_add_kcfg_files(kwin_SRCS base/kconfig/options_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS render/post/kconfig/color_correct_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS rules/kconfig/rule_settings.kcfgc)
kconfig_add_kcfg_files(kwin_SRCS rules/kconfig/rule_book_settings_base.kcfgc)

qt5_add_dbus_adaptor(kwin_SRCS base/dbus/org.kde.KWin.xml base/dbus/kwin.h KWin::base::dbus::kwin)
qt5_add_dbus_adaptor(kwin_SRCS
  render/dbus/org.kde.kwin.Compositing.xml
  render/dbus/compositing.h
  KWin::render::dbus::compositing
)
qt5_add_dbus_adaptor(kwin_SRCS
  render/dbus/org.kde.kwin.ColorCorrect.xml
  render/post/color_correct_dbus_interface.h
  KWin::render::post::color_correct_dbus_interface
)
qt5_add_dbus_adaptor(kwin_SRCS ${kwin_effects_dbus_xml} render/effects.h KWin::render::effects_handler_impl)
qt5_add_dbus_adaptor(kwin_SRCS
    win/dbus/org.kde.KWin.VirtualDesktopManager.xml
    win/dbus/virtual_desktop_manager.h
    KWin::win::dbus::virtual_desktop_manager
)
qt5_add_dbus_adaptor(kwin_SRCS
  win/dbus/org.kde.KWin.Session.xml
  win/session_manager.h
  KWin::win::session_manager
)

qt5_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/kf5_org.freedesktop.ScreenSaver.xml screenlocker_interface)
qt5_add_dbus_interface(kwin_SRCS ${KSCREENLOCKER_DBUS_INTERFACES_DIR}/org.kde.screensaver.xml kscreenlocker_interface)
qt5_add_dbus_interface(kwin_SRCS win/dbus/org.kde.kappmenu.xml appmenu_interface)

ki18n_wrap_ui(kwin_SRCS
    debug/console/debug_console.ui
    win/shortcut_dialog.ui
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
    KF5::ConfigCore
    KF5::ConfigWidgets
    KF5::CoreAddons
    KF5::GlobalAccel
    KF5::GlobalAccelPrivate
    KF5::I18n
    KF5::Notifications
    KF5::Package
    KF5::Plasma
    KF5::QuickAddons
    KF5::WindowSystem

    KDecoration2::KDecoration
    KDecoration2::KDecoration2Private
)

set(kwin_XLIB_LIBS
    ${X11_ICE_LIB}
    ${X11_SM_LIB}
    ${X11_X11_LIB}
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
)

set(kwin_INPUT_LIBS
    XKB::XKB
    Qt5::XkbCommonSupportPrivate
)

set(kwinLibs
    ${kwin_OWN_LIBS}
    ${kwin_QT_LIBS}
    ${kwin_KDE_LIBS}
    ${kwin_XLIB_LIBS}
    ${kwin_XCB_LIBS}
    ${kwin_INPUT_LIBS}

    # For render code
    kwinxrenderutils
)

add_library(kwin SHARED ${kwin_SRCS})

set_target_properties(kwin PROPERTIES
   VERSION ${PROJECT_VERSION}
   SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_precompile_headers(kwin PRIVATE
    cmake/precompiled/qt.h
    cmake/precompiled/std.h
    cmake/precompiled/xcb.h
)
target_link_libraries(kwin ${kwinLibs} kwinglutils epoxy::epoxy)

if(HAVE_PERF)
    target_sources(kwin PRIVATE debug/perf/ftrace_impl.cpp)
endif()

if (KWIN_BUILD_TABBOX)
  target_sources(kwin PRIVATE
    win/tabbox/tabbox.cpp
    win/tabbox/tabbox_client_model.cpp
    win/tabbox/tabbox_config.cpp
    win/tabbox/tabbox_desktop_chain.cpp
    win/tabbox/tabbox_desktop_model.cpp
    win/tabbox/tabbox_handler.cpp
    win/tabbox/tabbox_logging.cpp
    win/tabbox/tabbox_switcher_item.cpp
    win/tabbox/tabbox_x11_filter.cpp
  )
  target_link_libraries(kwin Qt::GuiPrivate)
endif()

generate_export_header(kwin EXPORT_FILE_NAME kwin_export.h)

set(kwin_X11_SRCS
   base/backend/x11/platform.cpp
   base/backend/x11/randr_filter.cpp
   base/x11/output.cpp
   debug/console/x11/x11_console.cpp
   input/x11/cursor.cpp
   input/x11/platform.cpp
   input/x11/window_selector.cpp
   input/x11/xfixes_cursor_event_filter.cpp
   input/logging.cpp
   render/backend/x11/deco_renderer.cpp
   render/backend/x11/non_composited_outline.cpp
   render/backend/x11/platform.cpp
   render/x11/compositor.cpp
   render/x11/effects.cpp
   render/x11/mouse_intercept_filter.cpp
   render/x11/overlay_window.cpp
)

if (KWIN_HAVE_XRENDER_COMPOSITING)
  set(kwin_X11_SRCS
    ${kwin_X11_SRCS}
    render/xrender/backend.cpp
    render/xrender/deco_renderer.cpp
    render/xrender/effect_frame.cpp
    render/xrender/scene.cpp
    render/xrender/shadow.cpp
    render/xrender/window.cpp
  )
endif()

if (HAVE_X11_XINPUT)
    set(kwin_X11_SRCS
        ${kwin_X11_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/input/x11/xinput_integration.cpp
    )
endif()

if (HAVE_EPOXY_GLX)
    set(kwin_X11_SRCS
        ${kwin_X11_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/render/backend/x11/glx_backend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/render/backend/x11/glx_context_attribute_builder.cpp
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
    KF5::Crash
PRIVATE
    Qt::X11Extras
)

if (HAVE_X11_XINPUT)
    target_link_libraries(kwin_x11_lib PRIVATE ${X11_Xinput_LIB})
endif()
if (HAVE_DL_LIBRARY)
    target_link_libraries(kwin_x11_lib PRIVATE ${DL_LIBRARY})
endif()

add_executable(kwin_x11 ${kwin_X11_SRCS} main_x11.cpp)
target_link_libraries(kwin_x11
  kwin_x11_lib
)
kcoreaddons_target_static_plugins(kwin_x11 "kwin/effects/plugins")

install(TARGETS kwin ${INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)
install(TARGETS kwin_x11 ${INSTALL_TARGETS_DEFAULT_ARGS})
install(TARGETS kwin_killer_helper DESTINATION ${KDE_INSTALL_LIBEXECDIR})

set(kwin_XWAYLAND_SRCS
   xwl/clipboard.cpp
   xwl/data_bridge.cpp
   xwl/dnd.cpp
   xwl/drag.cpp
   xwl/drag_wl.cpp
   xwl/drag_x.cpp
   xwl/primary_selection.cpp
   xwl/sources_ext.cpp
   xwl/transfer.cpp
   xwl/xwayland.cpp

   # These headers need to be compiled for the helper QObjects they contain.
   xwl/selection_data.h
   xwl/sources.h
)

ecm_qt_declare_logging_category(kwin_XWAYLAND_SRCS
    HEADER
        xwayland_logging.h
    IDENTIFIER
        KWIN_XWL
    CATEGORY_NAME
        kwin_xwl
    DEFAULT_SEVERITY
        Critical
)

add_subdirectory(plugins)

set(kwin_WAYLAND_SRCS
   base/backend/wlroots/drm_lease.cpp
   base/backend/wlroots/non_desktop_output.cpp
   base/backend/wlroots/output.cpp
   base/backend/wlroots/platform.cpp
   base/seat/backend/wlroots/session.cpp
   base/wayland/filtered_display.cpp
   base/wayland/idle_inhibition.cpp
   base/wayland/output.cpp
   base/wayland/server.cpp
   debug/console/wayland/input_device_model.cpp
   debug/console/wayland/input_filter.cpp
   debug/console/wayland/surface_tree_model.cpp
   debug/console/wayland/wayland_console.cpp
   input/backend/wlroots/keyboard.cpp
   input/backend/wlroots/platform.cpp
   input/backend/wlroots/pointer.cpp
   input/backend/wlroots/switch.cpp
   input/backend/wlroots/touch.cpp
   input/backend/wlroots/control/headless/keyboard.cpp
   input/backend/wlroots/control/keyboard.cpp
   input/backend/wlroots/control/pointer.cpp
   input/backend/wlroots/control/switch.cpp
   input/backend/wlroots/control/touch.cpp
   input/control/device.cpp
   input/control/keyboard.cpp
   input/control/pointer.cpp
   input/control/switch.cpp
   input/control/touch.cpp
   input/dbus/device.cpp
   input/dbus/device_manager.cpp
   input/dbus/keyboard_layout.cpp
   input/dbus/keyboard_layouts_v2.cpp
   input/dbus/tablet_mode_manager.cpp
   input/filters/decoration_event.cpp
   input/filters/dpms.cpp
   input/filters/drag_and_drop.cpp
   input/filters/effects.cpp
   input/filters/fake_tablet.cpp
   input/filters/forward.cpp
   input/filters/global_shortcut.cpp
   input/filters/helpers.cpp
   input/filters/internal_window.cpp
   input/filters/lock_screen.cpp
   input/filters/move_resize.cpp
   input/filters/popup.cpp
   input/filters/screen_edge.cpp
   input/filters/terminate_server.cpp
   input/filters/virtual_terminal.cpp
   input/filters/window_action.cpp
   input/filters/window_selector.cpp
   input/spies/keyboard_repeat.cpp
   input/spies/touch_hide_cursor.cpp
   input/spies/tablet_mode_switch.cpp
   input/wayland/cursor.cpp
   input/wayland/cursor_image.cpp
   input/wayland/cursor_theme.cpp
   input/wayland/fake/keyboard.cpp
   input/wayland/fake/pointer.cpp
   input/wayland/fake/touch.cpp
   input/wayland/input_method.cpp
   input/wayland/keyboard_redirect.cpp
   input/wayland/platform.cpp
   input/wayland/pointer_redirect.cpp
   input/wayland/redirect.cpp
   input/wayland/tablet_redirect.cpp
   input/wayland/touch_redirect.cpp
   input/logging.cpp
   input/switch.cpp
   input/touch.cpp
   input/xkb/layout_manager.cpp
   input/xkb/layout_policies.cpp
   render/gl/egl_dmabuf.cpp
   render/qpainter/deco_renderer.cpp
   render/qpainter/effect_frame.cpp
   render/qpainter/scene.cpp
   render/qpainter/shadow.cpp
   render/qpainter/window.cpp
   render/backend/wlroots/platform.cpp
   render/backend/wlroots/egl_backend.cpp
   render/backend/wlroots/egl_output.cpp
   render/backend/wlroots/egl_texture.cpp
   render/backend/wlroots/output.cpp
   render/backend/wlroots/qpainter_backend.cpp
   render/backend/wlroots/qpainter_output.cpp
   render/wayland/compositor.cpp
   render/wayland/effects.cpp
   render/wayland/output.cpp
   render/wayland/presentation.cpp
   win/wayland/space.cpp
   win/wayland/window.cpp
   win/wayland/xwl_window.cpp
)

if (KWIN_BUILD_TABBOX)
  set(kwin_WAYLAND_SRCS
    ${kwin_WAYLAND_SRCS}
    input/filters/tabbox.cpp
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
   VERSION ${PROJECT_VERSION}
   SOVERSION ${PROJECT_VERSION_MAJOR}
   OUTPUT_NAME "kwin_wayland"
)
target_compile_definitions(kwin_wayland_lib PUBLIC WLR_USE_UNSTABLE)

add_executable(kwin_wayland main_wayland.cpp)
target_link_libraries(kwin_wayland
    kwin_wayland_lib
    KF5GlobalAccelKWinPlugin
    KF5IdleTimeKWinPlugin
    KF5WindowSystemKWinPlugin
    KWinQpaPlugin
    KF5::Crash
    KF5::DBusAddons
)

if (HAVE_LIBCAP)
    target_link_libraries(kwin_wayland ${Libcap_LIBRARIES})
endif()

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

install(FILES base/kconfig/options_settings.kcfg
  DESTINATION ${KDE_INSTALL_KCFGDIR}
  RENAME ${KWIN_NAME}.kcfg
)
install(FILES render/post/kconfig/color_correct_settings.kcfg
    DESTINATION ${KDE_INSTALL_KCFGDIR}
    RENAME ${KWIN_NAME}_colorcorrect.kcfg
)
install(FILES desktop/kde/kwin.notifyrc
  DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
  RENAME ${KWIN_NAME}.notifyrc
)
install(
  FILES
    win/dbus/org.kde.KWin.VirtualDesktopManager.xml
    base/dbus/org.kde.KWin.xml
    render/dbus/org.kde.kwin.ColorCorrect.xml
    render/dbus/org.kde.kwin.Compositing.xml
    render/dbus/org.kde.kwin.Effects.xml

    # Still needs to be installed as plasma-workspace expects it at compile time.
    data/org.kde.kwin.VirtualKeyboard.xml
  DESTINATION
    ${KDE_INSTALL_DBUSINTERFACEDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kwin_export.h DESTINATION ${KDE_INSTALL_INCLUDEDIR} COMPONENT Devel)

# Install the KWin/Script service type
install(FILES scripting/kwinscript.desktop DESTINATION ${KDE_INSTALL_KSERVICETYPESDIR})

# Install the KWin/WindowSwitcher service type
install(FILES win/tabbox/kwinwindowswitcher.desktop DESTINATION ${KDE_INSTALL_KSERVICETYPESDIR})
install(FILES win/tabbox/kwindesktopswitcher.desktop DESTINATION ${KDE_INSTALL_KSERVICETYPESDIR})

# Install QML files
install(DIRECTORY render/qml/outline/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/outline)
install(
  DIRECTORY win/qml/onscreennotification/plasma
  DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/onscreennotification
)

if (BUILD_TESTING)
    add_subdirectory(autotests)
    add_subdirectory(tests)
endif()

if (KF5DocTools_FOUND)
    add_subdirectory(docs)
endif()

add_subdirectory(kconf_update)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(CMakePackageConfigHelpers)
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/KWinDBusInterface")
configure_package_config_file(
  desktop/kde/KWinDBusInterfaceConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/desktop/kde/KWinDBusInterfaceConfig.cmake"
  PATH_VARS KDE_INSTALL_DBUSINTERFACEDIR
  INSTALL_DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/desktop/kde/KWinDBusInterfaceConfig.cmake
  DESTINATION ${CMAKECONFIG_INSTALL_DIR}
)

ecm_install_configured_files(
  INPUT
    desktop/kde/plasma-kwin_x11.service.in
    desktop/kde/plasma-kwin_wayland.service.in
  @ONLY DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/bin/kwin_wayland_wrapper
    DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
)
