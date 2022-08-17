/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/backend/wlroots/platform.h"
#include "base/wayland/server.h"
#include "input/dbus/keyboard_layouts_v2.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "input/xkb/layout_manager.h"
#include "win/activation.h"
#include "win/space.h"
#include "win/virtual_desktops.h"
#include "win/wayland/window.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <QAction>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <Wrapland/Client/surface.h>

#include <linux/input.h>

extern "C" {
#if HAVE_WLR_BASE_INPUT_DEVICES
#include <wlr/interfaces/wlr_keyboard.h>
#else
#include <wlr/interfaces/wlr_input_device.h>
#endif
}

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
        struct v2 {
            v2(keyboard_layout_test* test)
                : keyboard_added{test, &keyboard_layout_test::keyboard_v2_added}
                , keyboard_removed{test, &keyboard_layout_test::keyboard_v2_removed}
                , layout_changed{test, &keyboard_layout_test::layout_v2_changed}
                , layouts_reconfigured{test, &keyboard_layout_test::layout_list_v2_changed}
            {
            }
            QSignalSpy keyboard_added;
            QSignalSpy keyboard_removed;
            QSignalSpy layout_changed;
            QSignalSpy layouts_reconfigured;
        } v2;

        explicit test_spies(keyboard_layout_test* test)
            : v1{test}
            , v2{test}
        {
        }
    };

    keyboard_layout_test()
    {
        qRegisterMetaType<input::dbus::keyboard_v2>("input::dbus::keyboard_v2");
        qDBusRegisterMetaType<input::dbus::keyboard_v2>();

        spies = std::make_unique<test_spies>(this);
    }

Q_SIGNALS:
    void layout_changed(uint index);
    void layout_list_changed();

    void keyboard_v2_added(input::dbus::keyboard_v2 keyboard);
    void keyboard_v2_removed(uint keyboard);
    void layout_v2_changed(uint keyboard, uint index);
    void layout_list_v2_changed(uint keyboard);

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_reconfigure();
    void test_multiple_keyboards();
    void test_change_layout_through_dbus();
    void test_xkb_shortcut();
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
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_keyboard* create_keyboard();
#else
    wlr_input_device* create_keyboard();
#endif

    KConfigGroup layout_group;
    std::unique_ptr<test_spies> spies;

    uint32_t keyboards_index{0};
};

#if HAVE_WLR_BASE_INPUT_DEVICES
wlr_keyboard* keyboard_layout_test::create_keyboard()
{
    keyboards_index++;
    auto keyboard = static_cast<wlr_keyboard*>(calloc(1, sizeof(wlr_keyboard)));
    auto name = "headless-keyboard" + std::to_string(keyboards_index);
    wlr_keyboard_init(keyboard, nullptr, name.c_str());
    Test::wlr_signal_emit_safe(&Test::app()->base.backend->events.new_input, keyboard);
    return keyboard;
}

void remove_input_device(wlr_keyboard* device)
{
    wlr_keyboard_finish(device);
}
#else
wlr_input_device* keyboard_layout_test::create_keyboard()
{
    keyboards_index++;
    return wlr_headless_add_input_device(Test::app()->base.backend, WLR_INPUT_DEVICE_KEYBOARD);
}

