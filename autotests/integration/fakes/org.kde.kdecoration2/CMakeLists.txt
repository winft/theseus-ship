# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

########################################################
# FakeDecoWithShadows
########################################################
add_library(org.kde.test.fakedecowithshadows MODULE fakedecoration_with_shadows.cpp)
set_target_properties(org.kde.test.fakedecowithshadows PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/fakes/org.kde.kdecoration2")
target_link_libraries(org.kde.test.fakedecowithshadows
    PUBLIC
        Qt::Core
        Qt::Gui
    PRIVATE
        KDecoration2::KDecoration
        KF6::CoreAddons
)

