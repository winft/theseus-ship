/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2020 Roman Gilg <subdiff@gmail.com>

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

#define private public
#include "base/options.h"
#undef private

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "screens.h"

#include "win/move.h"
#include "win/stacking.h"
#include "win/wayland/space.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <KConfigGroup>

namespace KWin
{

class TestScreens : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testReconfigure_data();
    void testReconfigure();
    void testSize_data();
    void testSize();
    void testCount();
    void testIntersecting_data();
    void testIntersecting();
    void testCurrent_data();
    void testCurrent();
    void testCurrentClient();
    void testCurrentWithFollowsMouse_data();
    void testCurrentWithFollowsMouse();
    void testCurrentPoint_data();
    void testCurrentPoint();

private:
    Wrapland::Client::Compositor* m_compositor = nullptr;
};

void TestScreens::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void TestScreens::init()
{
    Test::setup_wayland_connection();
    m_compositor = Test::get_client().interfaces.compositor.get();

    Test::app()->set_outputs(1);
    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void TestScreens::cleanup()
{
    Test::destroy_wayland_connection();
}

void TestScreens::testReconfigure_data()
{
    QTest::addColumn<QString>("focusPolicy");
    QTest::addColumn<bool>("expectedDefault");

    QTest::newRow("ClickToFocus") << QStringLiteral("ClickToFocus") << false;
    QTest::newRow("FocusFollowsMouse") << QStringLiteral("FocusFollowsMouse") << true;
    QTest::newRow("FocusUnderMouse") << QStringLiteral("FocusUnderMouse") << true;
    QTest::newRow("FocusStrictlyUnderMouse") << QStringLiteral("FocusStrictlyUnderMouse") << true;
}

void TestScreens::testReconfigure()
{
    auto& options = Test::app()->options;
    options->loadConfig();

    QCOMPARE(options->get_current_output_follows_mouse(), false);

    QFETCH(QString, focusPolicy);

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("Windows").writeEntry("FocusPolicy", focusPolicy);
    config->group("Windows").sync();
    config->sync();

    auto original_config = Test::app()->config();
    Test::app()->setConfig(config);
    options = std::make_unique<base::options>();
    options->loadConfig();

    QFETCH(bool, expectedDefault);
    QCOMPARE(options->get_current_output_follows_mouse(), expectedDefault);

    config->group("Windows").writeEntry("ActiveMouseScreen", !expectedDefault);
    config->sync();
    options->updateSettings();
    QCOMPARE(options->get_current_output_follows_mouse(), !expectedDefault);

    Test::app()->setConfig(original_config);
    options = std::make_unique<base::options>();
    options->loadConfig();
    QCOMPARE(options->get_current_output_follows_mouse(), false);
}

auto to_vector(QList<QRect> const& list)
{
    std::vector<QRect> vector;
    vector.resize(list.size());
    size_t count{0};
    for (auto const& element : list) {
        vector.at(count) = element;
        count++;
    }
    return vector;
}

void TestScreens::testSize_data()
{
    QTest::addColumn<QList<QRect>>("geometries");
    QTest::addColumn<QSize>("expectedSize");
    QTest::addColumn<int>("changeCount");

    // TODO(romangg): To test empty size does not make sense. Or does it?
    // QTest::newRow("empty") << QList<QRect>{{QRect()}} << QSize(0, 0);
    QTest::newRow("cloned") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}}
                            << QSize(200, 100) << 2;
    QTest::newRow("adjacent") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}}
                              << QSize(600, 400) << 4;
    QTest::newRow("overlapping") << QList<QRect>{{QRect{-10, -20, 50, 100}, QRect{0, 0, 100, 200}}}
                                 << QSize(110, 220) << 3;
    QTest::newRow("gap") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}}
                         << QSize(30, 60) << 3;
}

void TestScreens::testSize()
{
    auto& screens = Test::app()->base.screens;
    QSignalSpy sizeChangedSpy(&screens, &KWin::Screens::sizeChanged);
    QVERIFY(sizeChangedSpy.isValid());

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));

    QTEST(sizeChangedSpy.count(), "changeCount");
    QTEST(screens.size(), "expectedSize");
}

