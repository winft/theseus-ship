/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/touch.h"
#include "event.h"

#include "base/wayland/output_transform.h"
#include "kwin_export.h"

#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <cmath>

namespace KWin::input
{

class KWIN_EXPORT touch_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void down(touch_down_event);
    void up(touch_up_event);
    void motion(touch_motion_event);
    void cancel(touch_cancel_event);
    void frame();
};

class touch
{
public:
    touch()
        : qobject{std::make_unique<touch_qobject>()}
    {
    }

    virtual ~touch() = default;

    std::unique_ptr<touch_qobject> qobject;
    std::unique_ptr<control::touch> control;
};

template<typename Base>
class touch_impl : public touch
{
public:
    using output_t = typename Base::output_t;

    touch_impl(Base& base)
        : base{base}
    {
        QObject::connect(
            base.qobject.get(), &Base::qobject_t::topology_changed, qobject.get(), [this] {
                if (!control) {
                    return;
                }
                output = get_output();
                if (output) {
                    control->set_orientation(to_qt_orientation(output->transform()));
                }
            });
    }

    touch_impl(touch_impl const&) = delete;
    touch_impl& operator=(touch_impl const&) = delete;
    ~touch_impl() override = default;

    output_t* get_output() const
    {
        if (!control) {
            return nullptr;
        }

        auto const& outputs = base.outputs;
        if (outputs.empty()) {
            // Might be too early.
            return nullptr;
        }

        if (outputs.size() == 1) {
            return outputs.front();
        }

        // First try by name.
        if (auto name = control->output_name(); !name.empty()) {
            for (auto& output : outputs) {
                if (output->name() == name.c_str()) {
                    return output;
                }
            }
        }

        auto check_dimensions = [this](auto const& output) {
            auto const& size = control->size();
            auto const& out_size = output->physical_size();
            return std::round(size.width()) == std::round(out_size.width())
                && std::round(size.height()) == std::round(out_size.height());
        };

        output_t* internal{nullptr};

        // Prefer the internal screen.
        for (auto& output : outputs) {
            if (output->is_internal()) {
                // Only prefer it if the dimensions match.
                if (check_dimensions(output)) {
                    return output;
                }
                internal = output;
                break;
            }
        }

        for (auto& output : outputs) {
            if (check_dimensions(output)) {
                return output;
            }
        }

        // If nothing was found, but we got an internal screen, take this one.
        return internal;
    }

    output_t* output{nullptr};

private:
    static Qt::ScreenOrientation to_qt_orientation(base::wayland::output_transform transform)
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

    Base& base;
};

}
