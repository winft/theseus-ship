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
#include "input/cursor.h"
#include "render/effect_loader.h"
#include "scripting/platform.h"
#include "scripting/script.h"
#include "win/space.h"
#include "win/space_reconfigure.h"

#define private public
#include "win/screen_edges.h"
#undef private

#include <KConfigGroup>

Q_DECLARE_METATYPE(KWin::ElectricBorder)

namespace KWin
{

class ScreenEdgeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testEdge_data();
    void testEdge();
    void testTouchEdge_data();
    void testTouchEdge();
    void testEdgeUnregister();
    void testDeclarativeTouchEdge();

private:
    void triggerConfigReload();
};

void ScreenEdgeTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // empty config to have defaults
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);

    // disable all effects to prevent them grabbing edges
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*effects, *Test::app()->base.render->compositor)
                                  .listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    // disable electric border pushback
    config->group("Windows").writeEntry("ElectricBorderPushbackPixels", 0);
    config->group("TabBox").writeEntry("TouchBorderActivate", int(ElectricNone));

    config->sync();
    kwinApp()->setConfig(config);

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QVERIFY(Test::app()->base.space->scripting);

    Test::app()->base.space->edges->time_threshold = 0;
    Test::app()->base.space->edges->reactivate_threshold = 0;
}

void ScreenEdgeTest::init()
{
    Test::cursor()->set_pos(640, 512);
    if (Test::app()->base.space->showing_desktop) {
        win::toggle_show_desktop(*Test::app()->base.space);
    }
    QVERIFY(!Test::app()->base.space->showing_desktop);
}

void ScreenEdgeTest::cleanup()
{
    // try to unload the script
    const QStringList scripts = {QFINDTESTDATA("./scripts/screenedge.js"),
                                 QFINDTESTDATA("./scripts/screenedgeunregister.js"),
                                 QFINDTESTDATA("./scripts/touchScreenedge.js")};
    for (const QString& script : scripts) {
        if (!script.isEmpty()) {
            if (Test::app()->base.space->scripting->isScriptLoaded(script)) {
                QVERIFY(Test::app()->base.space->scripting->unloadScript(script));
                QTRY_VERIFY(!Test::app()->base.space->scripting->isScriptLoaded(script));
            }
        }
    }
}

void ScreenEdgeTest::testEdge_data()
{
    QTest::addColumn<KWin::ElectricBorder>("edge");
    QTest::addColumn<QPoint>("triggerPos");

    QTest::newRow("Top") << KWin::ElectricTop << QPoint(512, 0);
    QTest::newRow("TopRight") << KWin::ElectricTopRight << QPoint(1279, 0);
    QTest::newRow("Right") << KWin::ElectricRight << QPoint(1279, 512);
    QTest::newRow("BottomRight") << KWin::ElectricBottomRight << QPoint(1279, 1023);
    QTest::newRow("Bottom") << KWin::ElectricBottom << QPoint(512, 1023);
    QTest::newRow("BottomLeft") << KWin::ElectricBottomLeft << QPoint(0, 1023);
    QTest::newRow("Left") << KWin::ElectricLeft << QPoint(0, 512);
    QTest::newRow("TopLeft") << KWin::ElectricTopLeft << QPoint(0, 0);

    // repeat a row to show previously unloading and re-registering works
    QTest::newRow("Top") << KWin::ElectricTop << QPoint(512, 0);
}

void ScreenEdgeTest::testEdge()
{
    const QString scriptToLoad = QFINDTESTDATA("./scripts/screenedge.js");
    QVERIFY(!scriptToLoad.isEmpty());

    // mock the config
    auto config = kwinApp()->config();
    QFETCH(KWin::ElectricBorder, edge);
    config->group(QLatin1String("Script-") + scriptToLoad).writeEntry("Edge", int(edge));
    config->sync();

    QVERIFY(!Test::app()->base.space->scripting->isScriptLoaded(scriptToLoad));
    const int id = Test::app()->base.space->scripting->loadScript(scriptToLoad);
    QVERIFY(id != -1);
    QVERIFY(Test::app()->base.space->scripting->isScriptLoaded(scriptToLoad));
    auto s = Test::app()->base.space->scripting->findScript(scriptToLoad);
    QVERIFY(s);
    QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
    QVERIFY(runningChangedSpy.isValid());
    s->run();
    QVERIFY(runningChangedSpy.wait());
    QCOMPARE(runningChangedSpy.count(), 1);
    QCOMPARE(runningChangedSpy.first().first().toBool(), true);

    // triggering the edge will result in show desktop being triggered
    QSignalSpy showDesktopSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::showingDesktopChanged);
    QVERIFY(showDesktopSpy.isValid());

    // trigger the edge
    QFETCH(QPoint, triggerPos);
    Test::cursor()->set_pos(triggerPos);
    QCOMPARE(showDesktopSpy.count(), 1);
    QVERIFY(Test::app()->base.space->showing_desktop);
}

