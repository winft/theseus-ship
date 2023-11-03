/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>
#include <render/outline.h>

#include <QObject>
#include <QTimerEvent>
#include <functional>

namespace KWin::render
{

class KWIN_EXPORT compositor_qobject : public QObject
{
    Q_OBJECT
public:
    // TODO(romangg): Should be moved somewhere else. It's needed in win spaces and can't be defined
    // in render platforms (likely because outline has Q_OBJECT macro and platforms are templated).
    using outline_t = render::outline;

    compositor_qobject(std::function<bool(QTimerEvent*)> timer_event_handler);
    ~compositor_qobject() override;

protected:
    void timerEvent(QTimerEvent* te) override;

Q_SIGNALS:
    void timer_event_received(QTimerEvent*);
    void compositingToggled(bool active);
    void aboutToDestroy();
    void aboutToToggleCompositing();

private:
    std::function<bool(QTimerEvent*)> timer_event_handler;
};

}
