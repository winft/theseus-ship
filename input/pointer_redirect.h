/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device_redirect.h"
#include "event.h"

#include <QPointF>

namespace KWin
{

namespace input
{
class pointer;

class KWIN_EXPORT pointer_redirect : public device_redirect
{
    Q_OBJECT
public:
    static bool s_cursorUpdateBlocking;

    void init() override
    {
    }
    virtual void updateAfterScreenChange()
    {
    }
    virtual bool supportsWarping() const
    {
        return false;
    }
    virtual void warp(QPointF const& /*pos*/)
    {
    }
    virtual QPointF pos() const
    {
        return {};
    }
    virtual Qt::MouseButtons buttons() const
    {
        return {};
    }
    virtual bool areButtonsPressed() const
    {
        return false;
    }
    virtual void setEffectsOverrideCursor(Qt::CursorShape /*shape*/)
    {
    }
    virtual void removeEffectsOverrideCursor()
    {
    }
    virtual void setWindowSelectionCursor(QByteArray const& /*shape*/)
    {
    }
    virtual void removeWindowSelectionCursor()
    {
    }
    virtual void updatePointerConstraints()
    {
    }
    virtual void setEnableConstraints(bool /*set*/)
    {
    }
    virtual bool isConstrained() const
    {
        return false;
    }
    virtual void process_motion(motion_event const& /*event*/)
    {
    }
    virtual void process_motion_absolute(motion_absolute_event const& /*event*/)
    {
    }
    virtual void processMotion(QPointF const& /*pos*/,
                               uint32_t /*time*/,
                               [[maybe_unused]] KWin::input::pointer* device = nullptr)
    {
    }
    virtual void process_button(button_event const& /*event*/)
    {
    }
    virtual void process_axis(axis_event const& /*event*/)
    {
    }
    virtual void process_swipe_begin(swipe_begin_event const& /*event*/)
    {
    }
    virtual void process_swipe_update(swipe_update_event const& /*event*/)
    {
    }
    virtual void process_swipe_end(swipe_end_event const& /*event*/)
    {
    }
    virtual void process_pinch_begin(pinch_begin_event const& /*event*/)
    {
    }
    virtual void process_pinch_update(pinch_update_event const& /*event*/)
    {
    }
    virtual void process_pinch_end(pinch_end_event const& /*event*/)
    {
    }
};

}
}
