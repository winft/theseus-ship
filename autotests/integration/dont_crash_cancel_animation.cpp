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
#include "scripting/effect.h"
#include "toplevel.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <KDecoration2/Decoration>

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

namespace KWin
{

class DontCrashCancelAnimationFromAnimationEndedTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testScript();
};

void DontCrashCancelAnimationFromAnimationEndedTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(Test::app()->base.render->compositor);
    QVERIFY(startup_spy.size() || startup_spy.wait());
    QVERIFY(effects);
}

void DontCrashCancelAnimationFromAnimationEndedTest::init()
{
    Test::setup_wayland_connection();
}

void DontCrashCancelAnimationFromAnimationEndedTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void DontCrashCancelAnimationFromAnimationEndedTest::testScript()
{
    // load a scripted effect which deletes animation data
    auto effect = scripting::effect::create(QStringLiteral("crashy"),
                                            QFINDTESTDATA("data/anim-data-delete-effect/effect.js"),
                                            10,
                                            *effects);
    QVERIFY(effect);

    const auto children = effects->children();
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "KWin::render::effect_loader") != 0) {
            continue;
        }
        QVERIFY(QMetaObject::invokeMethod(*it,
                                          "effectLoaded",
                                          Q_ARG(KWin::Effect*, effect),
                                          Q_ARG(QString, QStringLiteral("crashy"))));
        break;
    }
    QVERIFY(
        Test::app()->base.render->compositor->effects->isEffectLoaded(QStringLiteral("crashy")));

    using namespace Wrapland::Client;
    // create a window
    auto surface = std::unique_ptr<Wrapland::Client::Surface>(Test::create_surface());
    QVERIFY(surface);
    auto shellSurface = std::unique_ptr<Wrapland::Client::XdgShellToplevel>(
        Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    // let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->stacking.active, c);

    // make sure we animate
    QTest::qWait(200);

    // wait for the window to be passed to Deleted
    QSignalSpy windowDeletedSpy(c->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowDeletedSpy.isValid());

    surface.reset();

    QVERIFY(windowDeletedSpy.wait());
    // make sure we animate
    QTest::qWait(200);
}

}

WAYLANDTEST_MAIN(KWin::DontCrashCancelAnimationFromAnimationEndedTest)
#include "dont_crash_cancel_animation.moc"
