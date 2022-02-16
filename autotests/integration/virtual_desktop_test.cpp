/*
    SPDX-FileCopyrightText: 2012, 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "main.h"
#include "screens.h"
#include "win/screen.h"
#include "win/virtual_desktops.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>

using namespace Wrapland::Client;

namespace KWin
{

class VirtualDesktopTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_count_data();
    void test_count();
    void test_navigation_wraps_around_data();
    void test_navigation_wraps_around();
    void test_current_data();
    void test_current();
    void test_current_change_on_count_change_data();
    void test_current_change_on_count_change();

    void next_data();
    void next();
    void previous_data();
    void previous();
    void left_data();
    void left();
    void right_data();
    void right();
    void above_data();
    void above();
    void below_data();
    void below();

    void update_grid_data();
    void update_grid();
    void update_layout_data();
    void update_layout();

    void test_name_data();
    void test_name();
    void test_switch_to_shortcuts();
    void test_change_rows();
    void test_load();
    void test_save();

    void testNetCurrentDesktop();
    void testLastDesktopRemoved();
    void testWindowOnMultipleDesktops();
    void testRemoveDesktopWithWindow();

private:
    template<typename T>
    void test_direction(QString const& actionName);
};

void VirtualDesktopTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());

    if (kwinApp()->x11Connection()) {
        // verify the current desktop x11 property on startup, see BUG: 391034
        base::x11::xcb::atom currentDesktopAtom("_NET_CURRENT_DESKTOP", connection());
        QVERIFY(currentDesktopAtom.is_valid());
        base::x11::xcb::property currentDesktop(
            0, kwinApp()->x11RootWindow(), currentDesktopAtom, XCB_ATOM_CARDINAL, 0, 1);
        bool ok = true;
        QCOMPARE(currentDesktop.value(0, &ok), 0);
        QVERIFY(ok);
    }
}

void VirtualDesktopTest::init()
{
    Test::setup_wayland_connection();
    win::virtual_desktop_manager::self()->setCount(1);
    win::virtual_desktop_manager::self()->setCurrent(0u);
}

void VirtualDesktopTest::cleanup()
{
    Test::destroy_wayland_connection();
}
static const uint s_countInitValue = 2;

void VirtualDesktopTest::test_count_data()
{
    QTest::addColumn<uint>("request");
    QTest::addColumn<uint>("result");
    QTest::addColumn<bool>("signal");
    QTest::addColumn<bool>("removedSignal");

    QTest::newRow("Minimum") << 1u << 1u << true << true;
    QTest::newRow("Below Minimum") << 0u << 1u << true << true;
    QTest::newRow("Normal Value") << 10u << 10u << true << false;
    QTest::newRow("Maximum") << win::virtual_desktop_manager::maximum()
                             << win::virtual_desktop_manager::maximum() << true << false;
    QTest::newRow("Above Maximum") << win::virtual_desktop_manager::maximum() + 1
                                   << win::virtual_desktop_manager::maximum() << true << false;
    QTest::newRow("Unchanged") << s_countInitValue << s_countInitValue << false << false;
}

void VirtualDesktopTest::test_count()
{
    auto vds = win::virtual_desktop_manager::self();
    QCOMPARE(vds->count(), 1);

    // start with a useful desktop count
    vds->setCount(s_countInitValue);

    QSignalSpy spy(vds, &win::virtual_desktop_manager::countChanged);
    QSignalSpy desktopsRemoved(vds, &win::virtual_desktop_manager::desktopRemoved);

    auto vdToRemove = vds->desktops().last();

    QFETCH(uint, request);
    QFETCH(uint, result);
    QFETCH(bool, signal);
    QFETCH(bool, removedSignal);

    vds->setCount(request);
    QCOMPARE(vds->count(), result);
    QCOMPARE(spy.isEmpty(), !signal);

    if (!spy.isEmpty()) {
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.count(), 2);
        QCOMPARE(arguments.at(0).type(), QVariant::UInt);
        QCOMPARE(arguments.at(1).type(), QVariant::UInt);
        QCOMPARE(arguments.at(0).toUInt(), s_countInitValue);
        QCOMPARE(arguments.at(1).toUInt(), result);
    }

    QCOMPARE(desktopsRemoved.isEmpty(), !removedSignal);
    if (!desktopsRemoved.isEmpty()) {
        QList<QVariant> arguments = desktopsRemoved.takeFirst();
        QCOMPARE(arguments.count(), 1);
        QCOMPARE(arguments.at(0).value<win::virtual_desktop*>(), vdToRemove);
    }
}

void VirtualDesktopTest::test_navigation_wraps_around_data()
{
    QTest::addColumn<bool>("init");
    QTest::addColumn<bool>("request");
    QTest::addColumn<bool>("result");
    QTest::addColumn<bool>("signal");

    QTest::newRow("enable") << false << true << true << true;
    QTest::newRow("disable") << true << false << false << true;
    QTest::newRow("keep enabled") << true << true << true << false;
    QTest::newRow("keep disabled") << false << false << false << false;
}

void VirtualDesktopTest::test_navigation_wraps_around()
{
    auto vds = win::virtual_desktop_manager::self();

    // TODO(romangg): This is sometimes false. Why?
    // QCOMPARE(vds->isNavigationWrappingAround(), true);

    QFETCH(bool, init);
    QFETCH(bool, request);
    QFETCH(bool, result);
    QFETCH(bool, signal);

    // set to init value
    vds->setNavigationWrappingAround(init);
    QCOMPARE(vds->isNavigationWrappingAround(), init);

    QSignalSpy spy(vds, &win::virtual_desktop_manager::navigationWrappingAroundChanged);
    vds->setNavigationWrappingAround(request);
    QCOMPARE(vds->isNavigationWrappingAround(), result);
    QCOMPARE(spy.isEmpty(), !signal);
}

void VirtualDesktopTest::test_current_data()
{
    QTest::addColumn<uint>("count");
    QTest::addColumn<uint>("init");
    QTest::addColumn<uint>("request");
    QTest::addColumn<uint>("result");
    QTest::addColumn<bool>("signal");

    QTest::newRow("lower") << 4u << 3u << 2u << 2u << true;
    QTest::newRow("higher") << 4u << 1u << 2u << 2u << true;
    QTest::newRow("maximum") << 4u << 1u << 4u << 4u << true;
    QTest::newRow("above maximum") << 4u << 1u << 5u << 1u << false;
    QTest::newRow("minimum") << 4u << 2u << 1u << 1u << true;
    QTest::newRow("below minimum") << 4u << 2u << 0u << 2u << false;
    QTest::newRow("unchanged") << 4u << 2u << 2u << 2u << false;
}

void VirtualDesktopTest::test_current()
{
    auto vds = win::virtual_desktop_manager::self();
    QCOMPARE(vds->current(), 1);

    QFETCH(uint, count);
    vds->setCount(count);

    QFETCH(uint, init);
    QCOMPARE(vds->setCurrent(init), init != 1);
    QCOMPARE(vds->current(), init);

    QSignalSpy spy(vds, &win::virtual_desktop_manager::currentChanged);

    QFETCH(uint, request);
    QFETCH(uint, result);
    QFETCH(bool, signal);

    QCOMPARE(vds->setCurrent(request), signal);
    QCOMPARE(vds->current(), result);
    QCOMPARE(spy.isEmpty(), !signal);

    if (!spy.isEmpty()) {
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.count(), 2);
        QCOMPARE(arguments.at(0).type(), QVariant::UInt);
        QCOMPARE(arguments.at(1).type(), QVariant::UInt);
        QCOMPARE(arguments.at(0).toUInt(), init);
        QCOMPARE(arguments.at(1).toUInt(), result);
    }
}

void VirtualDesktopTest::test_current_change_on_count_change_data()
{
    QTest::addColumn<uint>("initCount");
    QTest::addColumn<uint>("initCurrent");
    QTest::addColumn<uint>("request");
    QTest::addColumn<uint>("current");
    QTest::addColumn<bool>("signal");

    QTest::newRow("increment") << 4u << 2u << 5u << 2u << false;
    QTest::newRow("increment on last") << 4u << 4u << 5u << 4u << false;
    QTest::newRow("decrement") << 4u << 2u << 3u << 2u << false;
    QTest::newRow("decrement on second last") << 4u << 3u << 3u << 3u << false;
    QTest::newRow("decrement on last") << 4u << 4u << 3u << 3u << true;
    QTest::newRow("multiple decrement") << 4u << 2u << 1u << 1u << true;
}

void VirtualDesktopTest::test_current_change_on_count_change()
{
    auto vds = win::virtual_desktop_manager::self();

    QFETCH(uint, initCount);
    QFETCH(uint, initCurrent);
    vds->setCount(initCount);
    vds->setCurrent(initCurrent);

    QSignalSpy spy(vds, &win::virtual_desktop_manager::currentChanged);

    QFETCH(uint, request);
    QFETCH(uint, current);
    QFETCH(bool, signal);

    vds->setCount(request);
    QCOMPARE(vds->current(), current);
    QCOMPARE(spy.isEmpty(), !signal);
}

void add_direction_columns()
{
    QTest::addColumn<uint>("initCount");
    QTest::addColumn<uint>("initCurrent");
    QTest::addColumn<bool>("wrap");
    QTest::addColumn<uint>("result");
}

template<typename T>
void VirtualDesktopTest::test_direction(QString const& actionName)
{
    auto vds = win::virtual_desktop_manager::self();

    QFETCH(uint, initCount);
    QFETCH(uint, initCurrent);

    vds->setCount(initCount);
    vds->setRows(2);
    vds->setCurrent(initCurrent);

    QFETCH(bool, wrap);
    QFETCH(uint, result);

    T functor;
    QCOMPARE(functor(nullptr, wrap)->x11DesktopNumber(), result);

    vds->setNavigationWrappingAround(wrap);

    auto action = vds->findChild<QAction*>(actionName);
    QVERIFY(action);
    action->trigger();

    QCOMPARE(vds->current(), result);
    QCOMPARE(functor(initCurrent, wrap), result);
}

void VirtualDesktopTest::next_data()
{
    add_direction_columns();

    QTest::newRow("one desktop, wrap") << 1u << 1u << true << 1u;
    QTest::newRow("one desktop, no wrap") << 1u << 1u << false << 1u;
    QTest::newRow("desktops, wrap") << 4u << 1u << true << 2u;
    QTest::newRow("desktops, no wrap") << 4u << 1u << false << 2u;
    QTest::newRow("desktops at end, wrap") << 4u << 4u << true << 1u;
    QTest::newRow("desktops at end, no wrap") << 4u << 4u << false << 4u;
}

void VirtualDesktopTest::next()
{
    test_direction<win::virtual_desktop_next>(QStringLiteral("Switch to Next Desktop"));
}

void VirtualDesktopTest::previous_data()
{
    add_direction_columns();

    QTest::newRow("one desktop, wrap") << 1u << 1u << true << 1u;
    QTest::newRow("one desktop, no wrap") << 1u << 1u << false << 1u;
    QTest::newRow("desktops, wrap") << 4u << 3u << true << 2u;
    QTest::newRow("desktops, no wrap") << 4u << 3u << false << 2u;
    QTest::newRow("desktops at start, wrap") << 4u << 1u << true << 4u;
    QTest::newRow("desktops at start, no wrap") << 4u << 1u << false << 1u;
}

void VirtualDesktopTest::previous()
{
    test_direction<win::virtual_desktop_previous>(QStringLiteral("Switch to Previous Desktop"));
}

void VirtualDesktopTest::left_data()
{
    add_direction_columns();

    QTest::newRow("one desktop, wrap") << 1u << 1u << true << 1u;
    QTest::newRow("one desktop, no wrap") << 1u << 1u << false << 1u;
    QTest::newRow("desktops, wrap, 1st row") << 4u << 2u << true << 1u;
    QTest::newRow("desktops, no wrap, 1st row") << 4u << 2u << false << 1u;
    QTest::newRow("desktops, wrap, 2nd row") << 4u << 4u << true << 3u;
    QTest::newRow("desktops, no wrap, 2nd row") << 4u << 4u << false << 3u;

    QTest::newRow("desktops at start, wrap, 1st row") << 4u << 1u << true << 2u;
    QTest::newRow("desktops at start, no wrap, 1st row") << 4u << 1u << false << 1u;
    QTest::newRow("desktops at start, wrap, 2nd row") << 4u << 3u << true << 4u;
    QTest::newRow("desktops at start, no wrap, 2nd row") << 4u << 3u << false << 3u;

    QTest::newRow("non symmetric, start") << 5u << 5u << false << 4u;
    QTest::newRow("non symmetric, end, no wrap") << 5u << 4u << false << 4u;
    QTest::newRow("non symmetric, end, wrap") << 5u << 4u << true << 5u;
}

void VirtualDesktopTest::left()
{
    test_direction<win::virtual_desktop_left>(QStringLiteral("Switch One Desktop to the Left"));
}

void VirtualDesktopTest::right_data()
{
    add_direction_columns();

    QTest::newRow("one desktop, wrap") << 1u << 1u << true << 1u;
    QTest::newRow("one desktop, no wrap") << 1u << 1u << false << 1u;
    QTest::newRow("desktops, wrap, 1st row") << 4u << 1u << true << 2u;
    QTest::newRow("desktops, no wrap, 1st row") << 4u << 1u << false << 2u;
    QTest::newRow("desktops, wrap, 2nd row") << 4u << 3u << true << 4u;
    QTest::newRow("desktops, no wrap, 2nd row") << 4u << 3u << false << 4u;

    QTest::newRow("desktops at start, wrap, 1st row") << 4u << 2u << true << 1u;
    QTest::newRow("desktops at start, no wrap, 1st row") << 4u << 2u << false << 2u;
    QTest::newRow("desktops at start, wrap, 2nd row") << 4u << 4u << true << 3u;
    QTest::newRow("desktops at start, no wrap, 2nd row") << 4u << 4u << false << 4u;

    QTest::newRow("non symmetric, start") << 5u << 4u << false << 5u;
    QTest::newRow("non symmetric, end, no wrap") << 5u << 5u << false << 5u;
    QTest::newRow("non symmetric, end, wrap") << 5u << 5u << true << 4u;
}

void VirtualDesktopTest::right()
{
    test_direction<win::virtual_desktop_right>(QStringLiteral("Switch One Desktop to the Right"));
}

void VirtualDesktopTest::above_data()
{
    add_direction_columns();

    QTest::newRow("one desktop, wrap") << 1u << 1u << true << 1u;
    QTest::newRow("one desktop, no wrap") << 1u << 1u << false << 1u;
    QTest::newRow("desktops, wrap, 1st column") << 4u << 3u << true << 1u;
    QTest::newRow("desktops, no wrap, 1st column") << 4u << 3u << false << 1u;
    QTest::newRow("desktops, wrap, 2nd column") << 4u << 4u << true << 2u;
    QTest::newRow("desktops, no wrap, 2nd column") << 4u << 4u << false << 2u;

    QTest::newRow("desktops at start, wrap, 1st column") << 4u << 1u << true << 3u;
    QTest::newRow("desktops at start, no wrap, 1st column") << 4u << 1u << false << 1u;
    QTest::newRow("desktops at start, wrap, 2nd column") << 4u << 2u << true << 4u;
    QTest::newRow("desktops at start, no wrap, 2nd column") << 4u << 2u << false << 2u;
}

void VirtualDesktopTest::above()
{
    test_direction<win::virtual_desktop_above>(QStringLiteral("Switch One Desktop Up"));
}

void VirtualDesktopTest::below_data()
{
    add_direction_columns();
    QTest::newRow("one desktop, wrap") << 1u << 1u << true << 1u;
    QTest::newRow("one desktop, no wrap") << 1u << 1u << false << 1u;
    QTest::newRow("desktops, wrap, 1st column") << 4u << 1u << true << 3u;
    QTest::newRow("desktops, no wrap, 1st column") << 4u << 1u << false << 3u;
    QTest::newRow("desktops, wrap, 2nd column") << 4u << 2u << true << 4u;
    QTest::newRow("desktops, no wrap, 2nd column") << 4u << 2u << false << 4u;

    QTest::newRow("desktops at start, wrap, 1st column") << 4u << 3u << true << 1u;
    QTest::newRow("desktops at start, no wrap, 1st column") << 4u << 3u << false << 3u;
    QTest::newRow("desktops at start, wrap, 2nd column") << 4u << 4u << true << 2u;
    QTest::newRow("desktops at start, no wrap, 2nd column") << 4u << 4u << false << 4u;
}

void VirtualDesktopTest::below()
{
    test_direction<win::virtual_desktop_below>(QStringLiteral("Switch One Desktop Down"));
}

void VirtualDesktopTest::update_grid_data()
{
    QTest::addColumn<uint>("initCount");
    QTest::addColumn<QSize>("size");
    QTest::addColumn<Qt::Orientation>("orientation");
    QTest::addColumn<QPoint>("coords");
    QTest::addColumn<uint>("desktop");
    const Qt::Orientation h = Qt::Horizontal;
    const Qt::Orientation v = Qt::Vertical;

    QTest::newRow("one desktop, h") << 1u << QSize(1, 1) << h << QPoint(0, 0) << 1u;
    QTest::newRow("one desktop, v") << 1u << QSize(1, 1) << v << QPoint(0, 0) << 1u;
    QTest::newRow("one desktop, h, 0") << 1u << QSize(1, 1) << h << QPoint(1, 0) << 0u;
    QTest::newRow("one desktop, v, 0") << 1u << QSize(1, 1) << v << QPoint(0, 1) << 0u;

    QTest::newRow("two desktops, h, 1") << 2u << QSize(2, 1) << h << QPoint(0, 0) << 1u;
    QTest::newRow("two desktops, h, 2") << 2u << QSize(2, 1) << h << QPoint(1, 0) << 2u;
    QTest::newRow("two desktops, h, 3") << 2u << QSize(2, 1) << h << QPoint(0, 1) << 0u;
    QTest::newRow("two desktops, h, 4") << 2u << QSize(2, 1) << h << QPoint(2, 0) << 0u;

    QTest::newRow("two desktops, v, 1") << 2u << QSize(2, 1) << v << QPoint(0, 0) << 1u;
    QTest::newRow("two desktops, v, 2") << 2u << QSize(2, 1) << v << QPoint(1, 0) << 2u;
    QTest::newRow("two desktops, v, 3") << 2u << QSize(2, 1) << v << QPoint(0, 1) << 0u;
    QTest::newRow("two desktops, v, 4") << 2u << QSize(2, 1) << v << QPoint(2, 0) << 0u;

    QTest::newRow("four desktops, h, one row, 1") << 4u << QSize(4, 1) << h << QPoint(0, 0) << 1u;
    QTest::newRow("four desktops, h, one row, 2") << 4u << QSize(4, 1) << h << QPoint(1, 0) << 2u;
    QTest::newRow("four desktops, h, one row, 3") << 4u << QSize(4, 1) << h << QPoint(2, 0) << 3u;
    QTest::newRow("four desktops, h, one row, 4") << 4u << QSize(4, 1) << h << QPoint(3, 0) << 4u;

    QTest::newRow("four desktops, v, one column, 1")
        << 4u << QSize(1, 4) << v << QPoint(0, 0) << 1u;
    QTest::newRow("four desktops, v, one column, 2")
        << 4u << QSize(1, 4) << v << QPoint(0, 1) << 2u;
    QTest::newRow("four desktops, v, one column, 3")
        << 4u << QSize(1, 4) << v << QPoint(0, 2) << 3u;
    QTest::newRow("four desktops, v, one column, 4")
        << 4u << QSize(1, 4) << v << QPoint(0, 3) << 4u;

    QTest::newRow("four desktops, h, grid, 1") << 4u << QSize(2, 2) << h << QPoint(0, 0) << 1u;
    QTest::newRow("four desktops, h, grid, 2") << 4u << QSize(2, 2) << h << QPoint(1, 0) << 2u;
    QTest::newRow("four desktops, h, grid, 3") << 4u << QSize(2, 2) << h << QPoint(0, 1) << 3u;
    QTest::newRow("four desktops, h, grid, 4") << 4u << QSize(2, 2) << h << QPoint(1, 1) << 4u;
    QTest::newRow("four desktops, h, grid, 0/3") << 4u << QSize(2, 2) << h << QPoint(0, 3) << 0u;

    QTest::newRow("three desktops, h, grid, 1") << 3u << QSize(2, 2) << h << QPoint(0, 0) << 1u;
    QTest::newRow("three desktops, h, grid, 2") << 3u << QSize(2, 2) << h << QPoint(1, 0) << 2u;
    QTest::newRow("three desktops, h, grid, 3") << 3u << QSize(2, 2) << h << QPoint(0, 1) << 3u;
    QTest::newRow("three desktops, h, grid, 4") << 3u << QSize(2, 2) << h << QPoint(1, 1) << 0u;
}

void VirtualDesktopTest::update_grid()
{
    auto vds = win::virtual_desktop_manager::self();

    QFETCH(uint, initCount);
    vds->setCount(initCount);

    win::virtual_desktop_grid grid;

    QFETCH(QSize, size);
    QFETCH(Qt::Orientation, orientation);
    QCOMPARE(vds->desktops().count(), int(initCount));

    grid.update(size, orientation, vds->desktops());
    QCOMPARE(grid.size(), size);
    QCOMPARE(grid.width(), size.width());
    QCOMPARE(grid.height(), size.height());

    QFETCH(QPoint, coords);
    QFETCH(uint, desktop);
    QCOMPARE(grid.at(coords), vds->desktopForX11Id(desktop));

    if (desktop != 0) {
        QCOMPARE(grid.gridCoords(desktop), coords);
    }
}

void VirtualDesktopTest::update_layout_data()
{
    QTest::addColumn<uint>("desktop");
    QTest::addColumn<QSize>("result");

    // Grid does not shrink for some reason and stays at 2x2 from previous test. Needs to be
    // investigated.
#if 0
    QTest::newRow("01") << 1u << QSize(1, 1);
    QTest::newRow("02") << 2u << QSize(1, 2);
#endif
    QTest::newRow("03") << 3u << QSize(2, 2);
    QTest::newRow("04") << 4u << QSize(2, 2);
    QTest::newRow("05") << 5u << QSize(3, 2);
    QTest::newRow("06") << 6u << QSize(3, 2);
    QTest::newRow("07") << 7u << QSize(4, 2);
    QTest::newRow("08") << 8u << QSize(4, 2);
    QTest::newRow("09") << 9u << QSize(5, 2);
    QTest::newRow("10") << 10u << QSize(5, 2);
    QTest::newRow("11") << 11u << QSize(6, 2);
    QTest::newRow("12") << 12u << QSize(6, 2);
    QTest::newRow("13") << 13u << QSize(7, 2);
    QTest::newRow("14") << 14u << QSize(7, 2);
    QTest::newRow("15") << 15u << QSize(8, 2);
    QTest::newRow("16") << 16u << QSize(8, 2);
    QTest::newRow("17") << 17u << QSize(9, 2);
    QTest::newRow("18") << 18u << QSize(9, 2);
    QTest::newRow("19") << 19u << QSize(10, 2);
    QTest::newRow("20") << 20u << QSize(10, 2);
}

void VirtualDesktopTest::update_layout()
{
    auto vds = win::virtual_desktop_manager::self();

    QSignalSpy spy(vds, &win::virtual_desktop_manager::layoutChanged);
    QVERIFY(spy.isValid());

    // call update layout - implicitly through setCount
    QFETCH(uint, desktop);
    QFETCH(QSize, result);

    if (desktop == 1) {
        // Must be changed back and forth from our default so the spy fires.
        vds->setCount(2);
    }

    vds->setCount(desktop);
    vds->setRows(2);

    //    QEXPECT_FAIL("01", "Should rows() reduce to VDs count? Happened in old VD test.",
    //    Continue);
    QCOMPARE(vds->grid().size(), result);
    QVERIFY(!spy.empty());

    auto const& arguments = spy.back();
    QCOMPARE(arguments.at(0).toInt(), result.width());
    QCOMPARE(arguments.at(1).toInt(), result.height());

    spy.clear();

    // calling update layout again should not change anything
    vds->updateLayout();
    QCOMPARE(vds->grid().size(), result);
    QCOMPARE(spy.count(), 1);

    auto const& arguments2 = spy.back();
    QCOMPARE(arguments2.at(0).toInt(), result.width());
    QCOMPARE(arguments2.at(1).toInt(), result.height());
}

void VirtualDesktopTest::test_name_data()
{
    QTest::addColumn<uint>("initCount");
    QTest::addColumn<uint>("desktop");
    QTest::addColumn<QString>("desktopName");

    QTest::newRow("desktop 1") << 4u << 1u << "Desktop 1";
    QTest::newRow("desktop 2") << 4u << 2u << "Desktop 2";
    QTest::newRow("desktop 3") << 4u << 3u << "Desktop 3";
    QTest::newRow("desktop 4") << 4u << 4u << "Desktop 4";
    QTest::newRow("desktop 5") << 5u << 5u << "Desktop 5";
}

void VirtualDesktopTest::test_name()
{
    auto vds = win::virtual_desktop_manager::self();

    QFETCH(uint, initCount);
    QFETCH(uint, desktop);

    vds->setCount(initCount);
    QTEST(vds->name(desktop), "desktopName");
}

void VirtualDesktopTest::test_switch_to_shortcuts()
{
    auto vds = win::virtual_desktop_manager::self();
    vds->setCount(vds->maximum());
    vds->setCurrent(vds->maximum());

    QCOMPARE(vds->current(), vds->maximum());
    //    vds->initShortcuts();
    auto const toDesktop = QStringLiteral("Switch to Desktop %1");

    for (uint i = 1; i <= vds->maximum(); ++i) {
        const QString desktop(toDesktop.arg(i));
        QAction* action = vds->findChild<QAction*>(desktop);
        QVERIFY2(action, desktop.toUtf8().constData());
        action->trigger();
        QCOMPARE(vds->current(), i);
    }

    // invoke switchTo not from a QAction
    QMetaObject::invokeMethod(vds, "slotSwitchTo");

    // should still be on max
    QCOMPARE(vds->current(), vds->maximum());
}

void VirtualDesktopTest::test_change_rows()
{
    auto vds = win::virtual_desktop_manager::self();

    vds->setCount(4);
    vds->setRows(4);
    QCOMPARE(vds->rows(), 4);

    vds->setRows(5);
    QCOMPARE(vds->rows(), 4);

    vds->setCount(2);

    // TODO(romangg): Fails when compiled with Xwayland and passes otherwise. The root cause seems
    //                to be the update from root info in win::virtual_desktop_manager::updateLayout.
#if 0
    QEXPECT_FAIL("", "Should rows() reduce to VDs count? Happened in old VD test.", Continue);
    QCOMPARE(vds->rows(), 2);
#endif
}

void VirtualDesktopTest::test_load()
{
    auto vds = win::virtual_desktop_manager::self();

    // No config yet, load should not change anything.
    vds->load();
    QCOMPARE(vds->count(), 1);

    // Empty config should create one desktop.
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    vds->setConfig(config);
    vds->load();
    QCOMPARE(vds->count(), 1);

    // Setting a sensible number.
    config->group("Desktops").writeEntry("Number", 4);
    vds->load();
    QCOMPARE(vds->count(), 4);

    // Setting the config value and reloading should update.
    config->group("Desktops").writeEntry("Number", 5);
    vds->load();
    QCOMPARE(vds->count(), 5);
}

void VirtualDesktopTest::test_save()
{
    auto vds = win::virtual_desktop_manager::self();
    vds->setCount(4);

    // No config yet, just to ensure it actually works.
    vds->save();

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    vds->setConfig(config);

    QEXPECT_FAIL("", "Entry exists already. Was not the case in the old VD test.", Continue);
    QCOMPARE(config->hasGroup("Desktops"), false);

    // Now save should create the group "Desktops".
    vds->save();
    QCOMPARE(config->hasGroup("Desktops"), true);

    auto desktops = config->group("Desktops");
    QCOMPARE(desktops.readEntry<int>("Number", 1), 4);
    QCOMPARE(desktops.hasKey("Name_1"), false);
    QCOMPARE(desktops.hasKey("Name_2"), false);
    QCOMPARE(desktops.hasKey("Name_3"), false);
    QCOMPARE(desktops.hasKey("Name_4"), false);
}

void VirtualDesktopTest::testNetCurrentDesktop()
{
    if (!kwinApp()->x11Connection()) {
        QSKIP("Skipped on Wayland only");
    }
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 1u);
    win::virtual_desktop_manager::self()->setCount(4);
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 4u);

    base::x11::xcb::atom currentDesktopAtom("_NET_CURRENT_DESKTOP", connection());
    QVERIFY(currentDesktopAtom.is_valid());
    base::x11::xcb::property currentDesktop(
        0, kwinApp()->x11RootWindow(), currentDesktopAtom, XCB_ATOM_CARDINAL, 0, 1);
    bool ok = true;
    QCOMPARE(currentDesktop.value(0, &ok), 0);
    QVERIFY(ok);

    // go to desktop 2
    win::virtual_desktop_manager::self()->setCurrent(2);
    currentDesktop = base::x11::xcb::property(
        0, kwinApp()->x11RootWindow(), currentDesktopAtom, XCB_ATOM_CARDINAL, 0, 1);
    QCOMPARE(currentDesktop.value(0, &ok), 1);
    QVERIFY(ok);

    // go to desktop 3
    win::virtual_desktop_manager::self()->setCurrent(3);
    currentDesktop = base::x11::xcb::property(
        0, kwinApp()->x11RootWindow(), currentDesktopAtom, XCB_ATOM_CARDINAL, 0, 1);
    QCOMPARE(currentDesktop.value(0, &ok), 2);
    QVERIFY(ok);

    // go to desktop 4
    win::virtual_desktop_manager::self()->setCurrent(4);
    currentDesktop = base::x11::xcb::property(
        0, kwinApp()->x11RootWindow(), currentDesktopAtom, XCB_ATOM_CARDINAL, 0, 1);
    QCOMPARE(currentDesktop.value(0, &ok), 3);
    QVERIFY(ok);

    // and back to first
    win::virtual_desktop_manager::self()->setCurrent(1);
    currentDesktop = base::x11::xcb::property(
        0, kwinApp()->x11RootWindow(), currentDesktopAtom, XCB_ATOM_CARDINAL, 0, 1);
    QCOMPARE(currentDesktop.value(0, &ok), 0);
    QVERIFY(ok);
}

void VirtualDesktopTest::testLastDesktopRemoved()
{
    // first create a new desktop
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 1u);
    win::virtual_desktop_manager::self()->setCount(2);
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 2u);

    // switch to last desktop
    win::virtual_desktop_manager::self()->setCurrent(
        win::virtual_desktop_manager::self()->desktops().last());
    QCOMPARE(win::virtual_desktop_manager::self()->current(), 2u);

    // now create a window on this desktop
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(client);
    QCOMPARE(client->desktop(), 2);
    QSignalSpy desktopPresenceChangedSpy(client, &win::wayland::window::desktopPresenceChanged);
    QVERIFY(desktopPresenceChangedSpy.isValid());

    QCOMPARE(client->desktops().count(), 1u);
    QCOMPARE(win::virtual_desktop_manager::self()->currentDesktop(), client->desktops().first());

    // and remove last desktop
    win::virtual_desktop_manager::self()->setCount(1);
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 1u);
    // now the client should be moved as well
    QTRY_COMPARE(desktopPresenceChangedSpy.count(), 1);
    QCOMPARE(client->desktop(), 1);

    QCOMPARE(client->desktops().count(), 1u);
    QCOMPARE(win::virtual_desktop_manager::self()->currentDesktop(), client->desktops().first());
}

void VirtualDesktopTest::testWindowOnMultipleDesktops()
{
    // first create two new desktops
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 1u);
    win::virtual_desktop_manager::self()->setCount(3);
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 3u);

    // switch to last desktop
    win::virtual_desktop_manager::self()->setCurrent(
        win::virtual_desktop_manager::self()->desktops().last());
    QCOMPARE(win::virtual_desktop_manager::self()->current(), 3u);

    // now create a window on this desktop
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(client);
    QCOMPARE(client->desktop(), 3u);
    QSignalSpy desktopPresenceChangedSpy(client, &win::wayland::window::desktopPresenceChanged);
    QVERIFY(desktopPresenceChangedSpy.isValid());

    QCOMPARE(client->desktops().count(), 1u);
    QCOMPARE(win::virtual_desktop_manager::self()->currentDesktop(), client->desktops().first());

    // Set the window on desktop 2 as well
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(2));
    QCOMPARE(client->desktops().count(), 2u);
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[2], client->desktops()[0]);
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[1], client->desktops()[1]);
    QVERIFY(client->isOnDesktop(2));
    QVERIFY(client->isOnDesktop(3));

    // leave desktop 3
    win::leave_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(3));
    QCOMPARE(client->desktops().count(), 1u);
    // leave desktop 2
    win::leave_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(2));
    QCOMPARE(client->desktops().count(), 0u);
    // we should be on all desktops now
    QVERIFY(client->isOnAllDesktops());
    // put on desktop 1
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(1));
    QVERIFY(client->isOnDesktop(1));
    QVERIFY(!client->isOnDesktop(2));
    QVERIFY(!client->isOnDesktop(3));
    QCOMPARE(client->desktops().count(), 1u);
    // put on desktop 2
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(2));
    QVERIFY(client->isOnDesktop(1));
    QVERIFY(client->isOnDesktop(2));
    QVERIFY(!client->isOnDesktop(3));
    QCOMPARE(client->desktops().count(), 2u);
    // put on desktop 3
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(3));
    QVERIFY(client->isOnDesktop(1));
    QVERIFY(client->isOnDesktop(2));
    QVERIFY(client->isOnDesktop(3));
    QCOMPARE(client->desktops().count(), 3u);

    // entering twice dooes nothing
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(3));
    QCOMPARE(client->desktops().count(), 3u);

    // adding to "all desktops" results in just that one desktop
    win::set_on_all_desktops(client, true);
    QCOMPARE(client->desktops().count(), 0u);
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(3));
    QVERIFY(client->isOnDesktop(3));
    QCOMPARE(client->desktops().count(), 1u);

    // leaving a desktop on "all desktops" puts on everything else
    win::set_on_all_desktops(client, true);
    QCOMPARE(client->desktops().count(), 0u);
    win::leave_desktop(client, win::virtual_desktop_manager::self()->desktopForX11Id(3));
    QVERIFY(client->isOnDesktop(1));
    QVERIFY(client->isOnDesktop(2));
    QCOMPARE(client->desktops().count(), 2u);
}

void VirtualDesktopTest::testRemoveDesktopWithWindow()
{
    // first create two new desktops
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 1u);
    win::virtual_desktop_manager::self()->setCount(3);
    QCOMPARE(win::virtual_desktop_manager::self()->count(), 3u);

    // switch to last desktop
    win::virtual_desktop_manager::self()->setCurrent(
        win::virtual_desktop_manager::self()->desktops().last());
    QCOMPARE(win::virtual_desktop_manager::self()->current(), 3u);

    // now create a window on this desktop
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(client);
    QCOMPARE(client->desktop(), 3u);
    QSignalSpy desktopPresenceChangedSpy(client, &win::wayland::window::desktopPresenceChanged);
    QVERIFY(desktopPresenceChangedSpy.isValid());

    QCOMPARE(client->desktops().count(), 1u);
    QCOMPARE(win::virtual_desktop_manager::self()->currentDesktop(), client->desktops().first());

    // Set the window on desktop 2 as well
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktops()[1]);
    QCOMPARE(client->desktops().count(), 2u);
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[2], client->desktops()[0]);
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[1], client->desktops()[1]);
    QVERIFY(client->isOnDesktop(2));
    QVERIFY(client->isOnDesktop(3));

    // remove desktop 3
    win::virtual_desktop_manager::self()->setCount(2);
    QCOMPARE(client->desktops().count(), 1u);
    // window is only on desktop 2
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[1], client->desktops()[0]);

    // Again 3 desktops
    win::virtual_desktop_manager::self()->setCount(3);
    // move window to be only on desktop 3
    win::enter_desktop(client, win::virtual_desktop_manager::self()->desktops()[2]);
    win::leave_desktop(client, win::virtual_desktop_manager::self()->desktops()[1]);
    QCOMPARE(client->desktops().count(), 1u);
    // window is only on desktop 3
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[2], client->desktops()[0]);

    // remove desktop 3
    win::virtual_desktop_manager::self()->setCount(2);
    QCOMPARE(client->desktops().count(), 1u);
    // window is only on desktop 2
    QCOMPARE(win::virtual_desktop_manager::self()->desktops()[1], client->desktops()[0]);
}

}

WAYLANDTEST_MAIN(KWin::VirtualDesktopTest)
#include "virtual_desktop_test.moc"
