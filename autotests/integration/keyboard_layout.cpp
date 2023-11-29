/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

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

        constexpr auto service_name{"org.kde.keyboard"};
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

Q_SIGNALS:
    void layout_changed(uint index);
    void layout_list_changed();

    void keyboard_v2_added(input::dbus::keyboard_v2 keyboard);
    void keyboard_v2_removed(uint keyboard);
    void layout_v2_changed(uint keyboard, uint index);
    void layout_list_v2_changed(uint keyboard);
};

struct test_spies {
    test_spies()
        : signals{std::make_unique<signal_manager>()}
        , v1{signals.get()}
        , v2{signals.get()}
    {
    }

    std::unique_ptr<signal_manager> signals;

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
};

struct client_holder {
    client_holder()
    {
    }
    void create()
    {
        surface = create_surface();
        REQUIRE(surface);
        toplevel = create_xdg_shell_toplevel(surface);
        REQUIRE(toplevel);
    }
    void render(Qt::GlobalColor color)
    {
        REQUIRE(!window);
        window = render_and_wait_for_shown(surface, QSize(100, 100), color);
        REQUIRE(window);
    }

    std::unique_ptr<Wrapland::Client::Surface> surface;
    std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
    wayland_window* window{nullptr};
};

}

TEST_CASE("keyboard layout", "[input]")
{
    qRegisterMetaType<input::dbus::keyboard_v2>("input::dbus::keyboard_v2");
    qDBusRegisterMetaType<input::dbus::keyboard_v2>();

    auto start_setup = [](auto& setup) {
        setup->start();
        setup->set_outputs(2);
        test_outputs_default();
        setup_wayland_connection();
        return std::make_unique<test_spies>();
    };

    auto setup = std::make_unique<test::setup>("keyboard-layout");
    auto spies = start_setup(setup);

    auto get_xkb_keys = [&](int index = -1) {
        if (index < 0) {
            return input::xkb::get_primary_xkb_keyboard(*setup->base->mod.input);
        }
        return setup->base->mod.input->keyboards.at(index)->xkb.get();
    };

    auto reset_setup = [&]() {
        setup = {};

        auto cfg = KSharedConfig::openConfig("kxkbrc");
        auto const old_layout_group = cfg->group("Layout");

        setup = std::make_unique<test::setup>("keyboard-layout");

        auto layout_group = cfg->group("Layout");
        old_layout_group.copyTo(&layout_group);
        cfg->sync();

        spies = start_setup(setup);
    };

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
        wlr_signal_emit_safe(&setup->base->backend.native->events.new_input, keyboard);
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
        QCOMPARE(get_xkb_keys()->layouts_count(), 1u);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");
        QCOMPARE(get_xkb_keys()->layouts_count(), 1);
        QCOMPARE(get_xkb_keys()->layout_name_from_index(0), "English (US)");

        // Create a new keymap.
        auto lay_group = setup->base->mod.input->config.xkb->group("Layout");
        lay_group.writeEntry("LayoutList", QStringLiteral("de,us"));
        lay_group.sync();

        reconfigure_layouts();

        // Now we should have two layouts.
        QCOMPARE(get_xkb_keys()->layouts_count(), 2u);

        // Default layout is German.
        QCOMPARE(get_xkb_keys()->layout_name(), "German");
        QCOMPARE(get_xkb_keys()->layouts_count(), 2);
        QCOMPARE(get_xkb_keys()->layout_name_from_index(0), "German");
        QCOMPARE(get_xkb_keys()->layout_name_from_index(1), "English (US)");
    }

    SECTION("multiple_keyboards")
    {
        // Check creation of a second keyboard with respective D-Bus signals being emitted.

        // Currently no way to destroy a headless input device. Enable this check once we can
        // destroy the second keyboard before going into the next test function.
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
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
        KConfigGroup layout_group;

        auto reset_layouts = [&] {
            layout_group = setup->base->mod.input->config.xkb->group("Layout");
            layout_group.writeEntry("LayoutList", QStringLiteral("de,us,de(neo)"));
            layout_group.sync();
            reconfigure_layouts();
        };
        reset_layouts();

        // Now we should have three layouts.
        QCOMPARE(get_xkb_keys()->layouts_count(), 3u);

        // Default layout is German.
        get_xkb_keys()->switch_to_layout(0);
        QCOMPARE(get_xkb_keys()->layout_name(), "German");

        // Place garbage to layout entry.
        layout_group.writeEntry("LayoutDefaultFoo", "garbage");

        // Make sure the garbage is wiped out on saving.
        reset_setup();

        QVERIFY(!layout_group.hasKey("LayoutDefaultFoo"));

        // Now change through DBus to English.
        QCOMPARE(get_xkb_keys()->layout_name(), "German");

        auto reply = change_layout(Layout::us);
        reply.waitForFinished();
        QVERIFY(!reply.isError());
        QCOMPARE(reply.reply().arguments().first().toBool(), true);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");
        QVERIFY(spies->v1.layout_changed.wait());
        QCOMPARE(spies->v1.layout_changed.count(), 1);
        QTRY_COMPARE(spies->v2.layout_changed.count(), 1);
        spies->v1.layout_changed.clear();
        spies->v2.layout_changed.clear();

        // Layout should persist after restart.
        reset_setup();

        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        // There is no layout changed signal at start up.
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QCOMPARE(spies->v1.layout_changed.count(), 0);
        QTRY_COMPARE(spies->v2.layout_changed.count(), 0);

        // Switch to a layout which does not exist.
        reply = change_layout(Layout::bad);
        QVERIFY(!reply.isError());
        QCOMPARE(reply.reply().arguments().first().toBool(), false);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(spies->v2.layout_changed.count(), 0);

        // Switch to another layout should work.
        reply = change_layout(Layout::de);
        QVERIFY(!reply.isError());
        QCOMPARE(reply.reply().arguments().first().toBool(), true);
        QCOMPARE(get_xkb_keys()->layout_name(), "German");
        QVERIFY(spies->v1.layout_changed.wait());
        QCOMPARE(spies->v1.layout_changed.count(), 1);
        QTRY_COMPARE(spies->v2.layout_changed.count(), 1);
        spies->v1.layout_changed.clear();
        spies->v2.layout_changed.clear();

        // Switching to same layout should also work.
        reply = change_layout(Layout::de);
        QVERIFY(!reply.isError());
        QCOMPARE(reply.reply().arguments().first().toBool(), true);
        QCOMPARE(get_xkb_keys()->layout_name(), "German");
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(spies->v2.layout_changed.count(), 0);
    }

    SECTION("xkb_shortcut")
    {
        // This test verifies that per-layout global shortcuts are working correctly.

        // First configure layouts and the XKB toggle action.
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("Options", QStringLiteral("grp:ctrls_toggle"));
        layout_group.sync();

        // Now we should have three layouts.
        reconfigure_layouts();
        QCOMPARE(get_xkb_keys()->layouts_count(), 3u);

        // Create a second keyboard to test the v2 D-Bus interface.
        auto wlr_keyboard2 = create_keyboard();
        QVERIFY(setup->keyboard != wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_added.wait());
        QCOMPARE(spies->v2.keyboard_added.front().front().value<input::dbus::keyboard_v2>().id, 1);
        QCOMPARE(get_xkb_keys(1)->layouts_count(), 3u);

        // Default layout is English.
        get_xkb_keys()->switch_to_layout(0);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");

        // Now switch on the first keyboard to German through the XKB shortcut.
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        QCOMPARE(get_xkb_keys()->layout_name(), "German");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 1);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++);
        spies->v2.layout_changed.clear();

        // Switch to next layout.
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 2);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++);
        keyboard_key_released(KEY_LEFTCTRL, timestamp++);
        spies->v1.layout_changed.clear();
        spies->v2.layout_changed.clear();

        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");

        // Now on the second keyboard switch to German through the XKB shortcut.
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++, wlr_keyboard2);
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        // Now layout should be German on the second keyboard, but no change on the first one.
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "German");
        QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
        QCOMPARE(spies->v2.layout_changed.front().back(), 1);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        spies->v2.layout_changed.clear();

        // Switch to next layout.
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "German (Neo 2)");
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(spies->v2.layout_changed.front().front(), keyboards_index);
        QCOMPARE(spies->v2.layout_changed.front().back(), 2);

        keyboard_key_released(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        spies->v2.layout_changed.clear();

        // Switch to next layout on the second keyboard, which is again English.
        keyboard_key_pressed(KEY_RIGHTCTRL, timestamp++, wlr_keyboard2);
        QVERIFY(!spies->v1.layout_changed.wait(500));
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");
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
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
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
        reconfigure_layouts();
        QCOMPARE(get_xkb_keys()->layouts_count(), 3u);

        // Create a second keyboard to test the v2 D-Bus interface.
        auto wlr_keyboard2 = create_keyboard();
        QVERIFY(setup->keyboard != wlr_keyboard2);
        QVERIFY(spies->v2.keyboard_added.wait());
        QCOMPARE(spies->v2.keyboard_added.front().front().value<input::dbus::keyboard_v2>().id, 1);

        // Default layout is English.
        get_xkb_keys()->switch_to_layout(0);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");

        // Now switch to German through the global shortcut.
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
        keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        keyboard_key_pressed(KEY_2, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QTRY_COMPARE(spies->v2.layout_changed.size(), 1);

        QCOMPARE(get_xkb_keys()->layout_name(), "German");
        QCOMPARE(get_xkb_keys(1)->layout_name(), "English (US)");
        QCOMPARE(spies->v2.layout_changed.front().front(), 0);
        QCOMPARE(spies->v2.layout_changed.front().back(), 1);

        keyboard_key_released(KEY_2, timestamp++);
        spies->v2.layout_changed.clear();

        // Switch back to English.
        keyboard_key_pressed(KEY_1, timestamp++);
        QVERIFY(spies->v1.layout_changed.wait());
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");
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

        QCOMPARE(get_xkb_keys()->layouts_count(), 1u);

        // Default layout is English.
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        // With one layout we should not have the dbus interface.
        QVERIFY(!QDBusConnection::sessionBus()
                     .interface()
                     ->isServiceRegistered(QStringLiteral("org.kde.keyboard"))
                     .value());

        // Reconfigure to two layouts.
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de"));
        layout_group.sync();
        reconfigure_layouts();
        QCOMPARE(get_xkb_keys()->layouts_count(), 2u);
        QVERIFY(QDBusConnection::sessionBus()
                    .interface()
                    ->isServiceRegistered(QStringLiteral("org.kde.keyboard"))
                    .value());

        // And back to one layout.
        layout_group.writeEntry("LayoutList", QStringLiteral("us"));
        layout_group.sync();
        reconfigure_layouts();
        QCOMPARE(get_xkb_keys()->layouts_count(), 1u);
        QVERIFY(!QDBusConnection::sessionBus()
                     .interface()
                     ->isServiceRegistered(QStringLiteral("org.kde.keyboard"))
                     .value());
    }

    SECTION("subspace_policy")
    {
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("SwitchMode", QStringLiteral("Desktop"));
        layout_group.sync();
        reconfigure_layouts();

        QCOMPARE(get_xkb_keys()->layouts_count(), 3u);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        auto get_subsp_mgr = [&] { return setup->base->mod.space->subspace_manager.get(); };

        win::subspace_manager_set_count(*get_subsp_mgr(), 4);
        QCOMPARE(get_subsp_mgr()->subspaces.size(), 4u);
        auto subspaces = get_subsp_mgr()->subspaces;
        QCOMPARE(subspaces.size(), 4);

        // Give subspaces different layouts.
        uint desktop, layout;
        for (desktop = 0; desktop < get_subsp_mgr()->subspaces.size(); ++desktop) {
            // Switch to another virtual desktop.
            win::subspaces_set_current(*get_subsp_mgr(), *subspaces.at(desktop));
            QCOMPARE(subspaces.at(desktop), get_subsp_mgr()->current);

            // Should be reset to English.
            QCOMPARE(get_xkb_keys()->layout, 0);

            // Change first desktop to German.
            layout = (desktop + 1) % get_xkb_keys()->layouts_count();
            change_layout(layout).waitForFinished();
            QCOMPARE(get_xkb_keys()->layout, layout);
        }

        // imitate app restart to test layouts saving feature
        reset_setup();
        win::subspace_manager_set_count(*get_subsp_mgr(), 4);
        QCOMPARE(get_subsp_mgr()->subspaces.size(), 4u);

        subspaces = get_subsp_mgr()->subspaces;
        win::subspaces_set_current(*get_subsp_mgr(),
                                   *subspaces.at(get_subsp_mgr()->subspaces.size() - 1));

        // check layout set on desktop switching as intended
        for (--desktop;;) {
            QCOMPARE(subspaces.at(desktop), get_subsp_mgr()->current);

            layout = (desktop + 1) % get_xkb_keys()->layouts_count();
            QCOMPARE(get_xkb_keys()->layout, layout);

            if (--desktop >= get_subsp_mgr()->subspaces.size()) {
                // overflow
                break;
            }
            win::subspaces_set_current(*get_subsp_mgr(), *subspaces.at(desktop));
        }

        // Remove subspaces.
        desktop = 0;
        auto const deletedDesktop = subspaces.back();
        win::subspace_manager_set_count(*get_subsp_mgr(), 1);
        REQUIRE(get_xkb_keys()->layout
                == (layout = (desktop + 1) % get_xkb_keys()->layouts_count()));
        QCOMPARE(get_xkb_keys()->layout_name(), "German");

        // Add another desktop.
        win::subspace_manager_set_count(*get_subsp_mgr(), 2);

        // Switching to it should result in going to default.
        subspaces = get_subsp_mgr()->subspaces;
        QCOMPARE(subspaces.size(), 2);
        QCOMPARE(subspaces.front(), get_subsp_mgr()->current);

        win::subspaces_set_current(*get_subsp_mgr(), *subspaces.back());
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        // Check there are no more layouts left in config than the last actual non-default layouts
        // number.
        QSignalSpy deletedDesktopSpy(deletedDesktop, &win::subspace::aboutToBeDestroyed);
        QVERIFY(deletedDesktopSpy.isValid());
        QVERIFY(deletedDesktopSpy.wait());
        reset_setup();

        layout_group = setup->base->mod.input->config.xkb->group("Layout");
        QCOMPARE(layout_group.keyList().filter(QStringLiteral("LayoutDefault")).count(), 1);
    }

    SECTION("window_policy")
    {
        enum Layout { us, de, de_neo, bad };
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("SwitchMode", QStringLiteral("Window"));
        layout_group.sync();
        reconfigure_layouts();

        QCOMPARE(get_xkb_keys()->layouts_count(), 3u);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        // Create a window.
        auto client1 = client_holder();
        client1.create();
        client1.render(Qt::blue);

        // Now switch layout.
        auto reply = change_layout(Layout::de);
        reply.waitForFinished();
        QCOMPARE(get_xkb_keys()->layout_name(), "German");

        // Create a second window.
        auto client2 = client_holder();
        client2.create();
        client2.render(Qt::red);

        // This should have switched back to English.
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        // Now change to another layout.
        reply = change_layout(Layout::de_neo);
        reply.waitForFinished();
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");

        // Activate other window.
        win::activate_window(*setup->base->mod.space, *client1.window);
        QCOMPARE(get_xkb_keys()->layout_name(), "German");
        win::activate_window(*setup->base->mod.space, *client2.window);
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
    }

    SECTION("application_policy")
    {
        enum Layout { us, de, de_neo, bad };
        auto layout_group = setup->base->mod.input->config.xkb->group("Layout");
        layout_group.writeEntry("LayoutList", QStringLiteral("us,de,de(neo)"));
        layout_group.writeEntry("SwitchMode", QStringLiteral("WinClass"));
        layout_group.sync();
        reconfigure_layouts();

        QCOMPARE(get_xkb_keys()->layouts_count(), 3u);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        auto create_render_client = [](QByteArray const& appid, Qt::GlobalColor color) {
            auto client = client_holder();
            client.create();
            client.toplevel->setAppId(appid);
            client.render(color);
            return client;
        };

        // Create two windows.
        auto client1 = create_render_client("org.kde.foo", Qt::blue);
        auto client2 = create_render_client("org.kde.foo", Qt::red);

        // Now switch layout.
        spies->v1.layout_changed.clear();
        change_layout(Layout::de_neo);
        QVERIFY(spies->v1.layout_changed.wait());
        QCOMPARE(spies->v1.layout_changed.count(), 1);
        spies->v1.layout_changed.clear();
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");

        client1 = {};
        client2 = {};

        reset_setup();
        client1 = create_render_client("org.kde.foo", Qt::blue);
        client2 = create_render_client("org.kde.foo", Qt::red);

        // Resetting layouts should trigger layout application for current client.
        win::activate_window(*setup->base->mod.space, *client1.window);
        win::activate_window(*setup->base->mod.space, *client2.window);
        TRY_REQUIRE(spies->v1.layout_changed.size() == 1);
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");

        // Activate other window.
        win::activate_window(*setup->base->mod.space, *client1.window);

        // It is the same application and should not switch the layout.
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");
        win::activate_window(*setup->base->mod.space, *client2.window);
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");

        client2.toplevel.reset();
        client2.surface.reset();
        QVERIFY(wait_for_destroyed(client2.window));
        QVERIFY(!spies->v1.layout_changed.wait(1000));
        QCOMPARE(get_xkb_keys()->layout_name(), "German (Neo 2)");

        client1 = {};
        client2 = {};
        reset_setup();
        layout_group = setup->base->mod.input->config.xkb->group("Layout");

        QCOMPARE(layout_group.keyList().filter(QStringLiteral("LayoutDefault")).count(), 1);
    }

    SECTION("num_lock")
    {
        QCOMPARE(get_xkb_keys()->layouts_count(), 1u);
        QCOMPARE(get_xkb_keys()->layout_name(), "English (US)");

        // By default not set.
        QVERIFY(!(get_xkb_keys()->leds & input::keyboard_leds::num_lock));
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);

        // Now it should be on.
        QVERIFY(flags(get_xkb_keys()->leds & input::keyboard_leds::num_lock));

        // And back to off.
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);
        QVERIFY(!(get_xkb_keys()->leds & input::keyboard_leds::num_lock));

        // Let's reconfigure to enable through config.
        auto group = setup->base->mod.input->config.main->group("Keyboard");
        group.writeEntry("NumLock", 0);
        group.sync();

        // Without resetting the done flag should not be on.
        setup->base->mod.input->xkb.reconfigure();
        QVERIFY(!(get_xkb_keys()->leds & input::keyboard_leds::num_lock));

        // With the done flag unset it changes though.
        get_xkb_keys()->startup_num_lock_done = false;
        setup->base->mod.input->xkb.reconfigure();
        QVERIFY(flags(get_xkb_keys()->leds & input::keyboard_leds::num_lock));

        // Pressing should result in it being off.
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);
        QVERIFY(!(get_xkb_keys()->leds & input::keyboard_leds::num_lock));

        // Pressing again should enable it.
        keyboard_key_pressed(KEY_NUMLOCK, timestamp++);
        keyboard_key_released(KEY_NUMLOCK, timestamp++);
        QVERIFY(flags(get_xkb_keys()->leds & input::keyboard_leds::num_lock));

        // Now reconfigure to disable on load.
        group.writeEntry("NumLock", 1);
        group.sync();
        setup->base->mod.input->xkb.reconfigure();
        QVERIFY(!(get_xkb_keys()->leds & input::keyboard_leds::num_lock));
    }
}

}

#include "keyboard_layout.moc"