void remove_input_device(wlr_input_device* device)
{
    wlr_input_device_destroy(device);
}
#endif

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

    QVERIFY(spies->v2.keyboard_added.isValid());
    QVERIFY(spies->v2.keyboard_removed.isValid());
    QVERIFY(spies->v2.layout_changed.isValid());
    QVERIFY(spies->v2.layouts_reconfigured.isValid());

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
    {
        constexpr auto path_v2_name{"/LayoutsV2"};
        constexpr auto interface_v2_name{"org.kde.KeyboardLayoutsV2"};

        QVERIFY(QDBusConnection::sessionBus().connect(
            service_name,
            path_v2_name,
            interface_v2_name,
            "keyboardAdded",
            this,
            SIGNAL(keyboard_v2_added(input::dbus::keyboard_v2))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v2_name,
                                                      interface_v2_name,
                                                      "keyboardRemoved",
                                                      this,
                                                      SIGNAL(keyboard_v2_removed(uint))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v2_name,
                                                      interface_v2_name,
                                                      "layoutChanged",
                                                      this,
                                                      SIGNAL(layout_v2_changed(uint, uint))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v2_name,
                                                      interface_v2_name,
                                                      "layoutListChanged",
                                                      this,
                                                      SIGNAL(layout_list_v2_changed(uint))));
    }

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

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

    spies->v1.layout_changed.clear();
    spies->v2.layout_changed.clear();

    // We always reset to a us layout.
    if (auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
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
    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
    QCOMPARE(xkb->layouts_count(), 1u);
    QCOMPARE(xkb->layout_name(), "English (US)");
    QCOMPARE(xkb->layouts_count(), 1);
    QCOMPARE(xkb->layout_name_from_index(0), "English (US)");

    // Create a new keymap.
    auto lay_group = kwinApp()->kxkbConfig()->group("Layout");
    lay_group.writeEntry("LayoutList", QStringLiteral("de,us"));
    lay_group.sync();

    reconfigure_layouts();

    // Now we should have two layouts.
    QCOMPARE(xkb->layouts_count(), 2u);

    // Default layout is German.
    QCOMPARE(xkb->layout_name(), "German");
    QCOMPARE(xkb->layouts_count(), 2);
    QCOMPARE(xkb->layout_name_from_index(0), "German");
    QCOMPARE(xkb->layout_name_from_index(1), "English (US)");
}

void keyboard_layout_test::test_multiple_keyboards()
{
    // Check creation of a second keyboard with respective D-Bus signals being emitted.

    // Currently no way to destroy a headless input device. Enable this check once we can destroy
    // the second keyboard before going into the next test function.
    layout_group = kwinApp()->kxkbConfig()->group("Layout");
    layout_group.writeEntry("LayoutList", QStringLiteral("de,us"));
    layout_group.sync();
    reconfigure_layouts();

    auto wlr_keyboard2 = create_keyboard();
    QVERIFY(spies->v2.keyboard_added.wait());

    remove_input_device(wlr_keyboard2);
    QVERIFY(spies->v2.keyboard_removed.wait());
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
    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
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
    QTRY_COMPARE(spies->v2.layout_changed.count(), 1);
    spies->v1.layout_changed.clear();
    spies->v2.layout_changed.clear();

    // Layout should persist after reset.
    reset_layouts();
    QCOMPARE(xkb->layout_name(), "English (US)");
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    QTRY_COMPARE(spies->v2.layout_changed.count(), 1);
    spies->v1.layout_changed.clear();
    spies->v2.layout_changed.clear();

    // Switch to a layout which does not exist.
    reply = change_layout(Layout::bad);
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), false);
    QCOMPARE(xkb->layout_name(), "English (US)");
    QVERIFY(!spies->v1.layout_changed.wait(1000));
    QCOMPARE(spies->v2.layout_changed.count(), 0);

    // Switch to another layout should work.
    reply = change_layout(Layout::de);
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), true);
    QCOMPARE(xkb->layout_name(), "German");
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    QTRY_COMPARE(spies->v2.layout_changed.count(), 1);
    spies->v1.layout_changed.clear();
    spies->v2.layout_changed.clear();

    // Switching to same layout should also work.
    reply = change_layout(Layout::de);
    QVERIFY(!reply.isError());
    QCOMPARE(reply.reply().arguments().first().toBool(), true);
    QCOMPARE(xkb->layout_name(), "German");
    QVERIFY(!spies->v1.layout_changed.wait(1000));
    QCOMPARE(spies->v2.layout_changed.count(), 0);
}

