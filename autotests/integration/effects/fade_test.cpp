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

#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/wayland/window.h"

#include <KConfigGroup>

#include <Wrapland/Client/buffer.h>
#include <Wrapland/Client/surface.h>

using namespace Wrapland::Client;

namespace KWin
{

class FadeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testWindowCloseAfterWindowHidden();

private:
    Effect* m_fadeEffect = nullptr;
};

void FadeTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<KWin::Effect*>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    const auto builtinNames = render::effect_loader().listOfKnownEffects();
    for (QString name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QVERIFY(render::compositor::self());
}

void FadeTest::init()
{
    Test::setup_wayland_connection();

    // load the translucency effect
    auto e = static_cast<render::effects_handler_impl*>(effects);
    // find the effectsloader
    auto effectloader = e->findChild<render::basic_effect_loader*>();
    QVERIFY(effectloader);
    QSignalSpy effectLoadedSpy(effectloader, &render::basic_effect_loader::effectLoaded);
    QVERIFY(effectLoadedSpy.isValid());

    QVERIFY(!e->isEffectLoaded(QStringLiteral("kwin4_effect_fade")));
    QVERIFY(e->loadEffect(QStringLiteral("kwin4_effect_fade")));
    QVERIFY(e->isEffectLoaded(QStringLiteral("kwin4_effect_fade")));

    QCOMPARE(effectLoadedSpy.count(), 1);
    m_fadeEffect = effectLoadedSpy.first().first().value<Effect*>();
    QVERIFY(m_fadeEffect);
}

void FadeTest::cleanup()
{
    Test::destroy_wayland_connection();
    auto e = static_cast<render::effects_handler_impl*>(effects);
    if (e->isEffectLoaded(QStringLiteral("kwin4_effect_fade"))) {
        e->unloadEffect(QStringLiteral("kwin4_effect_fade"));
    }
    QVERIFY(!e->isEffectLoaded(QStringLiteral("kwin4_effect_fade")));
    m_fadeEffect = nullptr;
}

void FadeTest::testWindowCloseAfterWindowHidden()
{
    // this test simulates the showing/hiding/closing of a Wayland window
    // especially the situation that a window got unmapped and destroyed way later
    QVERIFY(!m_fadeEffect->isActive());

    QSignalSpy windowAddedSpy(effects, &EffectsHandler::windowAdded);
    QVERIFY(windowAddedSpy.isValid());
    QSignalSpy windowHiddenSpy(effects, &EffectsHandler::windowHidden);
    QVERIFY(windowHiddenSpy.isValid());
    QSignalSpy windowShownSpy(effects, &EffectsHandler::windowShown);
    QVERIFY(windowShownSpy.isValid());
    QSignalSpy windowClosedSpy(effects, &EffectsHandler::windowClosed);
    QVERIFY(windowClosedSpy.isValid());

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QTRY_COMPARE(windowAddedSpy.count(), 1);
    QTRY_COMPARE(m_fadeEffect->isActive(), true);

    QTest::qWait(500);
    QTRY_COMPARE(m_fadeEffect->isActive(), false);

    // now unmap the surface
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(windowHiddenSpy.wait());
    QCOMPARE(m_fadeEffect->isActive(), false);

    // and map again
    Test::render(surface, QSize(100, 50), Qt::red);
    QVERIFY(windowShownSpy.wait());
    QCOMPARE(m_fadeEffect->isActive(), false);

    // and unmap once more
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(windowHiddenSpy.wait());
    QCOMPARE(m_fadeEffect->isActive(), false);

    // and now destroy
    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());
    QCOMPARE(m_fadeEffect->isActive(), true);
    QTest::qWait(500);
    QTRY_COMPARE(m_fadeEffect->isActive(), false);
}

}

WAYLANDTEST_MAIN(KWin::FadeTest)
#include "fade_test.moc"
