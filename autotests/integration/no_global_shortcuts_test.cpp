/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2018 Martin Flöser <mgraesslin@kde.org>

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

#include "input/cursor.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "screens.h"
#include "wayland_server.h"
#include "win/screen_edges.h"
#include "workspace.h"

#include <KConfigGroup>
#include <KGlobalAccel>

#include <QDBusConnection>

#include <linux/input.h>

using namespace Wrapland::Client;

static const QString s_serviceName = QStringLiteral("org.kde.KWin.Test.ModifierOnlyShortcut");
static const QString s_path = QStringLiteral("/Test");

Q_DECLARE_METATYPE(KWin::ElectricBorder)

namespace KWin
{

/**
 * This test verifies the NoGlobalShortcuts initialization flag
 */
class NoGlobalShortcutsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testTrigger_data();
    void testTrigger();
    void testKGlobalAccel();
    void testPointerShortcut();
    void testAxisShortcut_data();
    void testAxisShortcut();
    void testScreenEdge();
};

class Target : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Test.ModifierOnlyShortcut")

public:
    Target();
    ~Target() override;

public Q_SLOTS:
    Q_SCRIPTABLE void shortcut();

Q_SIGNALS:
    void shortcutTriggered();
};

Target::Target()
    : QObject()
{
    QDBusConnection::sessionBus().registerService(s_serviceName);
    QDBusConnection::sessionBus().registerObject(
        s_path, s_serviceName, this, QDBusConnection::ExportScriptableSlots);
}

Target::~Target()
{
    QDBusConnection::sessionBus().unregisterObject(s_path);
    QDBusConnection::sessionBus().unregisterService(s_serviceName);
}

void Target::shortcut()
{
    Q_EMIT shortcutTriggered();
}

void NoGlobalShortcutsTest::initTestCase()
{
    qRegisterMetaType<KWin::ElectricBorder>("ElectricBorder");

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void NoGlobalShortcutsTest::init()
{
    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void NoGlobalShortcutsTest::cleanup()
{
}

void NoGlobalShortcutsTest::testTrigger_data()
{
    QTest::addColumn<QStringList>("metaConfig");
    QTest::addColumn<QStringList>("altConfig");
    QTest::addColumn<QStringList>("controlConfig");
    QTest::addColumn<QStringList>("shiftConfig");
    QTest::addColumn<int>("modifier");
    QTest::addColumn<QList<int>>("nonTriggeringMods");

    const QStringList trigger
        = QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")};
    const QStringList e = QStringList();

    QTest::newRow("leftMeta") << trigger << e << e << e << KEY_LEFTMETA
                              << QList<int>{KEY_LEFTALT,
                                            KEY_RIGHTALT,
                                            KEY_LEFTCTRL,
                                            KEY_RIGHTCTRL,
                                            KEY_LEFTSHIFT,
                                            KEY_RIGHTSHIFT};
    QTest::newRow("rightMeta") << trigger << e << e << e << KEY_RIGHTMETA
                               << QList<int>{KEY_LEFTALT,
                                             KEY_RIGHTALT,
                                             KEY_LEFTCTRL,
                                             KEY_RIGHTCTRL,
                                             KEY_LEFTSHIFT,
                                             KEY_RIGHTSHIFT};
    QTest::newRow("leftAlt") << e << trigger << e << e << KEY_LEFTALT
                             << QList<int>{KEY_LEFTMETA,
                                           KEY_RIGHTMETA,
                                           KEY_LEFTCTRL,
                                           KEY_RIGHTCTRL,
                                           KEY_LEFTSHIFT,
                                           KEY_RIGHTSHIFT};
    QTest::newRow("rightAlt") << e << trigger << e << e << KEY_RIGHTALT
                              << QList<int>{KEY_LEFTMETA,
                                            KEY_RIGHTMETA,
                                            KEY_LEFTCTRL,
                                            KEY_RIGHTCTRL,
                                            KEY_LEFTSHIFT,
                                            KEY_RIGHTSHIFT};
    QTest::newRow("leftControl") << e << e << trigger << e << KEY_LEFTCTRL
                                 << QList<int>{KEY_LEFTALT,
                                               KEY_RIGHTALT,
                                               KEY_LEFTMETA,
                                               KEY_RIGHTMETA,
                                               KEY_LEFTSHIFT,
                                               KEY_RIGHTSHIFT};
    QTest::newRow("rightControl") << e << e << trigger << e << KEY_RIGHTCTRL
                                  << QList<int>{KEY_LEFTALT,
                                                KEY_RIGHTALT,
                                                KEY_LEFTMETA,
                                                KEY_RIGHTMETA,
                                                KEY_LEFTSHIFT,
                                                KEY_RIGHTSHIFT};
    QTest::newRow("leftShift")
        << e << e << e << trigger << KEY_LEFTSHIFT
        << QList<int>{
               KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTMETA, KEY_RIGHTMETA};
    QTest::newRow("rightShift")
        << e << e << e << trigger << KEY_RIGHTSHIFT
        << QList<int>{
               KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTMETA, KEY_RIGHTMETA};
}

void NoGlobalShortcutsTest::testTrigger()
{
    // test based on ModifierOnlyShortcutTest::testTrigger
    Target target;
    QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
    QVERIFY(triggeredSpy.isValid());

    KConfigGroup group = kwinApp()->config()->group("ModifierOnlyShortcuts");
    QFETCH(QStringList, metaConfig);
    QFETCH(QStringList, altConfig);
    QFETCH(QStringList, shiftConfig);
    QFETCH(QStringList, controlConfig);
    group.writeEntry("Meta", metaConfig);
    group.writeEntry("Alt", altConfig);
    group.writeEntry("Shift", shiftConfig);
    group.writeEntry("Control", controlConfig);
    group.sync();
    workspace()->slotReconfigure();

    // configured shortcut should trigger
    quint32 timestamp = 1;
    QFETCH(int, modifier);
    Test::keyboard_key_pressed(modifier, timestamp++);
    Test::keyboard_key_released(modifier, timestamp++);
    QCOMPARE(triggeredSpy.count(), 0);

    // the other shortcuts should not trigger
    QFETCH(QList<int>, nonTriggeringMods);
    for (auto it = nonTriggeringMods.constBegin(), end = nonTriggeringMods.constEnd(); it != end;
         it++) {
        Test::keyboard_key_pressed(*it, timestamp++);
        Test::keyboard_key_released(*it, timestamp++);
        QCOMPARE(triggeredSpy.count(), 0);
    }
}

void NoGlobalShortcutsTest::testKGlobalAccel()
{
    std::unique_ptr<QAction> action(new QAction(nullptr));
    action->setProperty("componentName", QStringLiteral(KWIN_NAME));
    action->setObjectName(QStringLiteral("globalshortcuts-test-meta-shift-w"));
    QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
    QVERIFY(triggeredSpy.isValid());
    KGlobalAccel::self()->setShortcut(action.get(),
                                      QList<QKeySequence>{Qt::META + Qt::SHIFT + Qt::Key_W},
                                      KGlobalAccel::NoAutoloading);
    kwinApp()->input->redirect->registerShortcut(Qt::META + Qt::SHIFT + Qt::Key_W, action.get());

    // press meta+shift+w
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::MetaModifier);

    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input),
             Qt::ShiftModifier | Qt::MetaModifier);

    Test::keyboard_key_pressed(KEY_W, timestamp++);
    Test::keyboard_key_released(KEY_W, timestamp++);

    // release meta+shift
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);

    QVERIFY(!triggeredSpy.wait());
    QCOMPARE(triggeredSpy.count(), 0);
}