void keyboard_layout_test::test_xkb_shortcut()
{
    // This test verifies that per-layout global shortcuts are working correctly.

    // First configure layouts and the XKB toggle action.
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
    layout_group.writeEntry("Options", QStringLiteral("grp:ctrls_toggle"));
    layout_group.sync();

    // Now we should have three layouts.
    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
    reconfigure_layouts();
    QCOMPARE(xkb->layouts_count(), 3u);

    // Create a second keyboard to test the v2 D-Bus interface.
    auto wlr_keyboard2 = create_keyboard();
    QVERIFY(Test::app()->keyboard != wlr_keyboard2);
    QVERIFY(spies->v2.keyboard_added.wait());
    QCOMPARE(spies->v2.keyboard_added.front().front().value<input::dbus::keyboard_v2>().id, 1);
    auto& xkb2 = Test::app()->base.input->keyboards.at(1)->xkb;
    QCOMPARE(xkb2->layouts_count(), 3u);

    // Default layout is English.
    xkb->switch_to_layout(0);
    QCOMPARE(xkb->layout_name(), "English (US)");
    QCOMPARE(xkb2->layout_name(), "English (US)");

    // Now switch on the first keyboard to German through the XKB shortcut.
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++);
    QVERIFY(spies->v1.layout_changed.wait());
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

    QCOMPARE(xkb->layout_name(), "German");
    QCOMPARE(xkb2->layout_name(), "English (US)");
    QCOMPARE(spies->v2.layout_changed.front().front(), 0);
    QCOMPARE(spies->v2.layout_changed.front().back(), 1);

    Test::keyboard_key_released(KEY_RIGHTCTRL, timestamp++);
    spies->v2.layout_changed.clear();

    // Switch to next layout.
    Test::keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++);
    QVERIFY(spies->v1.layout_changed.wait());
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    QCOMPARE(xkb2->layout_name(), "English (US)");
    QCOMPARE(spies->v2.layout_changed.front().front(), 0);
    QCOMPARE(spies->v2.layout_changed.front().back(), 2);

    Test::keyboard_key_released(KEY_RIGHTCTRL, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    spies->v1.layout_changed.clear();
    spies->v2.layout_changed.clear();

    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    QCOMPARE(xkb2->layout_name(), "English (US)");

    // Now on the second keyboard switch to German through the XKB shortcut.
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++, wlr_keyboard2);
    Test::keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
    QVERIFY(!spies->v1.layout_changed.wait(500));
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

    // Now layout should be German on the second keyboard, but no change on the first one.
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    QCOMPARE(xkb2->layout_name(), "German");
    QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
    QCOMPARE(spies->v2.layout_changed.front().back(), 1);

    Test::keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
    spies->v2.layout_changed.clear();

    // Switch to next layout.
    Test::keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
    QVERIFY(!spies->v1.layout_changed.wait(500));
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    QCOMPARE(xkb2->layout_name(), "German (Neo 2)");
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
    QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
    QCOMPARE(spies->v2.layout_changed.front().back(), 2);

    Test::keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
    spies->v2.layout_changed.clear();

    // Switch to next layout on the second keyboard, which is again English.
    Test::keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
    QVERIFY(!spies->v1.layout_changed.wait(500));
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    QCOMPARE(xkb2->layout_name(), "English (US)");
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
    QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
    QCOMPARE(spies->v2.layout_changed.front().back(), 0);

    Test::keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++, wlr_keyboard2);

    remove_input_device(wlr_keyboard2);
    QVERIFY(spies->v2.keyboard_removed.wait());
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
    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
    reconfigure_layouts();
    QCOMPARE(xkb->layouts_count(), 3u);

    // Create a second keyboard to test the v2 D-Bus interface.
    auto wlr_keyboard2 = create_keyboard();
    QVERIFY(Test::app()->keyboard != wlr_keyboard2);
    QVERIFY(spies->v2.keyboard_added.wait());
    QCOMPARE(spies->v2.keyboard_added.front().front().value<input::dbus::keyboard_v2>().id, 1);
    auto& xkb2 = Test::app()->base.input->keyboards.at(1)->xkb;

    // Default layout is English.
    xkb->switch_to_layout(0);
    QCOMPARE(xkb->layout_name(), "English (US)");
    QCOMPARE(xkb2->layout_name(), "English (US)");

    // Now switch to German through the global shortcut.
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_pressed(KEY_2, timestamp++);
    QVERIFY(spies->v1.layout_changed.wait());
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

    QCOMPARE(xkb->layout_name(), "German");
    QCOMPARE(xkb2->layout_name(), "English (US)");
    QCOMPARE(spies->v2.layout_changed.front().front(), 0);
    QCOMPARE(spies->v2.layout_changed.front().back(), 1);

    Test::keyboard_key_released(KEY_2, timestamp++);
    spies->v2.layout_changed.clear();

    // Switch back to English.
    Test::keyboard_key_pressed(KEY_1, timestamp++);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(xkb->layout_name(), "English (US)");
    QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
    QCOMPARE(spies->v2.layout_changed.front().front(), 0);
    QCOMPARE(spies->v2.layout_changed.front().back(), 0);

    Test::keyboard_key_released(KEY_1, timestamp++);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    spies->v1.layout_changed.clear();
    spies->v2.layout_changed.clear();

    remove_input_device(wlr_keyboard2);
    QVERIFY(spies->v2.keyboard_removed.wait());
}

