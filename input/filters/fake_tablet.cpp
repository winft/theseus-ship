/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "fake_tablet.h"

#include "../pointer_redirect.h"
#include "main.h"
#include "wayland_server.h"
#include "workspace.h"

#include <QKeyEvent>

namespace KWin::input
{

bool fake_tablet_filter::tabletToolEvent(QTabletEvent* event)
{
    if (!workspace()) {
        return false;
    }

    switch (event->type()) {
    case QEvent::TabletMove:
    case QEvent::TabletEnterProximity:
        kwinApp()->input_redirect->pointer()->processMotion(event->globalPosF(),
                                                            event->timestamp());
        break;
    case QEvent::TabletPress:
        kwinApp()->input_redirect->pointer()->processButton(qtMouseButtonToButton(Qt::LeftButton),
                                                            InputRedirection::PointerButtonPressed,
                                                            event->timestamp());
        break;
    case QEvent::TabletRelease:
        kwinApp()->input_redirect->pointer()->processButton(qtMouseButtonToButton(Qt::LeftButton),
                                                            InputRedirection::PointerButtonReleased,
                                                            event->timestamp());
        break;
    case QEvent::TabletLeaveProximity:
        break;
    default:
        qCWarning(KWIN_CORE) << "Unexpected tablet event type" << event;
        break;
    }
    waylandServer()->simulateUserActivity();
    return true;
}

}
