include_directories(${KWIN_SOURCE_DIR})
add_definitions(-DKWIN_UNIT_TEST)
########################################################
# Test TabBox::ClientModel
########################################################
set(testTabBoxClientModel_SRCS
    ../../win/tabbox/tabbox_client_model.cpp
    ../../win/tabbox/tabbox_desktop_model.cpp
    ../../win/tabbox/tabbox_logging.cpp
    ../../win/tabbox/tabbox_config.cpp
    ../../win/tabbox/tabbox_handler.cpp
    mock_tabboxclient.cpp
    mock_tabboxhandler.cpp
    test_tabbox_clientmodel.cpp
)

ki18n_wrap_ui(testTabBoxClientModel_SRCS
    ../../win/shortcut_dialog.ui
)

add_executable(testTabBoxClientModel ${testTabBoxClientModel_SRCS})
set_target_properties(testTabBoxClientModel PROPERTIES COMPILE_DEFINITIONS "NO_NONE_WINDOW")
target_link_libraries(testTabBoxClientModel
    Qt::Core
    Qt::Concurrent
    Qt::DBus
    Qt::Quick
    Qt::Test
    Qt::Widgets
    Qt::X11Extras
    Qt::GuiPrivate

    KF5::ConfigCore
    KF5::ConfigWidgets
    KF5::GlobalAccel
    KF5::I18n
    KF5::Package
    KF5::Service
    KF5::WindowSystem

    KDecoration2::KDecoration

    XCB::XCB
)
add_test(NAME kwin-testTabBoxClientModel COMMAND testTabBoxClientModel)
ecm_mark_as_test(testTabBoxClientModel)

########################################################
# Test TabBox::TabBoxHandler
########################################################
set(testTabBoxHandler_SRCS
    ../../win/tabbox/tabbox_client_model.cpp
    ../../win/tabbox/tabbox_desktop_model.cpp
    ../../win/tabbox/tabbox_logging.cpp
    ../../win/tabbox/tabbox_config.cpp
    ../../win/tabbox/tabbox_handler.cpp
    mock_tabboxclient.cpp
    mock_tabboxhandler.cpp
    test_tabbox_handler.cpp
)

ki18n_wrap_ui(testTabBoxHandler_SRCS
    ../../win/shortcut_dialog.ui
)

add_executable(testTabBoxHandler ${testTabBoxHandler_SRCS})
set_target_properties(testTabBoxHandler PROPERTIES COMPILE_DEFINITIONS "NO_NONE_WINDOW")
target_link_libraries(testTabBoxHandler
    Qt::Core
    Qt::Concurrent
    Qt::DBus
    Qt::Quick
    Qt::Test
    Qt::Widgets
    Qt::X11Extras
    Qt::GuiPrivate

    KF5::ConfigCore
    KF5::ConfigWidgets
    KF5::GlobalAccel
    KF5::I18n
    KF5::Package
    KF5::Service
    KF5::WindowSystem

    KDecoration2::KDecoration

    XCB::XCB
)
add_test(NAME kwin-testTabBoxHandler COMMAND testTabBoxHandler)
ecm_mark_as_test(testTabBoxHandler)

########################################################
# Test TabBox::TabBoxConfig
########################################################
set(testTabBoxConfig_SRCS
    ../../win/tabbox/tabbox_logging.cpp
    ../../win/tabbox/tabbox_config.cpp
    test_tabbox_config.cpp
)

add_executable(testTabBoxConfig ${testTabBoxConfig_SRCS})
target_link_libraries(testTabBoxConfig Qt::Core Qt::Test)
add_test(NAME kwin-testTabBoxConfig COMMAND testTabBoxConfig)
ecm_mark_as_test(testTabBoxConfig)


########################################################
# Test TabBox::DesktopChainManager
########################################################
set(testDesktopChain_SRCS
    ../../win/tabbox/tabbox_desktop_chain.cpp
    ../../win/tabbox/tabbox_logging.cpp
    test_desktopchain.cpp
)

add_executable(testDesktopChain ${testDesktopChain_SRCS})
target_link_libraries(testDesktopChain Qt::Core Qt::Test)
add_test(NAME kwin-testDesktopChain COMMAND testDesktopChain)
ecm_mark_as_test(testDesktopChain)
