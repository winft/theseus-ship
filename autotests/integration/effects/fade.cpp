/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KConfigGroup>
#include <Wrapland/Client/buffer.h>
#include <Wrapland/Client/surface.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("fade", "[effect]")
{
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<KWin::Effect*>();

    test::setup setup("fade");

    // disable all effects - we don't want to have it interact with the rendering
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*setup.base->mod.render).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    QVERIFY(setup.base->mod.render);
    setup_wayland_connection();

    // load the translucency effect
    auto& e = setup.base->mod.render->effects;

    QSignalSpy effectLoadedSpy(e->loader.get(), &render::basic_effect_loader::effectLoaded);
    QVERIFY(effectLoadedSpy.isValid());

    QVERIFY(!e->isEffectLoaded(QStringLiteral("fade")));
    QVERIFY(e->loadEffect(QStringLiteral("fade")));
    QVERIFY(e->isEffectLoaded(QStringLiteral("fade")));

    QCOMPARE(effectLoadedSpy.count(), 1);

    auto fade_effect = effectLoadedSpy.first().first().value<Effect*>();
    QVERIFY(fade_effect);

    SECTION("window close after hidden")
    {
        // this test simulates the showing/hiding/closing of a Wayland window
        // especially the situation that a window got unmapped and destroyed way later
        QVERIFY(!fade_effect->isActive());

        QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
        QVERIFY(windowAddedSpy.isValid());
        QSignalSpy windowClosedSpy(effects, &EffectsHandler::windowClosed);
        QVERIFY(windowClosedSpy.isValid());

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QTRY_COMPARE(windowAddedSpy.count(), 1);
        QTRY_COMPARE(fade_effect->isActive(), true);

        QSignalSpy windowHiddenSpy(c->render->effect.get(), &EffectWindow::windowHidden);
        QVERIFY(windowHiddenSpy.isValid());
        QSignalSpy windowShownSpy(c->render->effect.get(), &EffectWindow::windowShown);
        QVERIFY(windowShownSpy.isValid());

        QTest::qWait(500);
        QTRY_COMPARE(fade_effect->isActive(), false);

        // now unmap the surface
        surface->attachBuffer(Buffer::Ptr());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(windowHiddenSpy.wait());
        QCOMPARE(fade_effect->isActive(), false);

        // and map again
        render(surface, QSize(100, 50), Qt::red);
        QVERIFY(windowShownSpy.wait());
        QCOMPARE(fade_effect->isActive(), false);

        // and unmap once more
        surface->attachBuffer(Buffer::Ptr());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(windowHiddenSpy.wait());
        QCOMPARE(fade_effect->isActive(), false);

        // and now destroy
        shellSurface.reset();
        surface.reset();
        QVERIFY(windowClosedSpy.wait());
        QCOMPARE(fade_effect->isActive(), true);
        QTest::qWait(500);
        QTRY_COMPARE(fade_effect->isActive(), false);
    }
}

}