void TestScreens::testCount()
{
    auto& screens = Test::app()->base.screens;
    auto const& base = Test::app()->base;

    QSignalSpy countChangedSpy(&screens, &KWin::Screens::countChanged);
    QVERIFY(countChangedSpy.isValid());

    QCOMPARE(base.get_outputs().size(), 1);

    // change to two screens
    QList<QRect> geometries{{QRect{0, 0, 100, 200}, QRect{100, 0, 100, 200}}};
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(countChangedSpy.count(), 3);
    QCOMPARE(base.get_outputs().size(), 2);

    countChangedSpy.clear();

    // go back to one screen
    geometries.takeLast();
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(countChangedSpy.count(), 3);
    QCOMPARE(countChangedSpy.last().first().toInt(), 0);
    QCOMPARE(countChangedSpy.last().last().toInt(), 1);
    QCOMPARE(base.get_outputs().size(), 1);

    // Setting the same geometries should emit the signal again.
    QSignalSpy changedSpy(&screens, &Screens::changed);
    QVERIFY(changedSpy.isValid());
    countChangedSpy.clear();

    Test::app()->set_outputs(to_vector(geometries));
    QCOMPARE(changedSpy.count(), geometries.size() + 2);
    QCOMPARE(countChangedSpy.count(), 2);
}

void TestScreens::testIntersecting_data()
{
    QTest::addColumn<QList<QRect>>("geometries");
    QTest::addColumn<QRect>("testGeometry");
    QTest::addColumn<int>("expectedCount");

    QTest::newRow("null-rect") << QList<QRect>{{QRect{0, 0, 100, 100}}} << QRect() << 0;
    QTest::newRow("non-overlapping")
        << QList<QRect>{{QRect{0, 0, 100, 100}}} << QRect(100, 0, 100, 100) << 0;
    QTest::newRow("in-between") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}}
                                << QRect(15, 0, 2, 2) << 0;
    QTest::newRow("gap-overlapping") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}}
                                     << QRect(9, 10, 200, 200) << 2;
    QTest::newRow("larger") << QList<QRect>{{QRect{0, 0, 100, 100}}} << QRect(-10, -10, 200, 200)
                            << 1;
    QTest::newRow("several") << QList<QRect>{{QRect{0, 0, 100, 100},
                                              QRect{100, 0, 100, 100},
                                              QRect{200, 100, 100, 100},
                                              QRect{300, 100, 100, 100}}}
                             << QRect(0, 0, 300, 300) << 3;
}

