add_subdirectory(helper)

add_library(KWinIntegrationTestFramework SHARED
  lib/app.cpp
  lib/client.cpp
  lib/helpers.cpp
  generic_scene_opengl_test.cpp
)

target_link_libraries(KWinIntegrationTestFramework
PUBLIC
  kwin_wayland_lib
  Qt::Test
PRIVATE
  KF5::Crash
  # Static plugins
  KWinQpaPlugin
  KF5GlobalAccelKWinPlugin
  KF5WindowSystemKWinPlugin
  KF5IdleTimeKWinPlugin
)
target_compile_definitions(KWinIntegrationTestFramework PUBLIC WLR_USE_UNSTABLE)

function(integrationTest)
  set(options WAYLAND_ONLY)
  set(oneValueArgs NAME)
  set(multiValueArgs SRCS LIBS)
  cmake_parse_arguments(ARGS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_executable(${ARGS_NAME} ${ARGS_SRCS})
  target_link_libraries(${ARGS_NAME} KWinIntegrationTestFramework ${ARGS_LIBS})
  add_test(
    NAME kwin-${ARGS_NAME}
    COMMAND dbus-run-session ${CMAKE_BINARY_DIR}/bin/${ARGS_NAME}
  )

  if (${ARGS_WAYLAND_ONLY})
    add_executable(${ARGS_NAME}_waylandonly ${ARGS_SRCS})
    set_target_properties(${ARGS_NAME}_waylandonly PROPERTIES COMPILE_DEFINITIONS "NO_XWAYLAND")
    target_link_libraries(${ARGS_NAME}_waylandonly KWinIntegrationTestFramework ${ARGS_LIBS})
    add_test(
      NAME kwin-${ARGS_NAME}-waylandonly
      COMMAND dbus-run-session ${CMAKE_BINARY_DIR}/bin/${ARGS_NAME}_waylandonly
    )
  endif()
endfunction()

integrationTest(NAME testDontCrashGlxgears SRCS dont_crash_glxgears.cpp)
integrationTest(NAME testLockScreen SRCS lockscreen.cpp)
integrationTest(WAYLAND_ONLY NAME testScreens SRCS screens.cpp)

# TODO: The screen edges test could be run in wayland-only mode too but is at the moment too
#       unstable for that.
integrationTest(NAME testScreenEdges SRCS screen_edges.cpp)

integrationTest(WAYLAND_ONLY NAME testDecorationInput SRCS decoration_input_test.cpp)
integrationTest(WAYLAND_ONLY NAME testInternalWindow SRCS internal_window.cpp)
integrationTest(WAYLAND_ONLY NAME testTouchInput SRCS touch_input_test.cpp)
integrationTest(NAME testInputMethod SRCS input_method.cpp)
integrationTest(WAYLAND_ONLY NAME testInputStackingOrder SRCS input_stacking_order.cpp)
integrationTest(NAME testPointerInput SRCS pointer_input.cpp)
integrationTest(NAME testPlatformCursor SRCS platformcursor.cpp)
integrationTest(WAYLAND_ONLY NAME testDontCrashCancelAnimation SRCS dont_crash_cancel_animation.cpp)
integrationTest(WAYLAND_ONLY NAME testTransientPlacement SRCS transient_placement.cpp)
integrationTest(NAME testDebugConsole SRCS debug_console_test.cpp)
integrationTest(NAME testDontCrashEmptyDeco SRCS dont_crash_empty_deco.cpp)
integrationTest(WAYLAND_ONLY NAME testPlasmaSurface SRCS plasma_surface_test.cpp)
integrationTest(NAME testLayerShell SRCS layer_shell.cpp)
integrationTest(WAYLAND_ONLY NAME testMaximized SRCS maximize_test.cpp)
integrationTest(WAYLAND_ONLY NAME testXdgShellClient SRCS xdgshellclient_test.cpp)
integrationTest(WAYLAND_ONLY NAME testDontCrashNoBorder SRCS dont_crash_no_border.cpp)
integrationTest(NAME testXwaylandSelections SRCS xwayland_selections_test.cpp)
integrationTest(WAYLAND_ONLY NAME testSceneOpenGL SRCS scene_opengl_test.cpp )
integrationTest(WAYLAND_ONLY NAME testSceneOpenGLShadow SRCS scene_opengl_shadow_test.cpp)
integrationTest(WAYLAND_ONLY NAME testNoXdgRuntimeDir SRCS no_xdg_runtime_dir_test.cpp)
integrationTest(WAYLAND_ONLY NAME testScreenChanges SRCS screen_changes_test.cpp)
integrationTest(NAME testModifierOnlyShortcut SRCS modifier_only_shortcut_test.cpp)
integrationTest(WAYLAND_ONLY NAME testTabBox SRCS tabbox_test.cpp)
integrationTest(WAYLAND_ONLY NAME testWindowSelection SRCS window_selection_test.cpp)
integrationTest(WAYLAND_ONLY NAME testPointerConstraints SRCS pointer_constraints_test.cpp)
integrationTest(WAYLAND_ONLY NAME testKeyboardLayout SRCS keyboard_layout_test.cpp)
integrationTest(WAYLAND_ONLY NAME testKeyboardKeymap SRCS keyboard_keymap.cpp)
integrationTest(WAYLAND_ONLY NAME testKeymapCreationFailure SRCS keymap_creation_failure_test.cpp)
integrationTest(WAYLAND_ONLY NAME testShowingDesktop SRCS showing_desktop_test.cpp)
integrationTest(WAYLAND_ONLY NAME testDontCrashUseractionsMenu SRCS dont_crash_useractions_menu.cpp)
integrationTest(WAYLAND_ONLY NAME testBindings SRCS bindings_test.cpp)
integrationTest(WAYLAND_ONLY NAME testVirtualDesktop SRCS virtual_desktop_test.cpp)
integrationTest(NAME testVirtualKeyboard SRCS virtual_keyboard.cpp)
integrationTest(WAYLAND_ONLY NAME testXdgShellClientRules SRCS xdgshellclient_rules_test.cpp)
integrationTest(WAYLAND_ONLY NAME testIdleInhibition SRCS idle_inhibition_test.cpp)
integrationTest(WAYLAND_ONLY NAME testColorCorrectNightColor SRCS colorcorrect_nightcolor_test.cpp)
integrationTest(WAYLAND_ONLY NAME testDontCrashCursorPhysicalSizeEmpty SRCS dont_crash_cursor_physical_size_empty.cpp)
integrationTest(WAYLAND_ONLY NAME testDontCrashReinitializeCompositor SRCS dont_crash_reinitialize_compositor.cpp)
integrationTest(WAYLAND_ONLY NAME testNoGlobalShortcuts SRCS no_global_shortcuts_test.cpp)
integrationTest(WAYLAND_ONLY NAME testBufferSizeChange SRCS buffer_size_change_test.cpp )
integrationTest(WAYLAND_ONLY NAME testPlacement SRCS placement_test.cpp)
integrationTest(WAYLAND_ONLY NAME testActivation SRCS activation_test.cpp)
integrationTest(WAYLAND_ONLY NAME testXdgActivation SRCS xdg_activation.cpp)

if (XCB_ICCCM_FOUND)
    integrationTest(NAME testMoveResize SRCS move_resize_window_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testStruts SRCS struts_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testDontCrashAuroraeDestroyDeco SRCS dont_crash_aurorae_destroy_deco.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testPlasmaWindow SRCS plasmawindow_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testScreenEdgeClientShow SRCS screenedge_client_show_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testX11DesktopWindow SRCS desktop_window_x11_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testXwaylandInput SRCS xwayland_input_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testWindowRules SRCS window_rules_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testX11Client SRCS x11_client_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testQuickTiling SRCS quick_tiling_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testGlobalShortcuts SRCS globalshortcuts_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testSceneQPainter SRCS scene_qpainter_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testSceneQPainterShadow SRCS scene_qpainter_shadow_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testStackingOrder SRCS stacking_order_test.cpp LIBS XCB::ICCCM)
    integrationTest(NAME testDbusInterface SRCS dbus_interface_test.cpp LIBS XCB::ICCCM)
endif()

add_subdirectory(scripting)
add_subdirectory(effects)
add_subdirectory(fakes)
