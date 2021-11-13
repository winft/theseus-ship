/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "input/xkb/layout_manager.h"
#include "kwin_wayland_test.h"
#include "platform.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/wayland/window.h"

#include <KConfigGroup>
#include <KGlobalAccel>

#include <Wrapland/Client/surface.h>

#include <QAction>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>

#include <linux/input.h>

namespace KWin
{

class keyboard_layout_test : public QObject
{
    Q_OBJECT
public:
    struct test_spies {
        struct v1 {
            v1(keyboard_layout_test* test)
                : layout_changed{test, &keyboard_layout_test::layout_changed}
                , layouts_reconfigured{test, &keyboard_layout_test::layout_list_changed}
            {
            }
            QSignalSpy layout_changed;
            QSignalSpy layouts_reconfigured;
        } v1;

        test_spies(keyboard_layout_test* test)
            : v1{test}
        {
        }
    };

    keyboard_layout_test()
    {
        qRegisterMetaType<win::wayland::window*>();

        spies = std::make_unique<test_spies>(this);
    }

Q_SIGNALS:
    void layout_changed(uint index);
    void layout_list_changed();

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_reconfigure();
    void test_change_layout_through_dbus();
    void test_per_layout_shortcut();
    void test_dbus_service_export();
    void test_virtual_desktop_policy();
    void test_window_policy();
    void test_application_policy();
    void test_num_lock();

private:
    void reconfigure_layouts();
    void reset_layouts();
    auto change_layout(uint index);
    void call_session(QString const& method);

    KConfigGroup layout_group;
    std::unique_ptr<test_spies> spies;
};

void keyboard_layout_test::reconfigure_layouts()
{
    spies->v1.layouts_reconfigured.clear();

    // Create DBus signal to reload.
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/Layouts"),
                                                      QStringLiteral("org.kde.keyboard"),
                                                      QStringLiteral("reloadConfig"));
    QVERIFY(QDBusConnection::sessionBus().send(message));

    QVERIFY(spies->v1.layouts_reconfigured.wait(1000));
    QCOMPARE(spies->v1.layouts_reconfigured.count(), 1);
}

void keyboard_layout_test::reset_layouts()
{
    // Switch Policy to destroy layouts from memory. On return to original Policy they should reload
    // from disk.
    call_session(QStringLiteral("aboutToSaveSession"));

    auto const policy = layout_group.readEntry("SwitchMode", "Global");

    if (policy == QLatin1String("Global")) {
        layout_group.writeEntry("SwitchMode", "Desktop");
    } else {
        layout_group.deleteEntry("SwitchMode");
    }
    reconfigure_layouts();

    layout_group.writeEntry("SwitchMode", policy);
    reconfigure_layouts();

    call_session(QStringLiteral("loadSession"));
}

auto keyboard_layout_test::change_layout(uint index)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.keyboard"),
                                                      QStringLiteral("/Layouts"),
                                                      QStringLiteral("org.kde.KeyboardLayouts"),
                                                      QStringLiteral("setLayout"));
    msg << index;
    return QDBusConnection::sessionBus().asyncCall(msg);
}

void keyboard_layout_test::call_session(QString const& method)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                      QStringLiteral("/Session"),
                                                      QStringLiteral("org.kde.KWin.Session"),
                                                      method);
    msg << QLatin1String(); // session name
    QVERIFY(QDBusConnection::sessionBus().call(msg).type() != QDBusMessage::ErrorMessage);
}

void keyboard_layout_test::initTestCase()
{
    QVERIFY(spies->v1.layout_changed.isValid());
    QVERIFY(spies->v1.layouts_reconfigured.isValid());

    constexpr auto service_name{"org.kde.keyboard"};

    {
        constexpr auto path_v1_name{"/Layouts"};
        constexpr auto interface_v1_name{"org.kde.KeyboardLayouts"};

        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v1_name,
                                                      interface_v1_name,
                                                      "layoutChanged",
                                                      this,
                                                      SIGNAL(layout_changed(uint))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v1_name,
                                                      interface_v1_name,
                                                      "layoutListChanged",
                                                      this,
                                                      SIGNAL(layout_list_changed())));
    }

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    kwinApp()->platform->setInitialWindowSize(QSize(1280, 1024));

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    kwinApp()->setKxkbConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));
    kwinApp()->setInputConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    layout_group = kwinApp()->kxkbConfig()->group("Layout");
    layout_group.deleteGroup();

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void keyboard_layout_test::init()
{
    Test::setup_wayland_connection();
}

