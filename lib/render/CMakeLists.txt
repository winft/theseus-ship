# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

qt6_add_dbus_interface(effects_interface_src effect/interface/org.kde.kwin.Effects.xml kwineffects_interface)

add_library(effects-interface STATIC ${effects_interface_src})
target_link_libraries(effects-interface
  Qt::DBus
  base
)
set_target_properties(effects-interface PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_library(render SHARED)

target_link_libraries(render
  PUBLIC
    KF6::Notifications
    epoxy::epoxy
    win
)

target_sources(render
  PUBLIC
    FILE_SET HEADERS
    FILES
      dbus/compositing.h
      effect/basic_effect_loader.h
      effect/contrast_update.h
      effect/effect_load_queue.h
      effect/frame.h
      effect/integration.h
      effect/interface/animation_effect.h
      effect/interface/offscreen_effect.h
      effect/interface/effect.h
      effect/interface/effect_frame.h
      effect/interface/effect_integration.h
      effect/interface/effect_plugin_factory.h
      effect/interface/effect_screen.h
      effect/interface/effect_togglable_state.h
      effect/interface/effect_window.h
      effect/interface/effects_handler.h
      effect/interface/motions.h
      effect/interface/offscreen_quick_view.h
      effect/interface/paint_clipper.h
      effect/interface/paint_data.h
      effect/interface/quick_scene.h
      effect/interface/time_line.h
      effect/interface/types.h
      effect/interface/window_quad.h
      effect/internal_win_properties.h
      effect/internal_win_update.h
      effect/screen_impl.h
      effect/setup_handler.h
      effect/setup_window.h
      effect/window_group_impl.h
      effect/window_impl.h
      gl/backend.h
      gl/buffer.h
      gl/context_attribute_builder.h
      gl/deco_renderer.h
      gl/egl.h
      gl/egl_context_attribute_builder.h
      gl/egl_data.h
      gl/gl.h
      gl/interface/framebuffer.h
      gl/interface/platform.h
      gl/interface/shader.h
      gl/interface/shader_manager.h
      gl/interface/texture.h
      gl/interface/utils.h
      gl/interface/utils_funcs.h
      gl/interface/vertex_buffer.h
      gl/lanczos_filter.h
      gl/scene.h
      gl/shadow.h
      gl/texture.h
      gl/timer_query.h
      gl/window.h
      interface/framebuffer.h
      post/color_correct_dbus_interface.h
      post/constants.h
      post/night_color_data.h
      post/night_color_manager.h
      post/night_color_setup.h
      post/suncalc.h
      buffer.h
      compositor.h
      compositor_qobject.h
      compositor_start.h
      cursor.h
      deco_shadow.h
      effects.h
      effect_loader.h
      options.h
      outline.h
      scene.h
      shadow.h
      shortcuts_init.h
      singleton_interface.h
      types.h
      window.h
  PRIVATE
    effect/interface/anidata.cpp
    effect/interface/animation_effect.cpp
    effect/interface/offscreen_effect.cpp
    effect/interface/effect.cpp
    effect/interface/effect_frame.cpp
    effect/interface/effect_plugin_factory.cpp
    effect/interface/effect_screen.cpp
    effect/interface/effect_togglable_state.cpp
    effect/interface/effect_window.cpp
    effect/interface/effects_handler.cpp
    effect/interface/offscreen_quick_view.cpp
    effect/interface/quick_scene.cpp
    effect/interface/motions.cpp
    effect/interface/paint_clipper.cpp
    effect/interface/time_line.cpp
    effect/interface/window_quad.cpp
    post/color_correct_dbus_interface.cpp
    post/suncalc.cpp
    compositor_qobject.cpp
    dbus/compositing.cpp
    effect/basic_effect_loader.cpp
    effect/frame.cpp
    effect_loader.cpp
    effects.cpp
    gl/context_attribute_builder.cpp
    gl/egl_context_attribute_builder.cpp
    gl/interface/framebuffer.cpp
    gl/interface/platform.cpp
    gl/interface/shader.cpp
    gl/interface/shader_manager.cpp
    gl/interface/texture.cpp
    gl/interface/utils.cpp
    gl/interface/utils_funcs.cpp
    gl/interface/vertex_buffer.cpp
    options.cpp
    outline.cpp
    singleton_interface.cpp

    # These headers need to be compiled for the helper QObjects they contain.
    cursor.h
)

qt6_add_resources(render gl/resources.qrc)
kconfig_add_kcfg_files(render config/render_settings.kcfgc)
kconfig_add_kcfg_files(render post/kconfig/color_correct_settings.kcfgc)

qt6_add_dbus_adaptor(render_dbus_SRCS
  dbus/org.kde.kwin.Compositing.xml
  dbus/compositing.h
  KWin::render::dbus::compositing_qobject
)
qt6_add_dbus_adaptor(render_dbus_SRCS
  dbus/org.kde.kwin.ColorCorrect.xml
  post/color_correct_dbus_interface.h
  KWin::render::post::color_correct_dbus_interface
)
qt6_add_dbus_adaptor(render_dbus_SRCS
  effect/interface/org.kde.kwin.Effects.xml
  effects.h
  KWin::render::effects_handler_wrap
)

target_sources(render
  PRIVATE
    ${render_dbus_SRCS}
)

add_library(render-x11 SHARED)

target_link_libraries(render-x11
  PUBLIC
    base-x11
    render
  PRIVATE
    XCB::IMAGE
)

target_sources(render-x11
  PUBLIC
    FILE_SET HEADERS
    FILES
      x11/compositor_selection_owner.h
      x11/effect/setup_handler.h
      x11/effect/setup_window.h
      x11/keyboard_intercept_filter.h
      x11/mouse_intercept_filter.h
      x11/property_notify_filter.h
      x11/shadow.h
      x11/support_properties.h
      x11/types.h
  PRIVATE
    x11/compositor_selection_owner.cpp
)

add_library(render-x11-backend SHARED)

target_link_libraries(render-x11-backend
  PUBLIC
    base-x11
    render-x11
  PRIVATE
    XCB::IMAGE
)

target_sources(render-x11-backend
  PUBLIC
    FILE_SET HEADERS
    FILES
      backend/x11/non_composited_outline.h
      x11/buffer.h
      x11/compositor_start.h
      x11/effect.h
      x11/effects.h
      x11/overlay_window.h
      x11/platform.h
      x11/sync.h
      xrender/utils.h
  PRIVATE
    backend/x11/glx_context_attribute_builder.cpp
    backend/x11/non_composited_outline.cpp
    xrender/utils.cpp
)

add_library(render-wl SHARED)

target_link_libraries(render-wl
  PUBLIC
    base-wl
    render
)

target_sources(render-wl
  PUBLIC
    FILE_SET HEADERS
    FILES
      qpainter/backend.h
      qpainter/buffer.h
      qpainter/deco_renderer.h
      qpainter/scene.h
      qpainter/shadow.h
      qpainter/window.h
      wayland/effect/blur_integration.h
      wayland/effect/blur_update.h
      wayland/effect/contrast_integration.h
      wayland/effect/contrast_update.h
      wayland/effect/slide_integration.h
      wayland/effect/slide_update.h
      wayland/effect/update.h
      wayland/effect/xwayland.h
      wayland/buffer.h
      wayland/duration_record.h
      wayland/effects.h
      wayland/egl.h
      wayland/egl_data.h
      wayland/output.h
      wayland/presentation.h
      wayland/setup_handler.h
      wayland/setup_window.h
      wayland/shadow.h
      wayland/utils.h
      wayland/xwl_effects.h
      wayland/xwl_platform.h
)

set_target_properties(render render-x11 render-x11-backend render-wl PROPERTIES
  VERSION ${CMAKE_PROJECT_VERSION}
  SOVERSION ${CMAKE_PROJECT_VERSION_MAJOR}
  PREFIX libkwinft-
)

install(
  FILES post/kconfig/color_correct_settings.kcfg
  DESTINATION ${KDE_INSTALL_KCFGDIR}
  RENAME ${KWIN_NAME}_colorcorrect.kcfg
)
install(
  FILES
    dbus/org.kde.kwin.ColorCorrect.xml
    dbus/org.kde.kwin.Compositing.xml
  DESTINATION
    ${KDE_INSTALL_DBUSINTERFACEDIR}
)

install(DIRECTORY qml/outline/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/outline)
install(DIRECTORY qml/frames/plasma DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/frames)

install(TARGETS render render-x11 render-x11-backend render-wl
  EXPORT kwinft-export
  LIBRARY NAMELINK_SKIP
  FILE_SET HEADERS DESTINATION ${KDE_INSTALL_INCLUDEDIR}/kwinft/render
)
