/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "win/desktop_space.h"
#include "win/move.h"
#include "win/x11/window.h"

#include <KConfigGroup>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

namespace
{

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_disconnect);
}

}

TEST_CASE("translucency", "[effect]")
{
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<KWin::Effect*>();

    test::setup setup("translucency", base::operation_mode::xwayland);

    // disable all effects - we don't want to have it interact with the rendering
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*setup.base->render).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->group("Outline").writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    config->group("Effect-translucency").writeEntry(QStringLiteral("Dialogs"), 90);
    config->sync();

    setup.start();
    QVERIFY(setup.base->render->compositor);

    // load the translucency effect
    auto& e = setup.base->render->compositor->effects;
    QSignalSpy effectLoadedSpy(e->loader.get(), &render::basic_effect_loader::effectLoaded);
    QVERIFY(effectLoadedSpy.isValid());

    QVERIFY(!e->isEffectLoaded(QStringLiteral("translucency")));
    QVERIFY(e->loadEffect(QStringLiteral("translucency")));
    QVERIFY(e->isEffectLoaded(QStringLiteral("translucency")));
    QCOMPARE(effectLoadedSpy.count(), 1);

    auto translucency_effect = effectLoadedSpy.first().first().value<Effect*>();
    QVERIFY(translucency_effect);

    SECTION("move after subspace change")
    {
        // test tries to simulate the condition of bug 366081
        QVERIFY(!translucency_effect->isActive());

        QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
        QVERIFY(windowAddedSpy.isValid());

        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.first().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(win::decoration(client));

        QVERIFY(windowAddedSpy.wait());
        QVERIFY(!translucency_effect->isActive());

        // let's send the window to subspace 2
        effects->setNumberOfDesktops(2);
        QCOMPARE(effects->numberOfDesktops(), 2);
        win::send_window_to_subspace(*setup.base->space, client, 2, false);
        effects->setCurrentDesktop(2);
        QVERIFY(!translucency_effect->isActive());
        cursor()->set_pos(client->geo.frame.center());
        win::perform_window_operation(client, win::win_op::move);
        QVERIFY(translucency_effect->isActive());
        QTest::qWait(200);
        QVERIFY(translucency_effect->isActive());

        // now end move resize
        win::end_move_resize(client);

        QVERIFY(translucency_effect->isActive());
        QTest::qWait(500);
        QTRY_VERIFY(!translucency_effect->isActive());

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
        xcb_destroy_window(c.get(), w);
        c.reset();
    }

    SECTION("dialog close")
    {
        // this test simulates the condition of BUG 342716
        // with translucency settings for window type dialog the effect never ends when the window
        // gets destroyed
        QVERIFY(!translucency_effect->isActive());
        QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
        QVERIFY(windowAddedSpy.isValid());

        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info winInfo(c.get(),
                                        w,
                                        setup.base->x11_data.root_window,
                                        win::x11::net::Properties(),
                                        win::x11::net::Properties2());
        winInfo.setWindowType(win::win_type::dialog);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.first().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(win::decoration(client));
        QVERIFY(win::is_dialog(client));

        QVERIFY(windowAddedSpy.wait());
        QTRY_VERIFY(translucency_effect->isActive());
        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());

        QSignalSpy windowDeletedSpy(effects, &EffectsHandler::windowDeleted);
        QVERIFY(windowDeletedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
        if (windowDeletedSpy.isEmpty()) {
            QVERIFY(windowDeletedSpy.wait());
        }
        QCOMPARE(windowDeletedSpy.count(), 1);
        QTRY_VERIFY(!translucency_effect->isActive());
        xcb_destroy_window(c.get(), w);
        c.reset();
    }
}

}
