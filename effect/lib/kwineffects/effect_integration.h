/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/types.h>

#include <QMatrix4x4>
#include <QRegion>
#include <chrono>
#include <functional>

namespace KWin
{

class Effect;
class EffectWindow;

namespace effect
{

template<typename UpdArg>
class win_integration
{
public:
    using update_function = std::function<void(UpdArg const&)>;
    virtual ~win_integration() = default;
    virtual void add(Effect& effect, update_function const& update) = 0;
    virtual void remove(Effect&) = 0;
};

struct update {
    EffectWindow* window{nullptr};
    bool valid{true};
};

template<typename Val>
struct value_update {
    update base;
    Val value;
};

using region_update = value_update<QRegion>;

struct color_update {
    update base;
    QRegion region;
    QMatrix4x4 color;
};

struct anim_update {
    update base;
    position location;
    std::chrono::milliseconds in;
    std::chrono::milliseconds out;
    double offset;
    double distance;
};

using region_integration = win_integration<region_update>;
using color_integration = win_integration<color_update>;
using anim_integration = win_integration<anim_update>;

}
}