void NoGlobalShortcutsTest::testPointerShortcut()
{
    // based on LockScreenTest::testPointerShortcut
    std::unique_ptr<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.get(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());
    kwinApp()->input->redirect->registerPointerShortcut(
        Qt::MetaModifier, Qt::LeftButton, action.get());

    // try to trigger the shortcut
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 0);
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 0);
}

void NoGlobalShortcutsTest::testAxisShortcut_data()
{
    QTest::addColumn<Qt::Orientation>("direction");
    QTest::addColumn<int>("sign");

    QTest::newRow("up") << Qt::Vertical << 1;
    QTest::newRow("down") << Qt::Vertical << -1;
    QTest::newRow("left") << Qt::Horizontal << 1;
    QTest::newRow("right") << Qt::Horizontal << -1;
}

void NoGlobalShortcutsTest::testAxisShortcut()
{
    // based on LockScreenTest::testAxisShortcut
    std::unique_ptr<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.get(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());
    QFETCH(Qt::Orientation, direction);
    QFETCH(int, sign);
    PointerAxisDirection axisDirection = PointerAxisUp;
    if (direction == Qt::Vertical) {
        axisDirection = sign > 0 ? PointerAxisUp : PointerAxisDown;
    } else {
        axisDirection = sign > 0 ? PointerAxisLeft : PointerAxisRight;
    }
    kwinApp()->input->redirect->registerAxisShortcut(Qt::MetaModifier, axisDirection, action.get());

    // try to trigger the shortcut
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    if (direction == Qt::Vertical)
        Test::pointer_axis_vertical(sign * 5.0, timestamp++, 0);
    else
        Test::pointer_axis_horizontal(sign * 5.0, timestamp++, 0);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 0);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 0);
}

void NoGlobalShortcutsTest::testScreenEdge()
{
    // based on LockScreenTest::testScreenEdge
    QSignalSpy screenEdgeSpy(workspace()->edges.get(), &win::screen_edger::approaching);
    QVERIFY(screenEdgeSpy.isValid());
    QCOMPARE(screenEdgeSpy.count(), 0);

    quint32 timestamp = 1;
    Test::pointer_motion_absolute({5, 5}, timestamp++);
    QCOMPARE(screenEdgeSpy.count(), 0);
}

}

WAYLANDTEST_MAIN_FLAGS(KWin::NoGlobalShortcutsTest,
                       KWin::base::wayland::start_options::no_global_shortcuts)
#include "no_global_shortcuts_test.moc"
