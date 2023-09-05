# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(contrast_SOURCES
    contrast.cpp
    contrastshader.cpp
    main.cpp
)
kwin_add_builtin_effect(contrast ${contrast_SOURCES})
target_link_libraries(contrast PRIVATE
  render
)
