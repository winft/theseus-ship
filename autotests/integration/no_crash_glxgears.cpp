/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "win/deco.h"
#include "win/x11/window.h"

#include <KDecoration2/Decoration>

namespace KWin::detail::test
{

TEST_CASE("no crash glxgears", "[xwl],[win]")
{
    // Closing a glxgears window through Aurorae themes used to crash KWin.

    test::setup setup("no-crash-glxgears", base::operation_mode::xwayland);
    setup.start();

    QSignalSpy clientAddedSpy(setup.base->space->qobject.get(), &space::qobject_t::clientAdded);
    QVERIFY(clientAddedSpy.isValid());

    QProcess glxgears;
    glxgears.setProgram(QStringLiteral("glxgears"));
    glxgears.start();
    QVERIFY(glxgears.waitForStarted());

    QVERIFY(clientAddedSpy.wait());
    QCOMPARE(clientAddedSpy.count(), 1);
    QCOMPARE(setup.base->space->windows.size(), 1);

    auto glxgearsClient = get_x11_window(setup.base->space->windows.front());
    QVERIFY(glxgearsClient);
    QVERIFY(win::decoration(glxgearsClient));
    QSignalSpy closedSpy(glxgearsClient->qobject.get(), &win::window_qobject::closed);
    QVERIFY(closedSpy.isValid());

    auto decoration = win::decoration(glxgearsClient);
    QVERIFY(decoration);

    // send a mouse event to the position of the close button
    // TODO: position is dependent on the decoration in use. We should use a static target instead,
    // a fake deco for autotests.
    QPointF pos = decoration->rect().topRight()
        + QPointF(-decoration->borderTop() / 2, decoration->borderTop() / 2);
    QHoverEvent event(QEvent::HoverMove, pos, pos);
    QCoreApplication::instance()->sendEvent(decoration, &event);

    // mouse press
    QMouseEvent mousePressevent(
        QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressevent.setAccepted(false);
    QCoreApplication::sendEvent(decoration, &mousePressevent);
    QVERIFY(mousePressevent.isAccepted());

    // mouse Release
    QMouseEvent mouseReleaseEvent(
        QEvent::MouseButtonRelease, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseReleaseEvent.setAccepted(false);
    QCoreApplication::sendEvent(decoration, &mouseReleaseEvent);
    QVERIFY(mouseReleaseEvent.isAccepted());

    QVERIFY(closedSpy.wait());
    xcb_flush(setup.base->x11_data.connection);

    if (glxgears.state() == QProcess::Running) {
        QVERIFY(glxgears.waitForFinished());
    }
}

}
