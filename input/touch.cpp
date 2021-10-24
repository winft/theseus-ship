/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "../platform.h"
#include "abstract_wayland_output.h"
#include "main.h"
#include "screens.h"

#include <cmath>

namespace KWin::input
{

Qt::ScreenOrientation to_qt_orientation(base::wayland::output_transform transform)
{
    using Tr = base::wayland::output_transform;

    // TODO(romangg): Are flipped cases different?
    switch (transform) {
    case Tr::rotated_90:
    case Tr::flipped_90:
        return Qt::PortraitOrientation;
    case Tr::rotated_180:
    case Tr::flipped_180:
        return Qt::InvertedLandscapeOrientation;
    case Tr::rotated_270:
    case Tr::flipped_270:
        return Qt::InvertedPortraitOrientation;
    default:
        return Qt::PrimaryOrientation;
    }
}

touch::touch(platform* plat, QObject* parent)
    : QObject(parent)
    , plat{plat}
{
    connect(screens(), &Screens::changed, this, [this] {
        if (!control) {
            return;
        }
        output = get_output();
        if (output) {
            control->set_orientation(to_qt_orientation(output->transform()));
        }
    });
}

touch::~touch()
{
}

AbstractWaylandOutput* touch::get_output() const
{
    if (!control) {
        return nullptr;
    }

    auto const& outputs = kwinApp()->platform->enabledOutputs();
    if (outputs.empty()) {
        // Might be too early.
        return nullptr;
    }

    if (outputs.size() == 1) {
        return static_cast<AbstractWaylandOutput*>(outputs.front());
    }

    // First try by name.
    if (auto name = control->output_name(); !name.empty()) {
        for (auto& output : outputs) {
            auto wl_out = static_cast<AbstractWaylandOutput*>(output);
            if (wl_out->name() == name.c_str()) {
                return wl_out;
            }
        }
    }

    auto check_dimensions = [this](auto const& output) {
        auto const& size = control->size();
        auto const& out_size = output->physicalSize();
        return std::round(size.width()) == std::round(out_size.width())
            && std::round(size.height()) == std::round(out_size.height());
    };

    AbstractWaylandOutput* internal{nullptr};

    // Prefer the internal screen.
    for (auto& output : outputs) {
        auto wl_out = static_cast<AbstractWaylandOutput*>(output);
        if (wl_out->isInternal()) {
            // Only prefer it if the dimensions match.
            if (check_dimensions(wl_out)) {
                return wl_out;
            }
            internal = wl_out;
            break;
        }
    }

    for (auto& output : outputs) {
        auto wl_out = static_cast<AbstractWaylandOutput*>(output);
        if (check_dimensions(wl_out)) {
            return wl_out;
        }
    }

    // If nothing was found, but we got an internal screen, take this one.
    return internal;
}

}
