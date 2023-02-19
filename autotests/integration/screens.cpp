/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "base/options.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "lib/app.h"

#include "win/activation.h"
#include "win/move.h"
#include "win/screen.h"
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
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void TestScreens::init()
{
    Test::setup_wayland_connection();
    m_compositor = Test::get_client().interfaces.compositor.get();

    Test::app()->set_outputs(1);
    Test::set_current_output(0);
    Test::cursor()->set_pos(QPoint(640, 512));
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
    auto original_config = Test::app()->base->config.main;
    auto& options = Test::app()->base->options;

    QCOMPARE(options->get_current_output_follows_mouse(), false);
    QFETCH(QString, focusPolicy);

    auto config = KSharedConfig::openConfig("testScreens_testReconfigure", KConfig::SimpleConfig);
    config->group("Windows").writeEntry("FocusPolicy", focusPolicy);
    config->group("Windows").deleteEntry("ActiveMouseScreen");
    config->group("Windows").sync();
    config->sync();

    Test::app()->base->config.main = config;
    options = std::make_unique<base::options>(Test::app()->base->operation_mode, config);
    options->loadConfig();

    QFETCH(bool, expectedDefault);
    QCOMPARE(options->get_current_output_follows_mouse(), expectedDefault);

    config->group("Windows").writeEntry("ActiveMouseScreen", !expectedDefault);
    config->sync();
    options->updateSettings();
    QCOMPARE(options->get_current_output_follows_mouse(), !expectedDefault);

    Test::app()->base->config.main = original_config;
    options = std::make_unique<base::options>(Test::app()->base->operation_mode, original_config);
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
    QSignalSpy topology_spy(Test::app()->base.get(), &base::platform::topology_changed);
    QVERIFY(topology_spy.isValid());

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(topology_spy.count(), 1);
    QTEST(Test::app()->base->topology.size, "expectedSize");
}

void TestScreens::testCount()
{
    auto const& base = Test::app()->base;

    QSignalSpy output_added_spy(base.get(), &base::platform::output_added);
    QSignalSpy output_removed_spy(base.get(), &base::platform::output_removed);
    QVERIFY(output_added_spy.isValid());
    QVERIFY(output_removed_spy.isValid());

    QCOMPARE(base->get_outputs().size(), 1);

    // change to two screens
    QList<QRect> geometries{{QRect{0, 0, 100, 200}, QRect{100, 0, 100, 200}}};
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(output_added_spy.count(), 2);
    QCOMPARE(output_removed_spy.count(), 1);
    QCOMPARE(base->get_outputs().size(), 2);

    output_added_spy.clear();
    output_removed_spy.clear();

    // go back to one screen
    geometries.takeLast();
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(output_removed_spy.count(), 2);
    QCOMPARE(output_added_spy.count(), 1);
    QCOMPARE(base->get_outputs().size(), 1);

    // Setting the same geometries should emit the signal again.
    QSignalSpy changedSpy(Test::app()->base.get(), &base::platform::topology_changed);
    QVERIFY(changedSpy.isValid());

    output_added_spy.clear();
    output_removed_spy.clear();

    Test::app()->set_outputs(to_vector(geometries));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(output_removed_spy.count(), 1);
    QCOMPARE(output_added_spy.count(), 1);
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
    QSignalSpy changedSpy(Test::app()->base.get(), &base::platform::topology_changed);
    QVERIFY(changedSpy.isValid());

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(changedSpy.count(), 1);

    QFETCH(QRect, testGeometry);
    auto const& outputs = Test::app()->base->get_outputs();
    QCOMPARE(outputs.size(), geometries.count());
    QTEST(static_cast<int>(base::get_intersecting_outputs(outputs, testGeometry).size()),
          "expectedCount");
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
    auto& base = Test::app()->base;
    Test::app()->set_outputs(2);
    QCOMPARE(base->get_outputs().size(), 2);

    QSignalSpy current_changed_spy(base.get(), &base::platform::current_output_changed);
    QVERIFY(current_changed_spy.isValid());

    QFETCH(int, current);
    Test::set_current_output(current);
    QCOMPARE(
        base::get_output_index(base->outputs, *win::get_current_output(*Test::app()->base->space)),
        current);
    QTEST(!current_changed_spy.isEmpty(), "signal");
}

