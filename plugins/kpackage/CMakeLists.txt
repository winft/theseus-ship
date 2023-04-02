# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

function(kwin_add_kpackage_structure dir file)
    kcoreaddons_add_plugin(kwin_${dir} SOURCES ${file} INSTALL_NAMESPACE kf6/packagestructure)
    target_link_libraries(kwin_${dir} KF6::Package)
endfunction()

kwin_add_kpackage_structure(aurorae aurorae/aurorae.cpp)
kwin_add_kpackage_structure(decoration decoration/decoration.cpp)
kwin_add_kpackage_structure(effect effect/effect.cpp)
kwin_add_kpackage_structure(scripts scripts/scripts.cpp)
kwin_add_kpackage_structure(windowswitcher windowswitcher/windowswitcher.cpp)
