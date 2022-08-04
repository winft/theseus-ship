/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <QTimerEvent>
#include <functional>

namespace KWin::render
{

class KWIN_EXPORT compositor_qobject : public QObject
{
    Q_OBJECT
public:
    compositor_qobject(std::function<bool(QTimerEvent*)> timer_event_handler);

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
