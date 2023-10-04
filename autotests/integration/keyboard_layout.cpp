/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/backend/wlroots/platform.h"
#include "base/wayland/server.h"
#include "input/dbus/keyboard_layouts_v2.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "input/xkb/layout_manager.h"
#include "win/activation.h"
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
#include <wlr/interfaces/wlr_keyboard.h>
}

namespace KWin::detail::test
{

namespace
{

class signal_manager : public QObject
{
    Q_OBJECT
public:
    signal_manager()
    {
        qRegisterMetaType<input::dbus::keyboard_v2>("input::dbus::keyboard_v2");
        qDBusRegisterMetaType<input::dbus::keyboard_v2>();
    }

Q_SIGNALS:
    void layout_changed(uint index);
    void layout_list_changed();

    void keyboard_v2_added(input::dbus::keyboard_v2 keyboard);
    void keyboard_v2_removed(uint keyboard);
    void layout_v2_changed(uint keyboard, uint index);
    void layout_list_v2_changed(uint keyboard);
};

struct test_spies {
    struct v1 {
        v1(signal_manager* test)
            : layout_changed{test, &signal_manager::layout_changed}
            , layouts_reconfigured{test, &signal_manager::layout_list_changed}
        {
        }
        QSignalSpy layout_changed;
        QSignalSpy layouts_reconfigured;
    } v1;
    struct v2 {
        v2(signal_manager* test)
            : keyboard_added{test, &signal_manager::keyboard_v2_added}
            , keyboard_removed{test, &signal_manager::keyboard_v2_removed}
            , layout_changed{test, &signal_manager::layout_v2_changed}
            , layouts_reconfigured{test, &signal_manager::layout_list_v2_changed}
        {
        }
        QSignalSpy keyboard_added;
        QSignalSpy keyboard_removed;
        QSignalSpy layout_changed;
        QSignalSpy layouts_reconfigured;
    } v2;

    explicit test_spies(signal_manager* test)
        : v1{test}
        , v2{test}
    {
    }
};

}

TEST_CASE("keyboard layout", "[input]")
{
    qRegisterMetaType<input::dbus::keyboard_v2>("input::dbus::keyboard_v2");
    qDBusRegisterMetaType<input::dbus::keyboard_v2>();

    signal_manager signals;

    {

        constexpr auto service_name{"org.kde.keyboard"};
        constexpr auto path_v1_name{"/Layouts"};
        constexpr auto interface_v1_name{"org.kde.KeyboardLayouts"};

        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v1_name,
                                                      interface_v1_name,
                                                      "layoutChanged",
                                                      &signals,
                                                      SIGNAL(layout_changed(uint))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v1_name,
                                                      interface_v1_name,
                                                      "layoutListChanged",
                                                      &signals,
                                                      SIGNAL(layout_list_changed())));

        constexpr auto path_v2_name{"/LayoutsV2"};
        constexpr auto interface_v2_name{"org.kde.KeyboardLayoutsV2"};

