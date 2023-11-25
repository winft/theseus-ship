# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_subdirectory(helper)
add_subdirectory(fakes)

add_executable(tests
  lib/client.cpp
  lib/helpers.cpp
  lib/main.cpp
  lib/setup.cpp
  # integration tests
  activation.cpp
  bindings.cpp
  buffer_size_change.cpp
  client_machine.cpp
  night_color.cpp
  dbus_interface.cpp
  debug_console.cpp
  decoration_input.cpp
  desktop_window_x11.cpp
  no_crash_aurorae_destroy_deco.cpp
  no_crash_cancel_animation.cpp
  no_crash_cursor_physical_size_empty.cpp
  no_crash_empty_deco.cpp
  no_crash_glxgears.cpp
  no_crash_no_border.cpp
  no_crash_reinitialize_compositor.cpp
  no_crash_useractions_menu.cpp
  gestures.cpp
  global_shortcuts.cpp
  idle_inhibition.cpp
  idle.cpp
  input_method.cpp
  input_stacking_order.cpp
  internal_window.cpp
  keyboard_keymap.cpp
  keyboard_layout.cpp
  keymap_creation_failure.cpp
  layer_shell.cpp
  lockscreen.cpp
  maximize.cpp
  modifier_only_shortcut.cpp
  move_resize_window.cpp
  no_global_shortcuts.cpp
  no_xdg_runtime_dir.cpp
  placement.cpp
  plasma_surface.cpp
  plasma_window.cpp
  platform_cursor.cpp
  pointer_constraints.cpp
  pointer_input.cpp
  quick_tiling.cpp
  opengl_shadow.cpp
  scene_opengl.cpp
  qpainter_shadow.cpp
  scene_qpainter.cpp
  screen_changes.cpp
  screen_edges.cpp
  screen_edge_window_show.cpp
  screens.cpp
  showing_desktop.cpp
  stacking_order.cpp
  struts.cpp
  subspace.cpp
  tabbox.cpp
  touch_input.cpp
  transient_placement.cpp
  virtual_keyboard.cpp
  window_rules.cpp
  window_selection.cpp
  x11_client.cpp
  xcb_size_hints.cpp
  xcb_wrapper.cpp
  xdg_activation.cpp
  xdg-shell_rules.cpp
  xdg-shell_window.cpp
  xwayland_input.cpp
  xwayland_selections.cpp
  # effect tests
  effects/fade.cpp
  effects/maximize_animation.cpp
  effects/minimize_animation.cpp
  effects/popup_open_close_animation.cpp
  effects/scripted_effects.cpp
  effects/slidingpopups.cpp
  effects/subspace_switching_animation.cpp
  effects/window_open_close_animation.cpp
  effects/translucency.cpp
  # scripting tests
  scripting/minimize_all.cpp
  scripting/screen_edge.cpp
  # unit tests
  ../libkwineffects/opengl_platform.cpp
  ../libkwineffects/timeline.cpp
  ../libkwineffects/window_quad_list.cpp
  ../on_screen_notifications.cpp
  ../opengl_context_attribute_builder.cpp
  ../tabbox/tabbox_client_model.cpp
  ../tabbox/tabbox_config.cpp
  ../tabbox/tabbox_handler.cpp
  ../gestures.cpp
  ../xcb_window.cpp
  ../xkb.cpp
  # unit tests support
  ../libkwineffects/mock_gl.cpp
  ../tabbox/mock_tabbox_client.cpp
  ../tabbox/mock_tabbox_handler.cpp
  ../../lib/render/backend/x11/glx_context_attribute_builder.cpp
)

target_compile_definitions(tests PRIVATE USE_XWL=1)

target_link_libraries(tests
PRIVATE
  desktop-kde
  kwinft::wayland
  kwinft::xwayland
  script
  Qt::Test
  Catch2::Catch2WithMain
  KF6::Crash
  KF6::WindowSystem
  WraplandClient
  # Static plugins
  KWinQpaPlugin
  KF6WindowSystemKWinPlugin
  KF6IdleTimeKWinPlugin
)

add_executable(tests-wl
  lib/client.cpp
  lib/helpers.cpp
  lib/main.cpp
  lib/setup.cpp
  # integration tests
  activation.cpp
  bindings.cpp
  buffer_size_change.cpp
  decoration_input.cpp
  gestures.cpp
  idle.cpp
  idle_inhibition.cpp
  input_method.cpp
  input_stacking_order.cpp
  internal_window.cpp
  keyboard_keymap.cpp
  keyboard_layout.cpp
  keymap_creation_failure.cpp
  layer_shell.cpp
  maximize.cpp
  modifier_only_shortcut.cpp
  night_color.cpp
  no_crash_cancel_animation.cpp
  no_crash_cursor_physical_size_empty.cpp
  no_crash_no_border.cpp
  no_crash_reinitialize_compositor.cpp
  no_crash_useractions_menu.cpp
  no_global_shortcuts.cpp
  no_xdg_runtime_dir.cpp
  opengl_shadow.cpp
  placement.cpp
  plasma_surface.cpp
  platform_cursor.cpp
  pointer_constraints.cpp
  pointer_input.cpp
  qpainter_shadow.cpp
  scene_opengl.cpp
  screen_changes.cpp
  screens.cpp
  showing_desktop.cpp
  subspace.cpp
  tabbox.cpp
  touch_input.cpp
  transient_placement.cpp
  virtual_keyboard.cpp
  window_selection.cpp
  xdg-shell_rules.cpp
  xdg-shell_window.cpp
  xdg_activation.cpp
  # effect tests
  effects/fade.cpp
  effects/maximize_animation.cpp
  effects/minimize_animation.cpp
  effects/popup_open_close_animation.cpp
  effects/scripted_effects.cpp
  effects/subspace_switching_animation.cpp
  effects/window_open_close_animation.cpp
  # scripting tests
  scripting/minimize_all.cpp
  scripting/screen_edge.cpp
)

target_compile_definitions(tests-wl PRIVATE USE_XWL=0)

target_link_libraries(tests-wl
PRIVATE
  desktop-kde
  kwinft::wayland
  script
  Qt::Test
  Catch2::Catch2WithMain
  KF6::Crash
  KF6::WindowSystem
  WraplandClient
  # Static plugins
  KWinQpaPlugin
  KF6WindowSystemKWinPlugin
  KF6IdleTimeKWinPlugin
)

include(Catch)
catch_discover_tests(tests)
catch_discover_tests(tests-wl TEST_SUFFIX " (wl)")