void TestScreens::testCurrentClient()
{
    QSignalSpy changedSpy(Test::app()->base.get(), &base::platform::topology_changed);
    QVERIFY(changedSpy.isValid());
    QSignalSpy current_output_spy(Test::app()->base.get(), &base::platform::current_output_changed);
    QVERIFY(current_output_spy.isValid());

    QList<QRect> geometries{{QRect{0, 0, 100, 100}, QRect{100, 0, 100, 100}}};
    Test::app()->set_outputs(to_vector(geometries));

    QCOMPARE(changedSpy.count(), 1);
    changedSpy.clear();

    // Create a window.
    QSignalSpy clientAddedSpy(Test::app()->base->space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flush_wayland_connection();
    QVERIFY(clientAddedSpy.wait());
    auto client = Test::get_wayland_window(Test::app()->base->space->stacking.active);
    QVERIFY(client);

    win::move(client, QPoint(101, 0));
    QCOMPARE(Test::app()->base->space->stacking.active, Test::space::window_t(client));
    win::unset_active_window(*Test::app()->base->space);
    QVERIFY(!Test::app()->base->space->stacking.active);

    QCOMPARE(win::get_current_output(*Test::app()->base->space),
             base::get_output(Test::app()->base->get_outputs(), 0));

    // it's not the active client, so changing won't work
    win::set_current_output_by_window(*Test::app()->base, *client);
    QVERIFY(changedSpy.isEmpty());
    QVERIFY(current_output_spy.isEmpty());

    auto output = base::get_output(Test::app()->base->get_outputs(), 0);
    QVERIFY(output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);

    // making the client active should affect things
    win::set_active(client, true);
    win::set_active_window(*Test::app()->base->space, *client);
    QCOMPARE(Test::get_wayland_window(Test::app()->base->space->stacking.active), client);

    // first of all current should be changed just by the fact that there is an active client
    output = base::get_output(Test::app()->base->get_outputs(), 1);
    QVERIFY(output);
    QCOMPARE(client->topo.central_output, output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);

    // but also calling setCurrent should emit the changed signal
    win::set_current_output_by_window(*Test::app()->base, *client);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(current_output_spy.count(), 1);

    output = base::get_output(Test::app()->base->get_outputs(), 1);
    QVERIFY(output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);

    // setting current with the same client again should not change, though
    win::set_current_output_by_window(*Test::app()->base, *client);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(current_output_spy.count(), 1);

    // and it should even still be on screen 1 if we make the client non-current again
    win::unset_active_window(*Test::app()->base->space);
    win::set_active(client, false);

    output = base::get_output(Test::app()->base->get_outputs(), 1);
    QVERIFY(output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);
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
    QSignalSpy changedSpy(Test::app()->base.get(), &base::platform::topology_changed);
    QVERIFY(changedSpy.isValid());

    auto group = Test::app()->base->config.main->group("Windows");
    group.writeEntry("ActiveMouseScreen", true);
    group.sync();
    win::space_reconfigure(*Test::app()->base->space);

    Test::pointer_motion_absolute(QPointF(0, 0), 1);

    auto output = base::get_output(Test::app()->base->get_outputs(), 0);
    QVERIFY(output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));
    QCOMPARE(changedSpy.count(), 1);

    QFETCH(QPoint, cursorPos);
    Test::pointer_motion_absolute(cursorPos, 2);

    QFETCH(int, expected);
    output = base::get_output(Test::app()->base->get_outputs(), expected);
    QVERIFY(output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);
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
    QSignalSpy changedSpy(Test::app()->base.get(), &base::platform::topology_changed);
    QVERIFY(changedSpy.isValid());

    auto group = Test::app()->base->config.main->group("Windows");
    group.writeEntry("ActiveMouseScreen", false);
    group.sync();
    win::space_reconfigure(*Test::app()->base->space);

    QFETCH(QList<QRect>, geometries);
    Test::app()->set_outputs(to_vector(geometries));
    QCOMPARE(changedSpy.count(), 1);

    QFETCH(QPoint, cursorPos);
    base::set_current_output_by_position(*Test::app()->base, cursorPos);

    QFETCH(int, expected);
    auto output = base::get_output(Test::app()->base->get_outputs(), expected);
    QVERIFY(output);
    QCOMPARE(win::get_current_output(*Test::app()->base->space), output);
}

}

WAYLANDTEST_MAIN(KWin::TestScreens)
#include "screens.moc"