void keyboard_layout_test::cleanup()
{
    Test::destroy_wayland_connection();

    // We always reset to a us layout.
    if (auto xkb = input::xkb::get_primary_xkb_keyboard();
        xkb->layout_name() != "English (US)" || xkb->layouts_count() != 1) {
        layout_group.writeEntry("LayoutList", QStringLiteral("us"));
        layout_group.sync();
        reconfigure_layouts();
    }
}

void keyboard_layout_test::test_reconfigure()
{
    // Verifies that we can change the keymap.

    // Default should be a keymap with only us layout.
    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 1u);
    QCOMPARE(xkb->layout_name(), "English (US)");
    QCOMPARE(xkb->layouts_count(), 1);
    QCOMPARE(xkb->layout_name_from_index(0), "English (US)");

    // Create a new keymap.
    auto layout_group = kwinApp()->kxkbConfig()->group("Layout");
    layout_group.writeEntry("LayoutList", QStringLiteral("de,us"));
    layout_group.sync();

    reconfigure_layouts();

    // Now we should have two layouts.
    QCOMPARE(xkb->layouts_count(), 2u);

    // Default layout is German.
    QCOMPARE(xkb->layout_name(), "German");
    QCOMPARE(xkb->layouts_count(), 2);
    QCOMPARE(xkb->layout_name_from_index(0), "German");
    QCOMPARE(xkb->layout_name_from_index(1), "English (US)");
}

void keyboard_layout_test::test_change_layout_through_dbus()
{
    // This test verifies that the layout can be changed through DBus.

    // First configure layouts.
    enum Layout { de, us, de_neo, bad };
    layout_group.writeEntry("LayoutList", QStringLiteral("de,us,de(neo)"));
    layout_group.sync();
    reconfigure_layouts();

    // Now we should have three layouts.
    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 3u);

    // Default layout is German.
    xkb->switch_to_layout(0);
    QCOMPARE(xkb->layout_name(), "German");

    // Place garbage to layout entry.
    layout_group.writeEntry("LayoutDefaultFoo", "garbage");

    // Make sure the garbage is wiped out on saving.
    reset_layouts();
    QVERIFY(!layout_group.hasKey("LayoutDefaultFoo"));

    // Now change through DBus to English.
    auto reply = change_layout(Layout::us);
    reply.waitForFinished();
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), true);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    spies->v1.layout_changed.clear();

    // Layout should persist after reset.
    reset_layouts();
    QCOMPARE(xkb->layout_name(), "English (US)");
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    spies->v1.layout_changed.clear();

    // Switch to a layout which does not exist.
    reply = change_layout(Layout::bad);
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), false);
    QCOMPARE(xkb->layout_name(), "English (US)");
    QVERIFY(!spies->v1.layout_changed.wait(1000));

    // Switch to another layout should work.
    reply = change_layout(Layout::de);
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), true);
    QCOMPARE(xkb->layout_name(), "German");
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);

    // Switching to same layout should also work.
    reply = change_layout(Layout::de);
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), true);
    QCOMPARE(xkb->layout_name(), "German");
    QVERIFY(!spies->v1.layout_changed.wait(1000));
}

void keyboard_layout_test::test_per_layout_shortcut()
{
    // Verifies that per-layout global shortcuts are working correctly.

    // First configure layouts.
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
    layout_group.sync();

    // And create the global shortcuts.
    auto const componentName = QStringLiteral("KDE Keyboard Layout Switcher");

    auto action = new QAction(this);
    action->setObjectName(QStringLiteral("Switch keyboard layout to English (US)"));
    action->setProperty("componentName", componentName);
    KGlobalAccel::self()->setShortcut(
        action, QList<QKeySequence>{Qt::CTRL + Qt::ALT + Qt::Key_1}, KGlobalAccel::NoAutoloading);
    delete action;

    action = new QAction(this);
    action->setObjectName(QStringLiteral("Switch keyboard layout to German"));
    action->setProperty("componentName", componentName);
    KGlobalAccel::self()->setShortcut(
        action, QList<QKeySequence>{Qt::CTRL + Qt::ALT + Qt::Key_2}, KGlobalAccel::NoAutoloading);
    delete action;

    // Now we should have three layouts.
    auto xkb = input::xkb::get_primary_xkb_keyboard();
    reconfigure_layouts();
    QCOMPARE(xkb->layouts_count(), 3u);

    // Default layout is English.
    xkb->switch_to_layout(0);
    QCOMPARE(xkb->layout_name(), "English (US)");

    // Now switch to German through the global shortcut.
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(xkb->layout_name(), "German");

    Test::keyboard_key_released(KEY_2, timestamp++);

    // Switch back to English.
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(xkb->layout_name(), "English (US)");

    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
}

