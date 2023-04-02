# SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later

function(kwin_add_package dir)
    kcoreaddons_add_plugin(kwin_${dir} SOURCES "${dir}/${dir}.cpp" INSTALL_NAMESPACE "kf6/packagestructure")
    target_link_libraries(kwin_${dir} KF6::Package)
endfunction()

kwin_add_package(aurorae)
kwin_add_package(decoration)
kwin_add_package(effect)
kwin_add_package(scripts)
kwin_add_package(windowswitcher)