void TestScreens::testIntersecting()
{
    auto& screens = Test::app()->base.screens;
    QSignalSpy changedSpy(&screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(changedSpy.count(), geometries.size() + 2);

    QFETCH(QRect, testGeometry);
    QCOMPARE(Test::app()->base.get_outputs().size(), geometries.count());
    QTEST(screens.intersecting(testGeometry), "expectedCount");
}

void TestScreens::testCurrent_data()
{
    QTest::addColumn<int>("current");
    QTest::addColumn<bool>("signal");

    QTest::newRow("unchanged") << 0 << false;
    QTest::newRow("changed") << 1 << true;
}

void TestScreens::testCurrent()
{
    auto& screens = Test::app()->base.screens;
    QSignalSpy currentChangedSpy(&screens, &KWin::Screens::currentChanged);
    QVERIFY(currentChangedSpy.isValid());

    QFETCH(int, current);
    screens.setCurrent(current);
    QCOMPARE(screens.current(), current);
    QTEST(!currentChangedSpy.isEmpty(), "signal");
}

void TestScreens::testCurrentClient()
{
    auto& screens = Test::app()->base.screens;
    QSignalSpy changedSpy(&screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    QList<QRect> geometries{{QRect{0, 0, 100, 100}, QRect{100, 0, 100, 100}}};
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(changedSpy.count(), geometries.size() + 2);

    QSignalSpy currentChangedSpy(&screens, &KWin::Screens::currentChanged);
    QVERIFY(currentChangedSpy.isValid());

    // Create a window.
    QSignalSpy clientAddedSpy(static_cast<win::wayland::space*>(workspace()),
                              &win::wayland::space::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flush_wayland_connection();
    QVERIFY(clientAddedSpy.wait());
    auto client = workspace()->activeClient();
    QVERIFY(client);

    win::move(client, QPoint(101, 0));
    QCOMPARE(workspace()->activeClient(), client);
    workspace()->setActiveClient(nullptr);
    QCOMPARE(workspace()->activeClient(), nullptr);

    // it's not the active client, so changing won't work
    screens.setCurrent(client);
    QVERIFY(currentChangedSpy.isEmpty());
    QCOMPARE(screens.current(), 0);

    // making the client active should affect things
    win::set_active(client, true);
    workspace()->setActiveClient(client);

    // first of all current should be changed just by the fact that there is an active client
    QCOMPARE(screens.current(), 1);
    // but also calling setCurrent should emit the changed signal
    screens.setCurrent(client);
    QCOMPARE(currentChangedSpy.count(), 1);
    QCOMPARE(screens.current(), 1);

    // setting current with the same client again should not change, though
    screens.setCurrent(client);
    QCOMPARE(currentChangedSpy.count(), 1);

    // and it should even still be on screen 1 if we make the client non-current again
    workspace()->setActiveClient(nullptr);
    win::set_active(client, false);
    QCOMPARE(screens.current(), 1);
}

void TestScreens::testCurrentWithFollowsMouse_data()
{
    QTest::addColumn<QList<QRect>>("geometries");
    QTest::addColumn<QPoint>("cursorPos");
    QTest::addColumn<int>("expected");

    // TODO(romangg): To test empty size does not make sense. Or does it?
    // QTest::newRow("empty") << QList<QRect>{{QRect()}} << QPoint(100, 100) << 0;
    QTest::newRow("cloned") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}}
                            << QPoint(50, 50) << 0;
    QTest::newRow("adjacent-0") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}}
                                << QPoint(199, 99) << 0;
    QTest::newRow("adjacent-1") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}}
                                << QPoint(200, 100) << 1;
    QTest::newRow("gap") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}}
                         << QPoint(15, 30) << 0;
}

void TestScreens::testCurrentWithFollowsMouse()
{
    auto& screens = Test::app()->base.screens;
    QSignalSpy changedSpy(&screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    auto& options = Test::app()->options;
    options->current_output_follows_mouse = true;

    Test::pointer_motion_absolute(QPointF(0, 0), 1);
    QCOMPARE(screens.current(), 0);

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(changedSpy.count(), geometries.size() + 2);

    QFETCH(QPoint, cursorPos);
    Test::pointer_motion_absolute(cursorPos, 2);
    //    KWin::s_cursorPos = cursorPos;
    QTEST(screens.current(), "expected");
}

void TestScreens::testCurrentPoint_data()
{
    QTest::addColumn<QList<QRect>>("geometries");
    QTest::addColumn<QPoint>("cursorPos");
    QTest::addColumn<int>("expected");

    // TODO(romangg): To test empty size does not make sense. Or does it?
    // QTest::newRow("empty") << QList<QRect>{{QRect()}} << QPoint(100, 100) << 0;
    QTest::newRow("cloned") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}}
                            << QPoint(50, 50) << 0;
    QTest::newRow("adjacent-0") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}}
                                << QPoint(199, 99) << 0;
    QTest::newRow("adjacent-1") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}}
                                << QPoint(200, 100) << 1;
    QTest::newRow("gap") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}}
                         << QPoint(15, 30) << 1;
}

void TestScreens::testCurrentPoint()
{
    auto& screens = Test::app()->base.screens;
    QSignalSpy changedSpy(&screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    auto& options = Test::app()->options;
    options->current_output_follows_mouse = false;

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(changedSpy.count(), geometries.size() + 2);

    QFETCH(QPoint, cursorPos);
    screens.setCurrent(cursorPos);
    QTEST(screens.current(), "expected");
}

}

WAYLANDTEST_MAIN(KWin::TestScreens)
#include "screens.moc"
