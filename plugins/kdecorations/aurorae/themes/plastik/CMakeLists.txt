# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

add_subdirectory(code)

install(DIRECTORY package/
        DESTINATION ${KDE_INSTALL_DATADIR}/${KWIN_NAME}/decorations/kwin4_decoration_qml_plastik)

file(COPY package/ DESTINATION ${CMAKE_BINARY_DIR}/bin/kwin/decorations/kwin4_decoration_qml_plastik)