void keyboard_layout_test::test_dbus_service_export()
{
    // Verifies that the dbus service is only exported if there are at least two layouts.

    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 1u);

    // Default layout is English.
    QCOMPARE(xkb->layout_name(), "English (US)");

    // With one layout we should not have the dbus interface.
    QVERIFY(!QDBusConnection::sessionBus()
                 .interface()
                 ->isServiceRegistered(QStringLiteral("org.kde.keyboard"))
                 .value());

    // Reconfigure to two layouts.
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de"));
    layout_group.sync();
    reconfigure_layouts();
    QCOMPARE(xkb->layouts_count(), 2u);
    QVERIFY(QDBusConnection::sessionBus()
                .interface()
                ->isServiceRegistered(QStringLiteral("org.kde.keyboard"))
                .value());

    // And back to one layout.
    layout_group.writeEntry("LayoutList", QStringLiteral("us"));
    layout_group.sync();
    reconfigure_layouts();
    QCOMPARE(xkb->layouts_count(), 1u);
    QVERIFY(!QDBusConnection::sessionBus()
                 .interface()
                 ->isServiceRegistered(QStringLiteral("org.kde.keyboard"))
                 .value());
}

void keyboard_layout_test::test_virtual_desktop_policy()
{
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
    layout_group.writeEntry("SwitchMode", QStringLiteral("Desktop"));
    layout_group.sync();
    reconfigure_layouts();

    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 3u);
    QCOMPARE(xkb->layout_name(), "English (US)");

    VirtualDesktopManager::self()->setCount(4);
    QCOMPARE(VirtualDesktopManager::self()->count(), 4u);
    auto desktops = VirtualDesktopManager::self()->desktops();
    QCOMPARE(desktops.count(), 4);

    // Give desktops different layouts.
    uint desktop, layout;
    for (desktop = 0; desktop < VirtualDesktopManager::self()->count(); ++desktop) {
        // Switch to another virtual desktop.
        VirtualDesktopManager::self()->setCurrent(desktops.at(desktop));
        QCOMPARE(desktops.at(desktop), VirtualDesktopManager::self()->currentDesktop());

        // Should be reset to English.
        QCOMPARE(xkb->layout, 0);

        // Change first desktop to German.
        layout = (desktop + 1) % xkb->layouts_count();
        change_layout(layout).waitForFinished();
        QCOMPARE(xkb->layout, layout);
    }

    // imitate app restart to test layouts saving feature
    reset_layouts();

    // check layout set on desktop switching as intended
    for (--desktop;;) {
        QCOMPARE(desktops.at(desktop), VirtualDesktopManager::self()->currentDesktop());

        layout = (desktop + 1) % xkb->layouts_count();
        QCOMPARE(xkb->layout, layout);

        if (--desktop >= VirtualDesktopManager::self()->count()) {
            // overflow
            break;
        }
        VirtualDesktopManager::self()->setCurrent(desktops.at(desktop));
    }

    // Remove virtual desktops.
    desktop = 0;
    auto const deletedDesktop = desktops.last();
    VirtualDesktopManager::self()->setCount(1);
    QCOMPARE(xkb->layout, layout = (desktop + 1) % xkb->layouts_count());
    QCOMPARE(xkb->layout_name(), "German");

    // Add another desktop.
    VirtualDesktopManager::self()->setCount(2);

    // Switching to it should result in going to default.
    desktops = VirtualDesktopManager::self()->desktops();
    QCOMPARE(desktops.count(), 2);
    QCOMPARE(desktops.first(), VirtualDesktopManager::self()->currentDesktop());

    VirtualDesktopManager::self()->setCurrent(desktops.last());
    QCOMPARE(xkb->layout_name(), "English (US)");

    // Check there are no more layouts left in config than the last actual non-default layouts
    // number.
    QSignalSpy deletedDesktopSpy(deletedDesktop, &VirtualDesktop::aboutToBeDestroyed);
    QVERIFY(deletedDesktopSpy.isValid());
    QVERIFY(deletedDesktopSpy.wait());
    reset_layouts();
    QCOMPARE(layout_group.keyList().filter(QStringLiteral("LayoutDefault")).count(), 1);
}

