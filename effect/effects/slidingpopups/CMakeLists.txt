# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

#######################################
# Effect

# Source files
set(slidingpopups_SOURCES
    main.cpp
    slidingpopups.cpp
)

kconfig_add_kcfg_files(slidingpopups_SOURCES
    slidingpopupsconfig.kcfgc
)

kwin4_add_effect_module(kwin4_effect_slidingpopups ${slidingpopups_SOURCES})
target_link_libraries(kwin4_effect_slidingpopups PRIVATE
  KF6::ConfigGui
  kwineffects
)
