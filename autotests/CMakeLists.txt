# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

Include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.3.1
)
FetchContent_MakeAvailable(Catch2)

remove_definitions(-DQT_USE_QSTRINGBUILDER)
add_subdirectory(libxrenderutils)
add_subdirectory(integration)
