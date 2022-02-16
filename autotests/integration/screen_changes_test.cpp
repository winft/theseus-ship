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
#include "screens.h"

#include <Wrapland/Client/output.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/xdgoutput.h>

using namespace Wrapland::Client;

namespace KWin
{

class ScreenChangesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testScreenAddRemove();
};

void ScreenChangesTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void ScreenChangesTest::init()
{
    Test::setup_wayland_connection();
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void ScreenChangesTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void ScreenChangesTest::testScreenAddRemove()
{
    // this test verifies that when a new screen is added it gets synced to Wayland

    // first create a registry to get signals about Outputs announced/removed
    Registry registry;
    QSignalSpy allAnnounced(&registry, &Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());
    QSignalSpy outputAnnouncedSpy(&registry, &Registry::outputAnnounced);
    QVERIFY(outputAnnouncedSpy.isValid());
    QSignalSpy outputRemovedSpy(&registry, &Registry::outputRemoved);
    QVERIFY(outputRemovedSpy.isValid());
    registry.create(Test::get_client().connection);
    QVERIFY(registry.isValid());
    registry.setup();
    QVERIFY(allAnnounced.wait());
    const auto xdgOMData = registry.interface(Registry::Interface::XdgOutputUnstableV1);
    auto xdgOutputManager = registry.createXdgOutputManager(xdgOMData.name, xdgOMData.version);

    // should be one output
    QCOMPARE(Test::app()->base.get_outputs().size(), 1);
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    const quint32 firstOutputId = outputAnnouncedSpy.first().first().value<quint32>();
    QVERIFY(firstOutputId != 0u);
    outputAnnouncedSpy.clear();

    // let's announce a new output
    QSignalSpy screensChangedSpy(&Test::app()->base.screens, &Screens::changed);
    QVERIFY(screensChangedSpy.isValid());

    auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {1280, 0, 1280, 1024}};
    Test::app()->set_outputs(geometries);

    QCOMPARE(screensChangedSpy.count(), Test::app()->base.get_outputs().size() + 2);
    Test::test_outputs_geometries(geometries);

    // this should result in it getting announced, two new outputs are added...
    QVERIFY(outputAnnouncedSpy.count() > 1 || outputAnnouncedSpy.wait());
    QTRY_COMPARE(outputAnnouncedSpy.count(), 2);

    // ... and afterward the previous output gets removed
    if (outputRemovedSpy.isEmpty()) {
        QVERIFY(outputRemovedSpy.wait());
    }

    QCOMPARE(outputRemovedSpy.count(), 1);
    QCOMPARE(outputRemovedSpy.first().first().value<quint32>(), firstOutputId);

    // let's wait a little bit to ensure we don't get more events
    QTest::qWait(100);
    QCOMPARE(outputAnnouncedSpy.count(), 2);
    QCOMPARE(outputRemovedSpy.count(), 1);

    // let's create the output objects to ensure they are correct
    std::unique_ptr<Output> o1(
        registry.createOutput(outputAnnouncedSpy.first().first().value<quint32>(),
                              outputAnnouncedSpy.first().last().value<quint32>()));
    QVERIFY(o1->isValid());
    QSignalSpy o1ChangedSpy(o1.get(), &Output::changed);
    QVERIFY(o1ChangedSpy.isValid());
    QVERIFY(o1ChangedSpy.wait());
    QCOMPARE(o1->geometry(), geometries.at(0));
    std::unique_ptr<Output> o2(
        registry.createOutput(outputAnnouncedSpy.last().first().value<quint32>(),
                              outputAnnouncedSpy.last().last().value<quint32>()));
    QVERIFY(o2->isValid());
    QSignalSpy o2ChangedSpy(o2.get(), &Output::changed);
    QVERIFY(o2ChangedSpy.isValid());
    QVERIFY(o2ChangedSpy.wait());
    QCOMPARE(o2->geometry(), geometries.at(1));

    // and check XDGOutput is synced
    std::unique_ptr<XdgOutput> xdgO1(xdgOutputManager->getXdgOutput(o1.get()));
    QSignalSpy xdgO1ChangedSpy(xdgO1.get(), &XdgOutput::changed);
    QVERIFY(xdgO1ChangedSpy.isValid());
    QVERIFY(xdgO1ChangedSpy.wait());
    QCOMPARE(xdgO1->logicalPosition(), geometries.at(0).topLeft());
    QCOMPARE(xdgO1->logicalSize(), geometries.at(0).size());
    std::unique_ptr<XdgOutput> xdgO2(xdgOutputManager->getXdgOutput(o2.get()));
    QSignalSpy xdgO2ChangedSpy(xdgO2.get(), &XdgOutput::changed);
    QVERIFY(xdgO2ChangedSpy.isValid());
    QVERIFY(xdgO2ChangedSpy.wait());
    QCOMPARE(xdgO2->logicalPosition(), geometries.at(1).topLeft());
    QCOMPARE(xdgO2->logicalSize(), geometries.at(1).size());

    // now let's try to remove one output again
    outputAnnouncedSpy.clear();
    outputRemovedSpy.clear();
    screensChangedSpy.clear();

    QSignalSpy o1RemovedSpy(o1.get(), &Output::removed);
    QVERIFY(o1RemovedSpy.isValid());
    QSignalSpy o2RemovedSpy(o2.get(), &Output::removed);
    QVERIFY(o2RemovedSpy.isValid());

    auto const geometries2 = std::vector<QRect>{{0, 0, 1280, 1024}};
    Test::app()->set_outputs(geometries2);

    QCOMPARE(screensChangedSpy.count(), Test::app()->base.get_outputs().size() + 3);
    Test::test_outputs_geometries(geometries2);

    QVERIFY(outputAnnouncedSpy.count() > 0 || outputAnnouncedSpy.wait());
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    if (o1RemovedSpy.isEmpty()) {
        QVERIFY(o1RemovedSpy.wait());
    }
    if (o2RemovedSpy.isEmpty()) {
        QVERIFY(o2RemovedSpy.wait());
    }
    // now wait a bit to ensure we don't get more events
    QTest::qWait(100);
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    QCOMPARE(o1RemovedSpy.count(), 1);
    QCOMPARE(o2RemovedSpy.count(), 1);
    QCOMPARE(outputRemovedSpy.count(), 2);
}

}

WAYLANDTEST_MAIN(KWin::ScreenChangesTest)
#include "screen_changes_test.moc"
