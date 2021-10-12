/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

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

#include "effect_builtins.h"
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

class WindowGeometryTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testStartup();
};

void WindowGeometryTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::Effect*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    const auto builtinNames = render::effect_loader().listOfKnownEffects();
    for (QString name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    plugins.writeEntry(QStringLiteral("windowgeometryEnabled"), true);

    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
    QVERIFY(render::compositor::self());
}

void WindowGeometryTest::init()
{
    Test::setup_wayland_connection();
}

void WindowGeometryTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void WindowGeometryTest::testStartup()
{
    // just a test to load the effect to verify it doesn't crash
    auto e = static_cast<render::effects_handler_impl*>(effects);
    QVERIFY(e->isEffectLoaded(QStringLiteral("windowgeometry")));
}

}

WAYLANDTEST_MAIN(KWin::WindowGeometryTest)
#include "windowgeometry_test.moc"
