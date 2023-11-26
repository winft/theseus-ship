/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/singleton_interface.h>

#include <QObject>
#include <vector>

namespace KWin::base
{

template<typename Platform>
void platform_init(Platform& platform)
{
    QObject::connect(&platform, &Platform::output_added, &platform, [&platform](auto output) {
        if (!platform.topology.current) {
            platform.topology.current = output;
        }
    });
    QObject::connect(&platform, &platform::output_removed, &platform, [&platform](auto output) {
        if (output == platform.topology.current) {
            platform.topology.current = nullptr;
        }
    });

    singleton_interface::platform = &platform;
    singleton_interface::get_outputs = [&platform] {
        std::vector<base::output*> vec;
        for (auto&& output : platform.outputs) {
            vec.push_back(output);
        }
        return vec;
    };
}

}
