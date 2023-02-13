/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/global_shortcuts_manager.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/qt_event.h"
#include "input/xkb/helpers.h"

#include <QTimer>

namespace KWin::input
{

template<typename Redirect>
class global_shortcut_filter : public event_filter<Redirect>
{
public:
    explicit global_shortcut_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
        m_powerDown = new QTimer;
        m_powerDown->setSingleShot(true);
        m_powerDown->setInterval(1000);
    }

    ~global_shortcut_filter() override
    {
        delete m_powerDown;
    }

    bool button(button_event const& event) override
    {
        if (event.state == button_state::pressed) {
            auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);
            if (this->redirect.platform.shortcuts->processPointerPressed(
                    mods, this->redirect.qtButtonStates())) {
                return true;
            }
        }
        return false;
    }

    bool axis(axis_event const& event) override
    {
        auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);

        if (mods == Qt::NoModifier) {
            return false;
        }

        auto direction = PointerAxisUp;
        if (event.orientation == axis_orientation::horizontal) {
            // TODO(romangg): Doesn't < 0 equal left direction?
            direction = event.delta < 0 ? PointerAxisRight : PointerAxisLeft;
        } else if (event.delta < 0) {
            direction = PointerAxisDown;
        }

        return this->redirect.platform.shortcuts->processAxis(mods, direction);
    }

    bool key(key_event const& event) override
    {
        auto xkb = event.base.dev->xkb.get();
        auto const& modifiers = xkb->qt_modifiers;
        auto const& shortcuts = this->redirect.platform.shortcuts.get();
        auto qt_key = key_to_qt_key(event.keycode, xkb);

        auto handle_power_key = [this, state = event.state, shortcuts, modifiers, qt_key] {
            auto power_off = [this, shortcuts, modifiers] {
                QObject::disconnect(m_powerDown, &QTimer::timeout, shortcuts, nullptr);
                m_powerDown->stop();
                shortcuts->processKey(modifiers, Qt::Key_PowerDown);
            };

            switch (state) {
            case key_state::pressed:
                QObject::connect(m_powerDown, &QTimer::timeout, shortcuts, power_off);
                m_powerDown->start();
                return true;
            case key_state::released:
                auto const ret
                    = !m_powerDown->isActive() || shortcuts->processKey(modifiers, qt_key);
                m_powerDown->stop();
                return ret;
            }
            return false;
        };

        if (qt_key == Qt::Key_PowerOff) {
            return handle_power_key();
        }

        if (event.state == key_state::pressed) {
            return shortcuts->processKey(modifiers, qt_key);
        } else {
            return shortcuts->processKeyRelease(modifiers, qt_key);
        }
    }

    bool key_repeat(key_event const& event) override
    {
        auto xkb = event.base.dev->xkb.get();
        auto qt_key = key_to_qt_key(event.keycode, xkb);

        if (qt_key == Qt::Key_PowerOff) {
            return false;
        }

        auto const& modifiers = xkb->modifiers_relevant_for_global_shortcuts();
        return this->redirect.platform.shortcuts->processKey(modifiers, qt_key);
    }

    bool swipe_begin(swipe_begin_event const& event) override
    {
        this->redirect.platform.shortcuts->processSwipeStart(DeviceType::Touchpad, event.fingers);
        return false;
    }

    bool swipe_update(swipe_update_event const& event) override
    {
        this->redirect.platform.shortcuts->processSwipeUpdate(
            DeviceType::Touchpad, QSizeF(event.delta.x(), event.delta.y()));
        return false;
    }

    bool swipe_end(swipe_end_event const& event) override
    {
        if (event.cancelled) {
            this->redirect.platform.shortcuts->processSwipeCancel(DeviceType::Touchpad);
        } else {
            this->redirect.platform.shortcuts->processSwipeEnd(DeviceType::Touchpad);
        }
        return false;
    }

    bool pinch_begin(pinch_begin_event const& event) override
    {
        if (event.fingers >= 3) {
            this->redirect.platform.shortcuts->processPinchStart(event.fingers);
        }
        return false;
    }

    bool pinch_update(pinch_update_event const& event) override
    {
        this->redirect.platform.shortcuts->processPinchUpdate(
            event.scale, event.rotation, QSizeF(event.delta.x(), event.delta.y()));
        return false;
    }

    bool pinch_end(pinch_end_event const& event) override
    {
        if (event.cancelled) {
            this->redirect.platform.shortcuts->processPinchCancel();
        } else {
            this->redirect.platform.shortcuts->processPinchEnd();
        }
        return false;
    }

    bool touch_down(touch_down_event const& event) override
    {
        if (m_gestureTaken) {
            this->redirect.platform.shortcuts->processSwipeCancel(DeviceType::Touchscreen);
            m_gestureCancelled = true;
            return true;
        } else {
            auto pos = event.pos;
            m_touchPoints.insert(event.id, pos);
            if (m_touchPoints.count() == 1) {
                m_lastTouchDownTime = event.base.time_msec;
            } else {
                if (event.base.time_msec - m_lastTouchDownTime > 250) {
                    m_gestureCancelled = true;
                    return false;
                }
                m_lastTouchDownTime = event.base.time_msec;
                auto const& outputs = this->redirect.platform.base.outputs;
                auto output = base::get_nearest_output(outputs, pos.toPoint());
                float xfactor = output->physical_size().width() / (float)output->geometry().width();
                float yfactor
                    = output->physical_size().height() / (float)output->geometry().height();
                bool distanceMatch = std::any_of(
                    m_touchPoints.constBegin(),
                    m_touchPoints.constEnd(),
                    [pos, xfactor, yfactor](const auto& point) {
                        QPointF p = pos - point;
                        return std::abs(xfactor * p.x()) + std::abs(yfactor * p.y()) < 50;
                    });
                if (!distanceMatch) {
                    m_gestureCancelled = true;
                    return false;
                }
            }
            if (m_touchPoints.count() >= 3 && !m_gestureCancelled) {
                m_gestureTaken = true;
                process_filters(
                    this->redirect.m_filters,
                    std::bind(&event_filter<Redirect>::touch_cancel, std::placeholders::_1));
                this->redirect.platform.shortcuts->processSwipeStart(DeviceType::Touchscreen,
                                                                     m_touchPoints.count());
                return true;
            }
        }
        return false;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        Q_UNUSED(time);
        if (m_gestureTaken) {
            if (m_gestureCancelled) {
                return true;
            }
            auto const& outputs = this->redirect.platform.base.outputs;
            auto output = base::get_nearest_output(outputs, event.pos.toPoint());
            const float xfactor
                = output->physical_size().width() / (float)output->geometry().width();
            const float yfactor
                = output->physical_size().height() / (float)output->geometry().height();

            auto& point = m_touchPoints[event.id];
            const QPointF dist = event.pos - point;
            const QSizeF delta = QSizeF(xfactor * dist.x(), yfactor * dist.y());
            this->redirect.platform.shortcuts->processSwipeUpdate(DeviceType::Touchscreen,
                                                                  5 * delta / m_touchPoints.size());
            point = event.pos;
            return true;
        }
        return false;
    }

    bool touch_up(touch_up_event const& event) override
    {
        Q_UNUSED(time);
        m_touchPoints.remove(event.id);
        if (m_gestureTaken) {
            if (!m_gestureCancelled) {
                this->redirect.platform.shortcuts->processSwipeEnd(DeviceType::Touchscreen);
                m_gestureCancelled = true;
            }
            m_gestureTaken &= m_touchPoints.count() > 0;
            m_gestureCancelled &= m_gestureTaken;
            m_touchGestureCancelSent &= m_gestureTaken;
            return true;
        }
        return false;
    }

    bool touch_frame() override
    {
        return m_gestureTaken;
    }

private:
    bool m_gestureTaken = false;
    bool m_gestureCancelled = false;
    bool m_touchGestureCancelSent = false;
    uint32_t m_lastTouchDownTime = 0;
    QPointF m_lastAverageDistance;
    QMap<int32_t, QPointF> m_touchPoints;

    QTimer* m_powerDown = nullptr;
};

}
