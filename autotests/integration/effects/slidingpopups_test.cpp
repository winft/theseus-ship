/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/scene.h"
#include "win/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <KConfigGroup>

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/slide.h>
#include <Wrapland/Client/surface.h>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class SlidingPopupsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testWithOtherEffect_data();
    void testWithOtherEffect();
    void testWithOtherEffectWayland_data();
    void testWithOtherEffectWayland();
};

void SlidingPopupsTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<KWin::Effect*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = Test::app()->base.config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*effects, *Test::app()->base.render->compositor)
                                  .listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    KConfigGroup wobblyGroup = config->group("Effect-Wobbly");
    wobblyGroup.writeEntry(QStringLiteral("Settings"), QStringLiteral("Custom"));
    wobblyGroup.writeEntry(QStringLiteral("OpenEffect"), true);
    wobblyGroup.writeEntry(QStringLiteral("CloseEffect"), true);
    config->sync();

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QVERIFY(Test::app()->base.render->compositor);

    auto& scene = Test::app()->base.render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
}

void SlidingPopupsTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);
}

void SlidingPopupsTest::cleanup()
{
    Test::destroy_wayland_connection();
    auto& e = Test::app()->base.render->compositor->effects;
    while (!e->loadedEffects().isEmpty()) {
        const QString effect = e->loadedEffects().constFirst();
        e->unloadEffect(effect);
        QVERIFY(!e->isEffectLoaded(effect));
    }
}

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

void SlidingPopupsTest::testWithOtherEffect_data()
{
    QTest::addColumn<QStringList>("effectsToLoad");

    QTest::newRow("fade, slide") << QStringList{QStringLiteral("kwin4_effect_fade"),
                                                QStringLiteral("slidingpopups")};
    QTest::newRow("slide, fade") << QStringList{QStringLiteral("slidingpopups"),
                                                QStringLiteral("kwin4_effect_fade")};
    QTest::newRow("scale, slide") << QStringList{QStringLiteral("kwin4_effect_scale"),
                                                 QStringLiteral("slidingpopups")};
    QTest::newRow("slide, scale") << QStringList{QStringLiteral("slidingpopups"),
                                                 QStringLiteral("kwin4_effect_scale")};

    if (effects->compositingType() & KWin::OpenGLCompositing) {
        QTest::newRow("glide, slide")
            << QStringList{QStringLiteral("glide"), QStringLiteral("slidingpopups")};
        QTest::newRow("slide, glide")
            << QStringList{QStringLiteral("slidingpopups"), QStringLiteral("glide")};
        QTest::newRow("wobblywindows, slide")
            << QStringList{QStringLiteral("wobblywindows"), QStringLiteral("slidingpopups")};
        QTest::newRow("slide, wobblywindows")
            << QStringList{QStringLiteral("slidingpopups"), QStringLiteral("wobblywindows")};
        QTest::newRow("fallapart, slide")
            << QStringList{QStringLiteral("fallapart"), QStringLiteral("slidingpopups")};
        QTest::newRow("slide, fallapart")
            << QStringList{QStringLiteral("slidingpopups"), QStringLiteral("fallapart")};
    }
}

void SlidingPopupsTest::testWithOtherEffect()
{
    // this test verifies that slidingpopups effect grabs the window added role
    // independently of the sequence how the effects are loaded.
    // see BUG 336866
    auto& e = Test::app()->base.render->compositor->effects;
    // find the effectsloader
    auto effectloader = e->findChild<render::basic_effect_loader*>();
    QVERIFY(effectloader);
    QSignalSpy effectLoadedSpy(effectloader, &render::basic_effect_loader::effectLoaded);
    QVERIFY(effectLoadedSpy.isValid());

    Effect* slidingPoupus = nullptr;
    Effect* otherEffect = nullptr;
    QFETCH(QStringList, effectsToLoad);
    for (const QString& effectName : effectsToLoad) {
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
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    NETWinInfo winInfo(c.get(), w, rootWindow(), NET::Properties(), NET::Properties2());
    winInfo.setWindowType(NET::Normal);

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
    xcb_change_property(c.get(), XCB_PROP_MODE_REPLACE, w, atom->atom, atom->atom, 32, size, data);

    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client_id = windowCreatedSpy.first().first().value<quint32>();
    auto client = Test::get_x11_window(Test::app()->base.space->windows_map.at(client_id));
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

void SlidingPopupsTest::testWithOtherEffectWayland_data()
{
    QTest::addColumn<QStringList>("effectsToLoad");

    QTest::newRow("fade, slide") << QStringList{QStringLiteral("kwin4_effect_fade"),
                                                QStringLiteral("slidingpopups")};
    QTest::newRow("slide, fade") << QStringList{QStringLiteral("slidingpopups"),
                                                QStringLiteral("kwin4_effect_fade")};
    QTest::newRow("scale, slide") << QStringList{QStringLiteral("kwin4_effect_scale"),
                                                 QStringLiteral("slidingpopups")};
    QTest::newRow("slide, scale") << QStringList{QStringLiteral("slidingpopups"),
                                                 QStringLiteral("kwin4_effect_scale")};

    if (effects->compositingType() & KWin::OpenGLCompositing) {
        QTest::newRow("glide, slide")
            << QStringList{QStringLiteral("glide"), QStringLiteral("slidingpopups")};
        QTest::newRow("slide, glide")
            << QStringList{QStringLiteral("slidingpopups"), QStringLiteral("glide")};
        QTest::newRow("wobblywindows, slide")
            << QStringList{QStringLiteral("wobblywindows"), QStringLiteral("slidingpopups")};
        QTest::newRow("slide, wobblywindows")
            << QStringList{QStringLiteral("slidingpopups"), QStringLiteral("wobblywindows")};
        QTest::newRow("fallapart, slide")
            << QStringList{QStringLiteral("fallapart"), QStringLiteral("slidingpopups")};
        QTest::newRow("slide, fallapart")
            << QStringList{QStringLiteral("slidingpopups"), QStringLiteral("fallapart")};
    }
}

void SlidingPopupsTest::testWithOtherEffectWayland()
{
    // this test verifies that slidingpopups effect grabs the window added role
    // independently of the sequence how the effects are loaded.
    // see BUG 336866
    // the test is like testWithOtherEffect, but simulates using a Wayland window
    auto& e = Test::app()->base.render->compositor->effects;
    // find the effectsloader
    auto effectloader = e->findChild<render::basic_effect_loader*>();
    QVERIFY(effectloader);
    QSignalSpy effectLoadedSpy(effectloader, &render::basic_effect_loader::effectLoaded);
    QVERIFY(effectLoadedSpy.isValid());

    Effect* slidingPoupus = nullptr;
    Effect* otherEffect = nullptr;
    QFETCH(QStringList, effectsToLoad);
    for (const QString& effectName : effectsToLoad) {
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
    registry->create(Test::get_client().connection);

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
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<Slide> slide(slideManager->createSlide(surface.get()));
    slide->setLocation(Slide::Location::Left);
    slide->commit();
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    QCOMPARE(windowAddedSpy.count(), 0);
    auto client = Test::render_and_wait_for_shown(surface, QSize(10, 20), Qt::blue);
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

WAYLANDTEST_MAIN(KWin::SlidingPopupsTest)
#include "slidingpopups_test.moc"
