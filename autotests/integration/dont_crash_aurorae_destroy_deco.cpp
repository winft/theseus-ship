/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "render/compositor.h"
#include "render/scene.h"
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <KDecoration2/Decoration>

#include <QQuickItem>

#include <linux/input.h>

namespace KWin
{

class DontCrashAuroraeDestroyDecoTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void testBorderlessMaximizedWindows();
};

void DontCrashAuroraeDestroyDecoTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("org.kde.kdecoration2").writeEntry("library", "org.kde.kwin.aurorae");
    config->sync();
    kwinApp()->setConfig(config);

    // this test needs to enforce OpenGL compositing to get into the crashy condition
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();

    auto& scene = Test::app()->base.render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
}

void DontCrashAuroraeDestroyDecoTest::init()
{
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void DontCrashAuroraeDestroyDecoTest::testBorderlessMaximizedWindows()
{
    // this test verifies that Aurorae doesn't crash when clicking the maximize button
    // with kwin config option BorderlessMaximizedWindows
    // see BUG 362772

    // first adjust the config
    KConfigGroup group = kwinApp()->config()->group("Windows");
    group.writeEntry("BorderlessMaximizedWindows", true);
    group.sync();
    Test::app()->workspace->slotReconfigure();
    QCOMPARE(kwinApp()->options->borderlessMaximizedWindows(), true);

    // create an xcb window
    xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    QVERIFY(!xcb_connection_has_error(c));

    xcb_window_t w = xcb_generate_id(c);
    xcb_create_window(c,
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      0,
                      0,
                      100,
                      200,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      0,
                      nullptr);
    xcb_map_window(c, w);
    xcb_flush(c);

    // we should get a client for it
    QSignalSpy windowCreatedSpy(Test::app()->workspace.get(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(win::decoration(client) != nullptr);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QCOMPARE(client->noBorder(), false);
    // verify that the deco is Aurorae
    QCOMPARE(qstrcmp(win::decoration(client)->metaObject()->className(), "Aurorae::Decoration"), 0);
    // find the maximize button
    auto item = win::decoration(client)->findChild<QQuickItem*>("maximizeButton");
    QVERIFY(item);
    const QPointF scenePoint = item->mapToScene(QPoint(0, 0));

    // mark the window as ready for painting, otherwise it doesn't get input events
    QMetaObject::invokeMethod(client, "setReadyForPainting");
    QVERIFY(client->ready_for_painting);

    // simulate click on maximize button
    QSignalSpy maximizedStateChangedSpy(
        client,
        static_cast<void (Toplevel::*)(KWin::Toplevel*, win::maximize_mode)>(
            &Toplevel::clientMaximizedStateChanged));
    QVERIFY(maximizedStateChangedSpy.isValid());
    quint32 timestamp = 1;
    Test::pointer_motion_absolute(client->frameGeometry().topLeft() + scenePoint.toPoint(),
                                  timestamp++);
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QVERIFY(maximizedStateChangedSpy.wait());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->noBorder(), true);

    // and destroy the window again
    xcb_unmap_window(c, w);
    xcb_destroy_window(c, w);
    xcb_flush(c);
    xcb_disconnect(c);

    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::DontCrashAuroraeDestroyDecoTest)
#include "dont_crash_aurorae_destroy_deco.moc"
