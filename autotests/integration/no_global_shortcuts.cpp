/*
SPDX-FileCopyrightText: 2018 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/helpers.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/space_reconfigure.h"

#include <KConfigGroup>
#include <KGlobalAccel>

#include <QDBusConnection>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

using namespace Wrapland::Client;

static const QString s_serviceName = QStringLiteral("org.kde.KWin.Test.NoGlobalShortcuts");
static const QString s_path = QStringLiteral("/Test");
static const QStringList trigger
    = QStringList{s_serviceName, s_path, s_serviceName, QStringLiteral("shortcut")};

namespace
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

}

namespace KWin::detail::test
{

TEST_CASE("no global shortcuts", "[input]")
{
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");
    qputenv("XKB_DEFAULT_RULES", "evdev");

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup(
        "no-global-shortcuts", operation_mode, base::wayland::start_options::no_global_shortcuts);
    setup.start();
    cursor()->set_pos(QPoint(640, 512));

    SECTION("trigger")
    {
        // Based on modifier only shortcut trigger section.

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

        uint32_t modifier;
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
        keyboard_key_pressed(modifier, timestamp++);
        keyboard_key_released(modifier, timestamp++);
        REQUIRE(!triggeredSpy.wait(100));

        // the other shortcuts should not trigger
        for (auto mod : non_triggering_mods) {
            keyboard_key_pressed(mod, timestamp++);
            keyboard_key_released(mod, timestamp++);
            REQUIRE(triggeredSpy.size() == 0);
        }
    }

    SECTION("kglobalaccel")
    {
        std::unique_ptr<QAction> action(new QAction(nullptr));
        action->setProperty("componentName", QStringLiteral(KWIN_NAME));
        action->setObjectName(QStringLiteral("globalshortcuts-test-meta-shift-w"));
        QSignalSpy triggeredSpy(action.get(), &QAction::triggered);
        QVERIFY(triggeredSpy.isValid());
        KGlobalAccel::self()->setShortcut(action.get(),
                                          QList<QKeySequence>{Qt::META | Qt::SHIFT | Qt::Key_W},
                                          KGlobalAccel::NoAutoloading);
        setup.base->input->registerShortcut(Qt::META | Qt::SHIFT | Qt::Key_W, action.get());

        // press meta+shift+w
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::MetaModifier);

        keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        REQUIRE(input::xkb::get_active_keyboard_modifiers(*setup.base->input)
                == (Qt::ShiftModifier | Qt::MetaModifier));

        keyboard_key_pressed(KEY_W, timestamp++);
        keyboard_key_released(KEY_W, timestamp++);

        // release meta+shift
        keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);

        REQUIRE(!triggeredSpy.wait(100));
        QCOMPARE(triggeredSpy.count(), 0);
    }

    SECTION("pointer shortcut")
    {
        // based on LockScreentestPointerShortcut
        std::unique_ptr<QAction> action(new QAction(nullptr));
        QSignalSpy actionSpy(action.get(), &QAction::triggered);
        QVERIFY(actionSpy.isValid());

        setup.base->input->shortcuts->registerPointerShortcut(
            action.get(), Qt::MetaModifier, Qt::LeftButton);

        // try to trigger the shortcut
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QCoreApplication::instance()->processEvents();
        QCOMPARE(actionSpy.count(), 0);
        pointer_button_released(BTN_LEFT, timestamp++);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
        QCoreApplication::instance()->processEvents();
        QCOMPARE(actionSpy.count(), 0);
    }

    SECTION("axis shortcut")
    {
        auto sign = GENERATE(-1, 1);
        auto direction = GENERATE(Qt::Vertical, Qt::Horizontal);

        // based on LockScreentestAxisShortcut
        std::unique_ptr<QAction> action(new QAction(nullptr));
        QSignalSpy actionSpy(action.get(), &QAction::triggered);
        QVERIFY(actionSpy.isValid());
        PointerAxisDirection axisDirection = PointerAxisUp;

        if (direction == Qt::Vertical) {
            axisDirection = sign > 0 ? PointerAxisUp : PointerAxisDown;
        } else {
            axisDirection = sign > 0 ? PointerAxisLeft : PointerAxisRight;
        }

        setup.base->input->shortcuts->registerAxisShortcut(
            action.get(), Qt::MetaModifier, axisDirection);

        // try to trigger the shortcut
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);

        if (direction == Qt::Vertical)
            pointer_axis_vertical(sign * 5.0, timestamp++, 0);
        else
            pointer_axis_horizontal(sign * 5.0, timestamp++, 0);

        QCoreApplication::instance()->processEvents();
        QCOMPARE(actionSpy.count(), 0);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
        QCoreApplication::instance()->processEvents();
        QCOMPARE(actionSpy.count(), 0);
    }

    SECTION("screen edge")
    {
        // based on LockScreentestScreenEdge
        QSignalSpy screenEdgeSpy(setup.base->space->edges->qobject.get(),
                                 &win::screen_edger_qobject::approaching);
        QVERIFY(screenEdgeSpy.isValid());
        QCOMPARE(screenEdgeSpy.count(), 0);

        quint32 timestamp = 1;
        pointer_motion_absolute({5, 5}, timestamp++);
        QCOMPARE(screenEdgeSpy.count(), 0);
    }
}

}

#include "no_global_shortcuts.moc"