void keyboard_layout_test::test_dbus_service_export()
{
    // Verifies that the dbus service is only exported if there are at least two layouts.

    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
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

    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
    QCOMPARE(xkb->layouts_count(), 3u);
    QCOMPARE(xkb->layout_name(), "English (US)");

    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
    vd_manager->setCount(4);
    QCOMPARE(vd_manager->count(), 4u);
    auto desktops = vd_manager->desktops();
    QCOMPARE(desktops.count(), 4);

    // Give desktops different layouts.
    uint desktop, layout;
    for (desktop = 0; desktop < vd_manager->count(); ++desktop) {
        // Switch to another virtual desktop.
        vd_manager->setCurrent(desktops.at(desktop));
        QCOMPARE(desktops.at(desktop), vd_manager->currentDesktop());

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
        QCOMPARE(desktops.at(desktop), vd_manager->currentDesktop());

        layout = (desktop + 1) % xkb->layouts_count();
        QCOMPARE(xkb->layout, layout);

        if (--desktop >= vd_manager->count()) {
            // overflow
            break;
        }
        vd_manager->setCurrent(desktops.at(desktop));
    }

    // Remove virtual desktops.
    desktop = 0;
    auto const deletedDesktop = desktops.last();
    vd_manager->setCount(1);
    QCOMPARE(xkb->layout, layout = (desktop + 1) % xkb->layouts_count());
    QCOMPARE(xkb->layout_name(), "German");

    // Add another desktop.
    vd_manager->setCount(2);

    // Switching to it should result in going to default.
    desktops = vd_manager->desktops();
    QCOMPARE(desktops.count(), 2);
    QCOMPARE(desktops.first(), vd_manager->currentDesktop());

    vd_manager->setCurrent(desktops.last());
    QCOMPARE(xkb->layout_name(), "English (US)");

    // Check there are no more layouts left in config than the last actual non-default layouts
    // number.
    QSignalSpy deletedDesktopSpy(deletedDesktop, &win::virtual_desktop::aboutToBeDestroyed);
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

    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
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
    win::activate_window(*Test::app()->base.space, c1);
    QCOMPARE(xkb->layout_name(), "German");
    win::activate_window(*Test::app()->base.space, c2);
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
}

void keyboard_layout_test::test_application_policy()
{
    enum Layout { us, de, de_neo, bad };
    layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
    layout_group.writeEntry("SwitchMode", QStringLiteral("WinClass"));
    layout_group.sync();
    reconfigure_layouts();

    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
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
    win::activate_window(*Test::app()->base.space, c1);
    win::activate_window(*Test::app()->base.space, c2);
    QVERIFY(spies->v1.layout_changed.wait());
    QCOMPARE(spies->v1.layout_changed.count(), 1);
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");

    // Activate other window.
    win::activate_window(*Test::app()->base.space, c1);

    // It is the same application and should not switch the layout.
    QVERIFY(!spies->v1.layout_changed.wait(1000));
    QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    win::activate_window(*Test::app()->base.space, c2);
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
    auto xkb = input::xkb::get_primary_xkb_keyboard(*Test::app()->base.input);
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
    Test::app()->base.input->xkb.reconfigure();
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

    // With the done flag unset it changes though.
    xkb->startup_num_lock_done = false;
    Test::app()->base.input->xkb.reconfigure();
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
    Test::app()->base.input->xkb.reconfigure();
    QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));
}

}

WAYLANDTEST_MAIN(KWin::keyboard_layout_test)
#include "keyboard_layout_test.moc"