void ScreenEdgeTest::testTouchEdge_data()
{
    QTest::addColumn<KWin::ElectricBorder>("edge");
    QTest::addColumn<QPoint>("triggerPos");
    QTest::addColumn<QPoint>("motionPos");

    QTest::newRow("Top") << KWin::ElectricTop << QPoint(50, 0) << QPoint(50, 500);
    QTest::newRow("Right") << KWin::ElectricRight << QPoint(1279, 50) << QPoint(500, 50);
    QTest::newRow("Bottom") << KWin::ElectricBottom << QPoint(512, 1023) << QPoint(512, 500);
    QTest::newRow("Left") << KWin::ElectricLeft << QPoint(0, 50) << QPoint(500, 50);

    // repeat a row to show previously unloading and re-registering works
    QTest::newRow("Top") << KWin::ElectricTop << QPoint(512, 0) << QPoint(512, 500);
}

void ScreenEdgeTest::testTouchEdge()
{
    const QString scriptToLoad = QFINDTESTDATA("./scripts/touchScreenedge.js");
    QVERIFY(!scriptToLoad.isEmpty());

    // mock the config
    auto config = kwinApp()->config();
    QFETCH(KWin::ElectricBorder, edge);
    config->group(QLatin1String("Script-") + scriptToLoad).writeEntry("Edge", int(edge));
    config->sync();

    QVERIFY(!Test::app()->base.space->scripting->isScriptLoaded(scriptToLoad));
    auto const id = Test::app()->base.space->scripting->loadScript(scriptToLoad);
    QVERIFY(id != -1);
    QVERIFY(Test::app()->base.space->scripting->isScriptLoaded(scriptToLoad));
    auto s = Test::app()->base.space->scripting->findScript(scriptToLoad);
    QVERIFY(s);
    QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
    QVERIFY(runningChangedSpy.isValid());
    s->run();
    QVERIFY(runningChangedSpy.wait());
    QCOMPARE(runningChangedSpy.count(), 1);
    QCOMPARE(runningChangedSpy.first().first().toBool(), true);
    // triggering the edge will result in show desktop being triggered
    QSignalSpy showDesktopSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::showingDesktopChanged);
    QVERIFY(showDesktopSpy.isValid());

    // trigger the edge
    QFETCH(QPoint, triggerPos);
    quint32 timestamp = 0;
    Test::touch_down(0, triggerPos, timestamp++);
    QFETCH(QPoint, motionPos);
    Test::touch_motion(0, motionPos, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(showDesktopSpy.wait());
    QCOMPARE(showDesktopSpy.count(), 1);
    QVERIFY(Test::app()->base.space->showing_desktop);
}

void ScreenEdgeTest::triggerConfigReload()
{
    win::space_reconfigure(*Test::app()->base.space);
}

void ScreenEdgeTest::testEdgeUnregister()
{
    const QString scriptToLoad = QFINDTESTDATA("./scripts/screenedgeunregister.js");
    QVERIFY(!scriptToLoad.isEmpty());

    Test::app()->base.space->scripting->loadScript(scriptToLoad);
    auto s = Test::app()->base.space->scripting->findScript(scriptToLoad);
    auto configGroup = s->config();
    configGroup.writeEntry("Edge", int(KWin::ElectricLeft));
    configGroup.sync();
    const QPoint triggerPos = QPoint(0, 512);

    QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
    s->run();
    QVERIFY(runningChangedSpy.wait());

    QSignalSpy showDesktopSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::showingDesktopChanged);
    QVERIFY(showDesktopSpy.isValid());

    // trigger the edge
    Test::cursor()->set_pos(triggerPos);
    QCOMPARE(showDesktopSpy.count(), 1);

    // reset
    Test::cursor()->set_pos(500, 500);
    win::toggle_show_desktop(*Test::app()->base.space);
    showDesktopSpy.clear();

    // trigger again, to show that retriggering works
    Test::cursor()->set_pos(triggerPos);
    QCOMPARE(showDesktopSpy.count(), 1);

    // reset
    Test::cursor()->set_pos(500, 500);
    win::toggle_show_desktop(*Test::app()->base.space);
    showDesktopSpy.clear();

    // make the script unregister the edge
    configGroup.writeEntry("mode", "unregister");
    triggerConfigReload();
    Test::cursor()->set_pos(triggerPos);
    QCOMPARE(showDesktopSpy.count(), 0); // not triggered

    // force the script to unregister a non-registered edge to prove it doesn't explode
    triggerConfigReload();
}

void ScreenEdgeTest::testDeclarativeTouchEdge()
{
    const QString scriptToLoad = QFINDTESTDATA("./scripts/screenedgetouch.qml");
    QVERIFY(!scriptToLoad.isEmpty());
    QVERIFY(Test::app()->base.space->scripting->loadDeclarativeScript(scriptToLoad) != -1);
    QVERIFY(Test::app()->base.space->scripting->isScriptLoaded(scriptToLoad));

    auto s = Test::app()->base.space->scripting->findScript(scriptToLoad);
    QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
    s->run();
    QTRY_COMPARE(runningChangedSpy.count(), 1);

    QSignalSpy showDesktopSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::showingDesktopChanged);
    QVERIFY(showDesktopSpy.isValid());

    // Trigger the edge through touch
    quint32 timestamp = 0;
    Test::touch_down(0, QPointF(0, 50), timestamp++);
    Test::touch_motion(0, QPointF(500, 50), timestamp++);
    Test::touch_up(0, timestamp++);

    QVERIFY(showDesktopSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::ScreenEdgeTest)
#include "screenedge_test.moc"