void keyboard_layout_test::test_window_policy()
{
    enum Layout { us, de, de_neo, bad };
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
    layout_group.writeEntry("SwitchMode", QStringLiteral("Window"));
    layout_group.sync();
    reconfigure_layouts();

    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 3u);
    QCOMPARE(xkb->layout_name(), "English (US)");

    // Create a window.
    std::unique_ptr<Wrapland::Client::Surface> surface(Test::create_surface());
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface));
    auto c1 = Test::render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
    QVERIFY(c1);

    // Now switch layout.
    auto reply = change_layout(Layout::de);
    reply.waitForFinished();
    QCOMPARE(xkb->layout_name(), "German");

    // Create a second window.
    std::unique_ptr<Wrapland::Client::Surface> surface2(Test::create_surface());
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface2(
        Test::create_xdg_shell_toplevel(surface2));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 100), Qt::red);
    QVERIFY(c2);

    // This should have switched back to English.
    QCOMPARE(xkb->layout_name(), "English (US)");

    // Now change to another layout.
    reply = change_layout(Layout::de_neo);
    reply.waitForFinished();
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");

    // Activate other window.
    workspace()->activateClient(c1);
    QCOMPARE(xkb->layout_name(), "German");
    workspace()->activateClient(c2);
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
}

void keyboard_layout_test::test_application_policy()
{
    enum Layout { us, de, de_neo, bad };
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
    layout_group.writeEntry("SwitchMode", QStringLiteral("WinClass"));
    layout_group.sync();
    reconfigure_layouts();

    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 3u);
    QCOMPARE(xkb->layout_name(), "English (US)");

    // Create a window.
    std::unique_ptr<Wrapland::Client::Surface> surface(Test::create_surface());
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface));
    shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
    auto c1 = Test::render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
    QVERIFY(c1);

    // Create a second window.
    std::unique_ptr<Wrapland::Client::Surface> surface2(Test::create_surface());
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface2(
        Test::create_xdg_shell_toplevel(surface2));
    shellSurface2->setAppId(QByteArrayLiteral("org.kde.foo"));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 100), Qt::red);
    QVERIFY(c2);

    // Now switch layout.
    spies->v1.layout_changed.clear();
    change_layout(Layout::de_neo);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    spies->v1.layout_changed.clear();
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");

    reset_layouts();

    // Resetting layouts should trigger layout application for current client.
    workspace()->activateClient(c1);
    workspace()->activateClient(c2);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");

    // Activate other window.
    workspace()->activateClient(c1);

    // It is the same application and should not switch the layout.
    QVERIFY(!spies->v1.layout_changed.wait(1000));
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    workspace()->activateClient(c2);
    QVERIFY(!spies->v1.layout_changed.wait(1000));
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");

    shellSurface2.reset();
    surface2.reset();
    QVERIFY(Test::wait_for_destroyed(c2));
    QVERIFY(!spies->v1.layout_changed.wait(1000));
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");

    reset_layouts();
    QCOMPARE(layout_group.keyList().filter(QStringLiteral("LayoutDefault")).count(), 1);
}

void keyboard_layout_test::test_num_lock()
{
    auto xkb = input::xkb::get_primary_xkb_keyboard();
    QCOMPARE(xkb->layouts_count(), 1u);
    QCOMPARE(xkb->layout_name(), "English (US)");

    // By default not set.
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
    Test::keyboard_key_released(KEY_NUMLOCK, timestamp++);

    // Now it should be on.
    QVERIFY(flags(xkb->leds & input::keyboard_leds::num_lock));

    // And back to off.
    Test::keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
    Test::keyboard_key_released(KEY_NUMLOCK, timestamp++);
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

    // Let's reconfigure to enable through config.
    auto group = kwinApp()->inputConfig()->group("Keyboard");
    group.writeEntry("NumLock", 0);
    group.sync();

    // Without resetting the done flag should not be on.
    kwinApp()->input->xkb.reconfigure();
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

    // With the done flag unset it changes though.
    xkb->startup_num_lock_done = false;
    kwinApp()->input->xkb.reconfigure();
    QVERIFY(flags(xkb->leds & input::keyboard_leds::num_lock));

    // Pressing should result in it being off.
    Test::keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
    Test::keyboard_key_released(KEY_NUMLOCK, timestamp++);
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

    // Pressing again should enable it.
    Test::keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
    Test::keyboard_key_released(KEY_NUMLOCK, timestamp++);
    QVERIFY(flags(xkb->leds & input::keyboard_leds::num_lock));

    // Now reconfigure to disable on load.
    group.writeEntry("NumLock", 1);
    group.sync();
    kwinApp()->input->xkb.reconfigure();
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));
}

}

WAYLANDTEST_MAIN(KWin::keyboard_layout_test)
#include "keyboard_layout_test.moc"
