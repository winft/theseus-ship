/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "win/space.h"
#include "win/space_reconfigure.h"

#include <KConfigGroup>

#include <QDBusConnection>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

using namespace Wrapland::Client;

static const QString s_serviceName = QStringLiteral("org.kde.KWin.Test.ModifierOnlyShortcut");
static const QString s_path = QStringLiteral("/Test");
static const QStringList trigger
    = QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")};

namespace KWin::detail::test
{

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

TEST_CASE("modifier only shortcut", "[input]")
{
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

    test::setup setup("mod-only-shortcut");
    setup.start();
    Test::setup_wayland_connection();
    Test::cursor()->set_pos(QPoint(640, 512));

    SECTION("trigger")
    {
        enum class key {
            meta,
            alt,
            control,
            shift,
        };

        auto key = GENERATE(key::meta, key::alt, key::control, key::shift);
        auto is_left_key = GENERATE(true, false);

        struct {
            QStringList meta;
            QStringList alt;
            QStringList control;
            QStringList shift;
        } config;

        int modifier;
        auto non_triggering_mods = std::vector<int>{KEY_LEFTMETA,
                                                    KEY_RIGHTMETA,
                                                    KEY_LEFTALT,
                                                    KEY_RIGHTALT,
                                                    KEY_LEFTCTRL,
                                                    KEY_RIGHTCTRL,
                                                    KEY_LEFTSHIFT,
                                                    KEY_RIGHTSHIFT};

        auto remove_triggering_mods = [&non_triggering_mods](int trigger1, int trigger2) {
            remove_all(non_triggering_mods, trigger1);
            remove_all(non_triggering_mods, trigger2);
        };

        switch (key) {
        case key::meta:
            config.meta = trigger;
            modifier = is_left_key ? KEY_LEFTMETA : KEY_RIGHTMETA;
            remove_triggering_mods(KEY_LEFTMETA, KEY_RIGHTMETA);
            break;
        case key::alt:
            config.alt = trigger;
            modifier = is_left_key ? KEY_LEFTALT : KEY_RIGHTALT;
            remove_triggering_mods(KEY_LEFTALT, KEY_RIGHTALT);
            break;
        case key::control:
            config.control = trigger;
            modifier = is_left_key ? KEY_LEFTCTRL : KEY_RIGHTCTRL;
            remove_triggering_mods(KEY_LEFTCTRL, KEY_RIGHTCTRL);
            break;
        case key::shift:
            config.shift = trigger;
            modifier = is_left_key ? KEY_LEFTSHIFT : KEY_RIGHTSHIFT;
            remove_triggering_mods(KEY_LEFTSHIFT, KEY_RIGHTSHIFT);
            break;
        default:
            REQUIRE(false);
        };

        // this test verifies that modifier only shortcut triggers correctly
        Target target;
        QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
        QVERIFY(triggeredSpy.isValid());

        auto group = setup.base->config.main->group("ModifierOnlyShortcuts");
        group.writeEntry("Meta", config.meta);
        group.writeEntry("Alt", config.alt);
        group.writeEntry("Shift", config.shift);
        group.writeEntry("Control", config.control);
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // configured shortcut should trigger
        quint32 timestamp = 1;

        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        TRY_REQUIRE(triggeredSpy.size() == 1);

        // the other shortcuts should not trigger
        for (auto mod : non_triggering_mods) {
            Test::keyboard_key_pressed(mod, timestamp++);
            Test::keyboard_key_released(mod, timestamp++);
            QCOMPARE(triggeredSpy.count(), 1);
        }

        // try configured again
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        TRY_REQUIRE(triggeredSpy.size() == 2);

        // click another key while modifier is held
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_pressed(KEY_A, timestamp++);
        Test::keyboard_key_released(KEY_A, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        // release other key after modifier release
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_pressed(KEY_A, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        Test::keyboard_key_released(KEY_A, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        // press key before pressing modifier
        Test::keyboard_key_pressed(KEY_A, timestamp++);
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        Test::keyboard_key_released(KEY_A, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        // mouse button pressed before clicking modifier
        Test::pointer_button_pressed(BTN_LEFT, timestamp++);
        QTRY_COMPARE(setup.base->space->input->qtButtonStates(), Qt::LeftButton);

        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        Test::pointer_button_released(BTN_LEFT, timestamp++);
        QTRY_COMPARE(setup.base->space->input->qtButtonStates(), Qt::NoButton);
        QCOMPARE(triggeredSpy.count(), 2);

        // mouse button press before mod press, release before mod release
        Test::pointer_button_pressed(BTN_LEFT, timestamp++);
        QTRY_COMPARE(setup.base->space->input->qtButtonStates(), Qt::LeftButton);

        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::pointer_button_released(BTN_LEFT, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(setup.base->space->input->qtButtonStates(), Qt::NoButton);
        QCOMPARE(triggeredSpy.count(), 2);

        // mouse button click while mod is pressed
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::pointer_button_pressed(BTN_LEFT, timestamp++);
        QTRY_COMPARE(setup.base->space->input->qtButtonStates(), Qt::LeftButton);

        Test::pointer_button_released(BTN_LEFT, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(setup.base->space->input->qtButtonStates(), Qt::NoButton);
        QCOMPARE(triggeredSpy.count(), 2);

        // scroll while mod is pressed
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::pointer_axis_vertical(5.0, timestamp++, 0);
        Test::keyboard_key_released(modifier, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        // same for horizontal
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::pointer_axis_horizontal(5.0, timestamp++, 0);
        Test::keyboard_key_released(modifier, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        // now try to lock the screen while modifier key is pressed
        Test::keyboard_key_pressed(modifier, timestamp++);

        Test::lock_screen();

        Test::keyboard_key_released(modifier, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        // now trigger while screen is locked, should also not work
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QCOMPARE(triggeredSpy.count(), 2);

        Test::unlock_screen();
    }

    SECTION("caps lock")
    {
        // this test verifies that Capslock does not trigger the shift shortcut
        // but other shortcuts still trigger even when Capslock is on
        Target target;
        QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
        QVERIFY(triggeredSpy.isValid());

        auto group = setup.base->config.main->group("ModifierOnlyShortcuts");
        group.writeEntry("Meta", QStringList());
        group.writeEntry("Alt", QStringList());
        group.writeEntry(
            "Shift", QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")});
        group.writeEntry("Control", QStringList());
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // first test that the normal shortcut triggers
        quint32 timestamp = 1;
        const int modifier = KEY_LEFTSHIFT;
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(triggeredSpy.count(), 1);

        // now capslock
        Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        QTRY_COMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input),
                     Qt::ShiftModifier);
        QTRY_COMPARE(triggeredSpy.count(), 1);

        // currently caps lock is on
        // shift still triggers
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input),
                     Qt::ShiftModifier);
        QTRY_COMPARE(triggeredSpy.count(), 2);

        // meta should also trigger
        group.writeEntry(
            "Meta", QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")});
        group.writeEntry("Alt", QStringList());
        group.writeEntry("Shift", QStringList{});
        group.writeEntry("Control", QStringList());
        group.sync();
        win::space_reconfigure(*setup.base->space);

        Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        TRY_REQUIRE(input::xkb::get_active_keyboard_modifiers(*setup.base->input)
                    == (Qt::ShiftModifier | Qt::MetaModifier));
        TRY_REQUIRE(input::xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(
                        *setup.base->input)
                    == Qt::MetaModifier);

        Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);
        QTRY_COMPARE(triggeredSpy.count(), 3);

        // set back to shift to ensure we don't trigger with capslock
        group.writeEntry("Meta", QStringList());
        group.writeEntry("Alt", QStringList());
        group.writeEntry(
            "Shift", QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")});
        group.writeEntry("Control", QStringList());
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // release caps lock
        Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        QTRY_COMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::NoModifier);
        QTRY_COMPARE(triggeredSpy.count(), 3);
    }

    SECTION("global shortcuts disabled")
    {
        auto modifier = GENERATE(KEY_LEFTMETA,
                                 KEY_RIGHTMETA,
                                 KEY_LEFTALT,
                                 KEY_RIGHTALT,
                                 KEY_LEFTCTRL,
                                 KEY_RIGHTCTRL,
                                 KEY_LEFTSHIFT,
                                 KEY_RIGHTSHIFT);

        struct {
            QStringList meta;
            QStringList alt;
            QStringList control;
            QStringList shift;
        } config;

        switch (modifier) {
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            config.meta = trigger;
            break;
        case KEY_LEFTALT:
        case KEY_RIGHTALT:
            config.alt = trigger;
            break;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
            config.control = trigger;
            break;
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
            config.shift = trigger;
            break;
        default:
            REQUIRE(false);
        };

        // this test verifies that when global shortcuts are disabled inside KWin (e.g. through a
        // window rule) the modifier only shortcuts do not trigger. see BUG: 370146
        Target target;
        QSignalSpy triggeredSpy(&target, &Target::shortcutTriggered);
        QVERIFY(triggeredSpy.isValid());

        auto group = setup.base->config.main->group("ModifierOnlyShortcuts");
        group.writeEntry("Meta", config.meta);
        group.writeEntry("Alt", config.alt);
        group.writeEntry("Shift", config.shift);
        group.writeEntry("Control", config.control);
        group.sync();
        win::space_reconfigure(*setup.base->space);

        // trigger once to verify the shortcut works
        quint32 timestamp = 1;
        QVERIFY(!setup.base->space->global_shortcuts_disabled);
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(triggeredSpy.count(), 1);
        triggeredSpy.clear();

        // now disable global shortcuts
        win::set_global_shortcuts_disabled(*setup.base->space, true);
        QVERIFY(setup.base->space->global_shortcuts_disabled);
        // Should not get triggered
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(triggeredSpy.count(), 0);
        triggeredSpy.clear();

        // enable again
        win::set_global_shortcuts_disabled(*setup.base->space, false);
        QVERIFY(!setup.base->space->global_shortcuts_disabled);
        // should get triggered again
        Test::keyboard_key_pressed(modifier, timestamp++);
        Test::keyboard_key_released(modifier, timestamp++);
        QTRY_COMPARE(triggeredSpy.count(), 1);
    }
}

}

#include "modifier_only_shortcut_test.moc"