        QVERIFY(QDBusConnection::sessionBus().connect(
            service_name,
            path_v2_name,
            interface_v2_name,
            "keyboardAdded",
            &signals,
            SIGNAL(keyboard_v2_added(input::dbus::keyboard_v2))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v2_name,
                                                      interface_v2_name,
                                                      "keyboardRemoved",
                                                      &signals,
                                                      SIGNAL(keyboard_v2_removed(uint))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v2_name,
                                                      interface_v2_name,
                                                      "layoutChanged",
                                                      &signals,
                                                      SIGNAL(layout_v2_changed(uint, uint))));
        QVERIFY(QDBusConnection::sessionBus().connect(service_name,
                                                      path_v2_name,
                                                      interface_v2_name,
                                                      "layoutListChanged",
                                                      &signals,
                                                      SIGNAL(layout_list_v2_changed(uint))));
    }

    auto spies = std::make_unique<test_spies>(&signals);

    test::setup setup("keyboard-layout");
    setup.start();

    auto layout_group = setup.base->input->config.xkb->group("Layout");
    layout_group.deleteGroup();
    layout_group.sync();

    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection();

    uint32_t keyboards_index{0};

    QVERIFY(spies->v1.layout_changed.isValid());
    QVERIFY(spies->v1.layouts_reconfigured.isValid());

    QVERIFY(spies->v2.keyboard_added.isValid());
    QVERIFY(spies->v2.keyboard_removed.isValid());
    QVERIFY(spies->v2.layout_changed.isValid());
    QVERIFY(spies->v2.layouts_reconfigured.isValid());

    auto create_keyboard = [&]() {
        keyboards_index++;
        auto keyboard = static_cast<wlr_keyboard*>(calloc(1, sizeof(wlr_keyboard)));
        auto name = "headless-keyboard" + std::to_string(keyboards_index);
        wlr_keyboard_init(keyboard, nullptr, name.c_str());
        wlr_signal_emit_safe(&setup.base->backend->events.new_input, keyboard);
        return keyboard;
    };

    auto remove_input_device = [](wlr_keyboard* device) { wlr_keyboard_finish(device); };

    auto reconfigure_layouts = [&]() {
        spies->v1.layouts_reconfigured.clear();

        // Create DBus signal to reload.
        QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/Layouts"),
                                                          QStringLiteral("org.kde.keyboard"),
                                                          QStringLiteral("reloadConfig"));
        QVERIFY(QDBusConnection::sessionBus().send(message));

        QVERIFY(spies->v1.layouts_reconfigured.wait(1000));
        QCOMPARE(spies->v1.layouts_reconfigured.count(), 1);
    };

    auto call_session = [](QString const& method) {
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                  QStringLiteral("/Session"),
                                                  QStringLiteral("org.kde.KWin.Session"),
                                                  method);

        // session name
        msg << QLatin1String();
        QVERIFY(QDBusConnection::sessionBus().call(msg).type() != QDBusMessage::ErrorMessage);
    };

    auto reset_layouts = [&]() {
        // Switch Policy to destroy layouts from memory. On return to original Policy they should
        // reload from disk.
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
    };

    auto change_layout = [](uint index) {
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.keyboard"),
                                                          QStringLiteral("/Layouts"),
                                                          QStringLiteral("org.kde.KeyboardLayouts"),
                                                          QStringLiteral("setLayout"));
        msg << index;
        return QDBusConnection::sessionBus().asyncCall(msg);
    };

    SECTION("reconfigure")
    {
        // Verifies that we can change the keymap.

        // Default should be a keymap with only us layout.
        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        QCOMPARE(xkb->layouts_count(), 1u);
        QCOMPARE(xkb->layout_name(), "English (US)");
        QCOMPARE(xkb->layouts_count(), 1);
        QCOMPARE(xkb->layout_name_from_index(0), "English (US)");

        // Create a new keymap.
        auto lay_group = setup.base->input->config.xkb->group("Layout");
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

    SECTION("multiple_keyboards")
    {
        // Check creation of a second keyboard with respective D-Bus signals being emitted.

        // Currently no way to destroy a headless input device. Enable this check once we can
        // destroy the second keyboard before going into the next test function.
        layout_group = setup.base->input->config.xkb->group("Layout");
        layout_group.writeEntry("LayoutList", QStringLiteral("de,us"));
        layout_group.sync();
        reconfigure_layouts();

        auto wlr_keyboard2 = create_keyboard();
        QVERIFY(spies->v2.keyboard_added.wait());

        remove_input_device(wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_removed.wait());
    }

    SECTION("change_layout_through_dbus")
    {
        // This test verifies that the layout can be changed through DBus.

        // First configure layouts.
        enum Layout { de, us, de_neo, bad };
        layout_group.writeEntry("LayoutList", QStringLiteral("de,us,de(neo)"));
        layout_group.sync();
        reconfigure_layouts();

        // Now we should have three layouts.
        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
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

    SECTION("xkb_shortcut")
    {
        // This test verifies that per-layout global shortcuts are working correctly.

        // First configure layouts and the XKB toggle action.
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("Options", QStringLiteral("grp:ctrls_toggle"));
        layout_group.sync();

        // Now we should have three layouts.
        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        reconfigure_layouts();
        QCOMPARE(xkb->layouts_count(), 3u);

        // Create a second keyboard to test the v2 D-Bus interface.
        auto wlr_keyboard2 = create_keyboard();
        QVERIFY(setup.keyboard != wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_added.wait());
        QCOMPARE(spies->v2.keyboard_added.front().front().value<input::dbus::keyboard_v2>().id, 1);
        auto& xkb2 = setup.base->input->keyboards.at(1)->xkb;
        QCOMPARE(xkb2->layouts_count(), 3u);

        // Default layout is English.
        xkb->switch_to_layout(0);
        QCOMPARE(xkb->layout_name(), "English (US)");
        QCOMPARE(xkb2->layout_name(), "English (US)");

        // Now switch on the first keyboard to German through the XKB shortcut.
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        QCOMPARE(xkb->layout_name(), "German");
        QCOMPARE(xkb2->layout_name(), "English (US)");
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 1);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++);
        spies->v2.layout_changed.clear();

        // Switch to next layout.
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
        QCOMPARE(xkb2->layout_name(), "English (US)");
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 2);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        spies->v1.layout_changed.clear();
        spies->v2.layout_changed.clear();

        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
        QCOMPARE(xkb2->layout_name(), "English (US)");

        // Now on the second keyboard switch to German through the XKB shortcut.
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++, wlr_keyboard2);
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        // Now layout should be German on the second keyboard, but no change on the first one.
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
        QCOMPARE(xkb2->layout_name(), "German");
        QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
        QCOMPARE(spies->v2.layout_changed.front().back(), 1);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        spies->v2.layout_changed.clear();

        // Switch to next layout.
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
        QCOMPARE(xkb2->layout_name(), "German (Neo 2)");
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
        QCOMPARE(spies->v2.layout_changed.front().back(), 2);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        spies->v2.layout_changed.clear();

        // Switch to next layout on the second keyboard, which is again English.
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
        QCOMPARE(xkb2->layout_name(), "English (US)");
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
        QCOMPARE(spies->v2.layout_changed.front().back(), 0);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++, wlr_keyboard2);

        remove_input_device(wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_removed.wait());
    }

    SECTION("per_layout_shortcut")
    {
        // Verifies that per-layout global shortcuts are working correctly.

        // First configure layouts.
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.sync();

        // And create the global shortcuts.
        auto const componentName = QStringLiteral("KDE Keyboard Layout Switcher");

        auto action = new QAction;
        action->setObjectName(QStringLiteral("Switch keyboard layout to English (US)"));
        action->setProperty("componentName", componentName);
        KGlobalAccel::self()->setShortcut(action,
                                          QList<QKeySequence>{Qt::CTRL | Qt::ALT | Qt::Key_1},
                                          KGlobalAccel::NoAutoloading);
        delete action;

        action = new QAction;
        action->setObjectName(QStringLiteral("Switch keyboard layout to German"));
        action->setProperty("componentName", componentName);
        KGlobalAccel::self()->setShortcut(action,
                                          QList<QKeySequence>{Qt::CTRL | Qt::ALT | Qt::Key_2},
                                          KGlobalAccel::NoAutoloading);
        delete action;

        // Now we should have three layouts.
        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        reconfigure_layouts();
        QCOMPARE(xkb->layouts_count(), 3u);

        // Create a second keyboard to test the v2 D-Bus interface.
        auto wlr_keyboard2 = create_keyboard();
        QVERIFY(setup.keyboard != wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_added.wait());
        QCOMPARE(spies->v2.keyboard_added.front().front().value<input::dbus::keyboard_v2>().id, 1);
        auto& xkb2 = setup.base->input->keyboards.at(1)->xkb;

        // Default layout is English.
        xkb->switch_to_layout(0);
        QCOMPARE(xkb->layout_name(), "English (US)");
        QCOMPARE(xkb2->layout_name(), "English (US)");

        // Now switch to German through the global shortcut.
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        QCOMPARE(xkb->layout_name(), "German");
        QCOMPARE(xkb2->layout_name(), "English (US)");
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 1);

        keyboard_key_released(KEY_2, timestamp++);
        spies->v2.layout_changed.clear();

        // Switch back to English.
        keyboard_key_pressed(KEY_1, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QCOMPARE(xkb->layout_name(), "English (US)");
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 0);

        keyboard_key_released(KEY_1, timestamp++);
        keyboard_key_released(KEY_LEFTALT, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        spies->v1.layout_changed.clear();
        spies->v2.layout_changed.clear();

        remove_input_device(wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_removed.wait());
    }

    SECTION("dbus_service_export")
    {
        // Verifies that the dbus service is only exported if there are at least two layouts.

        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
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

    SECTION("subspace_policy")
    {
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("SwitchMode", QStringLiteral("Desktop"));
        layout_group.sync();
        reconfigure_layouts();

        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        QCOMPARE(xkb->layouts_count(), 3u);
        QCOMPARE(xkb->layout_name(), "English (US)");

        auto& vd_manager = setup.base->space->subspace_manager;
        vd_manager->setCount(4);
        QCOMPARE(vd_manager->subspaces.size(), 4u);
        auto subspaces = vd_manager->subspaces;
        QCOMPARE(subspaces.size(), 4);

        // Give subspaces different layouts.
        uint desktop, layout;
        for (desktop = 0; desktop < vd_manager->subspaces.size(); ++desktop) {
            // Switch to another virtual desktop.
            vd_manager->setCurrent(subspaces.at(desktop));
            QCOMPARE(subspaces.at(desktop), vd_manager->current);

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
            QCOMPARE(subspaces.at(desktop), vd_manager->current);

            layout = (desktop + 1) % xkb->layouts_count();
            QCOMPARE(xkb->layout, layout);

            if (--desktop >= vd_manager->subspaces.size()) {
                // overflow
                break;
            }
            vd_manager->setCurrent(subspaces.at(desktop));
        }

        // Remove subspaces.
        desktop = 0;
        auto const deletedDesktop = subspaces.back();
        vd_manager->setCount(1);
        REQUIRE(xkb->layout == (layout = (desktop + 1) % xkb->layouts_count()));
        QCOMPARE(xkb->layout_name(), "German");

        // Add another desktop.
        vd_manager->setCount(2);

        // Switching to it should result in going to default.
        subspaces = vd_manager->subspaces;
        QCOMPARE(subspaces.size(), 2);
        QCOMPARE(subspaces.front(), vd_manager->current);

        vd_manager->setCurrent(subspaces.back());
        QCOMPARE(xkb->layout_name(), "English (US)");

        // Check there are no more layouts left in config than the last actual non-default layouts
        // number.
        QSignalSpy deletedDesktopSpy(deletedDesktop, &win::subspace::aboutToBeDestroyed);
        QVERIFY(deletedDesktopSpy.isValid());
        QVERIFY(deletedDesktopSpy.wait());
        reset_layouts();
        QCOMPARE(layout_group.keyList().filter(QStringLiteral("LayoutDefault")).count(), 1);
    }

    SECTION("window_policy")
    {
        enum Layout { us, de, de_neo, bad };
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("SwitchMode", QStringLiteral("Window"));
        layout_group.sync();
        reconfigure_layouts();

        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        QCOMPARE(xkb->layouts_count(), 3u);
        QCOMPARE(xkb->layout_name(), "English (US)");

        // Create a window.
        std::unique_ptr<Wrapland::Client::Surface> surface(create_surface());
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface(
            create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        auto c1 = render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
        QVERIFY(c1);

        // Now switch layout.
        auto reply = change_layout(Layout::de);
        reply.waitForFinished();
        QCOMPARE(xkb->layout_name(), "German");

        // Create a second window.
        std::unique_ptr<Wrapland::Client::Surface> surface2(create_surface());
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface2(
            create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto c2 = render_and_wait_for_shown(surface2, QSize(100, 100), Qt::red);
        QVERIFY(c2);

        // This should have switched back to English.
        QCOMPARE(xkb->layout_name(), "English (US)");

        // Now change to another layout.
        reply = change_layout(Layout::de_neo);
        reply.waitForFinished();
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");

        // Activate other window.
        win::activate_window(*setup.base->space, *c1);
        QCOMPARE(xkb->layout_name(), "German");
        win::activate_window(*setup.base->space, *c2);
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
    }

    SECTION("application_policy")
    {
        enum Layout { us, de, de_neo, bad };
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("SwitchMode", QStringLiteral("WinClass"));
        layout_group.sync();
        reconfigure_layouts();

        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        QCOMPARE(xkb->layouts_count(), 3u);
        QCOMPARE(xkb->layout_name(), "English (US)");

        // Create a window.
        std::unique_ptr<Wrapland::Client::Surface> surface(create_surface());
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface(
            create_xdg_shell_toplevel(surface));
        shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
        auto c1 = render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
        QVERIFY(c1);

        // Create a second window.
        std::unique_ptr<Wrapland::Client::Surface> surface2(create_surface());
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> shellSurface2(
            create_xdg_shell_toplevel(surface2));
        shellSurface2->setAppId(QByteArrayLiteral("org.kde.foo"));
        auto c2 = render_and_wait_for_shown(surface2, QSize(100, 100), Qt::red);
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
        win::activate_window(*setup.base->space, *c1);
        win::activate_window(*setup.base->space, *c2);
        QVERIFY(spies->v1.layout_changed.wait());
        QCOMPARE(spies->v1.layout_changed.count(), 1);
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");

        // Activate other window.
        win::activate_window(*setup.base->space, *c1);

        // It is the same application and should not switch the layout.
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");
        win::activate_window(*setup.base->space, *c2);
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");

        shellSurface2.reset();
        surface2.reset();
        QVERIFY(wait_for_destroyed(c2));
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(xkb->layout_name(), "German (Neo 2)");

        reset_layouts();
        QCOMPARE(layout_group.keyList().filter(QStringLiteral("LayoutDefault")).count(), 1);
    }

    SECTION("num_lock")
    {
        auto xkb = input::xkb::get_primary_xkb_keyboard(*setup.base->input);
        QCOMPARE(xkb->layouts_count(), 1u);
        QCOMPARE(xkb->layout_name(), "English (US)");

        // By default not set.
        QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);

        // Now it should be on.
        QVERIFY(flags(xkb->leds & input::keyboard_leds::num_lock));

        // And back to off.
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);
        QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

        // Let's reconfigure to enable through config.
        auto group = setup.base->input->config.main->group("Keyboard");
        group.writeEntry("NumLock", 0);
        group.sync();

        // Without resetting the done flag should not be on.
        setup.base->input->xkb.reconfigure();
        QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

        // With the done flag unset it changes though.
        xkb->startup_num_lock_done = false;
        setup.base->input->xkb.reconfigure();
        QVERIFY(flags(xkb->leds & input::keyboard_leds::num_lock));

        // Pressing should result in it being off.
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);
        QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));

        // Pressing again should enable it.
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);
        QVERIFY(flags(xkb->leds & input::keyboard_leds::num_lock));

        // Now reconfigure to disable on load.
        group.writeEntry("NumLock", 1);
        group.sync();
        setup.base->input->xkb.reconfigure();
        QVERIFY(!(xkb->leds & input::keyboard_leds::num_lock));
    }
}

}

#include "keyboard_layout.moc"
