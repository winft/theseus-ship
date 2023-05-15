# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(QPA_SOURCES
    abstractplatformcontext.cpp
    backingstore.cpp
    eglhelpers.cpp
    integration.cpp
    main.cpp
    offscreensurface.cpp
    platformcursor.cpp
    screen.cpp
    sharingplatformcontext.cpp
    window.cpp
)

include(ECMQtDeclareLoggingCategory)
ecm_qt_declare_logging_category(QPA_SOURCES HEADER logging.h IDENTIFIER KWIN_QPA CATEGORY_NAME kwin_qpa_plugin DEFAULT_SEVERITY Critical)

add_library(KWinQpaPlugin OBJECT ${QPA_SOURCES})
target_compile_definitions(KWinQpaPlugin PRIVATE QT_STATICPLUGIN)

target_link_libraries(KWinQpaPlugin PRIVATE
    Qt::CorePrivate
    Qt::GuiPrivate
    Freetype::Freetype # Must be after Qt platform support libs
    Fontconfig::Fontconfig
    win
)
