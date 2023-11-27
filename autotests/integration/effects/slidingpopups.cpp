/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KConfigGroup>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/slide.h>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

TEST_CASE("slidingpopups", "[effect]")
{
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<KWin::Effect*>();

    test::setup setup("slidingpopups", base::operation_mode::xwayland);

    // disable all effects - we don't want to have it interact with the rendering
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*setup.base->mod.render).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    KConfigGroup wobblyGroup = config->group(QStringLiteral("Effect-Wobbly"));
    wobblyGroup.writeEntry(QStringLiteral("Settings"), QStringLiteral("Custom"));
    wobblyGroup.writeEntry(QStringLiteral("OpenEffect"), true);
    wobblyGroup.writeEntry(QStringLiteral("CloseEffect"), true);
    config->sync();

    setup.start();
    QVERIFY(setup.base->mod.render);
    auto& effects_impl = setup.base->mod.render->effects;
    while (!effects_impl->loadedEffects().isEmpty()) {
        auto const effect = effects_impl->loadedEffects().constFirst();
        effects_impl->unloadEffect(effect);
        QVERIFY(!effects_impl->isEffectLoaded(effect));
    }

    auto& scene = setup.base->mod.render->scene;
    QVERIFY(scene);
    REQUIRE(scene->isOpenGl());

    setup_wayland_connection(global_selection::xdg_decoration);

    SECTION("with other effect")
    {
        // this test verifies that slidingpopups effect grabs the window added role
        // independently of the sequence how the effects are loaded.
        // see BUG 336866
        auto effectsToLoad
            = GENERATE(QStringList{QStringLiteral("fade"), QStringLiteral("slidingpopups")},
                       QStringList{QStringLiteral("slidingpopups"), QStringLiteral("fade")},
                       QStringList{QStringLiteral("scale"), QStringLiteral("slidingpopups")},
                       QStringList{QStringLiteral("slidingpopups"), QStringLiteral("scale")});

        // find the effectsloader
        auto& e = setup.base->mod.render->effects;
        auto effectloader = e->findChild<render::basic_effect_loader*>();
        QVERIFY(effectloader);
        QSignalSpy effectLoadedSpy(effectloader, &render::basic_effect_loader::effectLoaded);
        QVERIFY(effectLoadedSpy.isValid());

        Effect* slidingPoupus = nullptr;
        Effect* otherEffect = nullptr;
        for (auto const& effectName : effectsToLoad) {
            QVERIFY(!e->isEffectLoaded(effectName));
            QVERIFY(e->loadEffect(effectName));
            QVERIFY(e->isEffectLoaded(effectName));

            QCOMPARE(effectLoadedSpy.count(), 1);
            Effect* effect = effectLoadedSpy.first().first().value<Effect*>();
            if (effectName == QStringLiteral("slidingpopups")) {
                slidingPoupus = effect;
            } else {
                otherEffect = effect;
            }
            effectLoadedSpy.clear();
        }
        QVERIFY(slidingPoupus);
        QVERIFY(otherEffect);

        QVERIFY(!slidingPoupus->isActive());
        QVERIFY(!otherEffect->isActive());

        // give the compositor some time to render
        QTest::qWait(50);

        QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
        QVERIFY(windowAddedSpy.isValid());

        // create an xcb window
        auto c = xcb_connection_create();
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
        winInfo.setWindowType(win::win_type::normal);

        // and get the slide atom
        const QByteArray effectAtomName = QByteArrayLiteral("_KDE_SLIDE");
        xcb_intern_atom_cookie_t atomCookie = xcb_intern_atom_unchecked(
            c.get(), false, effectAtomName.length(), effectAtomName.constData());
        const int size = 2;
        int32_t data[size];
        data[0] = 0;
        data[1] = 0;
        std::unique_ptr<xcb_intern_atom_reply_t, decltype(std::free)*> atom{
            xcb_intern_atom_reply(c.get(), atomCookie, nullptr), std::free};
        QVERIFY(atom);
        xcb_change_property(
            c.get(), XCB_PROP_MODE_REPLACE, w, atom->atom, atom->atom, 32, size, data);

        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.first().first().value<quint32>();
        auto client = get_x11_window(setup.base->mod.space->windows_map.at(client_id));
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(win::is_normal(client));

        // sliding popups should be active
        QVERIFY(windowAddedSpy.wait());
        QTRY_VERIFY(slidingPoupus->isActive());
        QVERIFY(!otherEffect->isActive());

        // wait till effect ends
        QTRY_VERIFY(!slidingPoupus->isActive());
        QTest::qWait(300);
        QVERIFY(!otherEffect->isActive());

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());

        QSignalSpy windowDeletedSpy(effects, &EffectsHandler::windowDeleted);
        QVERIFY(windowDeletedSpy.isValid());
        QVERIFY(windowDeletedSpy.wait());

        // again we should have the sliding popups active
        QVERIFY(slidingPoupus->isActive());
        QVERIFY(!otherEffect->isActive());

        QTRY_VERIFY(!slidingPoupus->isActive());
        QCOMPARE(windowClosedSpy.count(), 1);
        QTest::qWait(300);
        QVERIFY(!otherEffect->isActive());
        xcb_destroy_window(c.get(), w);
        c.reset();
    }

    SECTION("with other effect and wayland window")
    {
        // this test verifies that slidingpopups effect grabs the window added role
        // independently of the sequence how the effects are loaded.
        // see BUG 336866
        // the test is like "with other effect", but simulates using a Wayland window
        auto effectsToLoad
            = GENERATE(QStringList{QStringLiteral("fade"), QStringLiteral("slidingpopups")},
                       QStringList{QStringLiteral("slidingpopups"), QStringLiteral("fade")},
                       QStringList{QStringLiteral("scale"), QStringLiteral("slidingpopups")},
                       QStringList{QStringLiteral("slidingpopups"), QStringLiteral("scale")});

        // find the effectsloader
        auto& e = setup.base->mod.render->effects;
        auto effectloader = e->findChild<render::basic_effect_loader*>();
        QVERIFY(effectloader);
        QSignalSpy effectLoadedSpy(effectloader, &render::basic_effect_loader::effectLoaded);
        QVERIFY(effectLoadedSpy.isValid());

        Effect* slidingPoupus = nullptr;
        Effect* otherEffect = nullptr;

        for (auto const& effectName : effectsToLoad) {
            QVERIFY(!e->isEffectLoaded(effectName));
            QVERIFY(e->loadEffect(effectName));
            QVERIFY(e->isEffectLoaded(effectName));

            QCOMPARE(effectLoadedSpy.count(), 1);
            Effect* effect = effectLoadedSpy.first().first().value<Effect*>();
            if (effectName == QStringLiteral("slidingpopups")) {
                slidingPoupus = effect;
            } else {
                otherEffect = effect;
            }
            effectLoadedSpy.clear();
        }

        QVERIFY(slidingPoupus);
        QVERIFY(otherEffect);

        QVERIFY(!slidingPoupus->isActive());
        QVERIFY(!otherEffect->isActive());
        QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
        QVERIFY(windowAddedSpy.isValid());

        using namespace Wrapland::Client;
        // the test created the slide protocol, let's create a Registry and listen for it
        std::unique_ptr<Registry> registry(new Registry);
        registry->create(get_client().connection);

        QSignalSpy interfacesAnnouncedSpy(registry.get(), &Registry::interfacesAnnounced);
        QVERIFY(interfacesAnnouncedSpy.isValid());
        registry->setup();
        QVERIFY(interfacesAnnouncedSpy.wait());
        auto slideInterface = registry->interface(Registry::Interface::Slide);
        QVERIFY(slideInterface.name != 0);
        std::unique_ptr<SlideManager> slideManager(
            registry->createSlideManager(slideInterface.name, slideInterface.version));
        QVERIFY(slideManager);

        // create Wayland window
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<Slide> slide(slideManager->createSlide(surface.get()));
        slide->setLocation(Slide::Location::Left);
        slide->commit();
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        QCOMPARE(windowAddedSpy.count(), 0);
        auto client = render_and_wait_for_shown(surface, QSize(10, 20), Qt::blue);
        QVERIFY(client);
        QVERIFY(win::is_normal(client));

        // sliding popups should be active
        QCOMPARE(windowAddedSpy.count(), 1);
        QTRY_VERIFY(slidingPoupus->isActive());
        QVERIFY(!otherEffect->isActive());

        // wait till effect ends
        QTRY_VERIFY(!slidingPoupus->isActive());
        QTest::qWait(300);
        QVERIFY(!otherEffect->isActive());

        // and destroy the window again
        shellSurface.reset();
        surface.reset();

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());

        QSignalSpy windowDeletedSpy(effects, &EffectsHandler::windowDeleted);
        QVERIFY(windowDeletedSpy.isValid());
        QVERIFY(windowDeletedSpy.wait());

        // again we should have the sliding popups active
        QVERIFY(slidingPoupus->isActive());
        QVERIFY(!otherEffect->isActive());

        QTRY_VERIFY(!slidingPoupus->isActive());
        QCOMPARE(windowClosedSpy.count(), 1);
        QTest::qWait(300);
        QVERIFY(!otherEffect->isActive());
    }
}

}
