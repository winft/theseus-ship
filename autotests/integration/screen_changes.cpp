/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"

#include <Wrapland/Client/output.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/xdgoutput.h>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("screen changes", "[base]")
{
    // this test verifies that when a new screen is added it gets synced to Wayland

#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("screen-changes", operation_mode);
    setup.start();
    setup_wayland_connection();
    cursor()->set_pos(QPoint(640, 512));

    // first create a registry to get signals about Outputs announced/removed
    Registry registry;
    QSignalSpy allAnnounced(&registry, &Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());
    QSignalSpy outputAnnouncedSpy(&registry, &Registry::outputAnnounced);
    QVERIFY(outputAnnouncedSpy.isValid());
    QSignalSpy outputRemovedSpy(&registry, &Registry::outputRemoved);
    QVERIFY(outputRemovedSpy.isValid());
    registry.create(get_client().connection);
    QVERIFY(registry.isValid());
    registry.setup();
    QVERIFY(allAnnounced.wait());
    const auto xdgOMData = registry.interface(Registry::Interface::XdgOutputUnstableV1);
    auto xdgOutputManager = registry.createXdgOutputManager(xdgOMData.name, xdgOMData.version);

    // should be one output
    QCOMPARE(setup.base->outputs.size(), 1);
    QCOMPARE(outputAnnouncedSpy.count(), 1);
    const quint32 firstOutputId = outputAnnouncedSpy.first().first().value<quint32>();
    QVERIFY(firstOutputId != 0u);
    outputAnnouncedSpy.clear();

    // let's announce a new output
    QSignalSpy outputs_changed_spy(setup.base.get(), &base::platform::topology_changed);
    QVERIFY(outputs_changed_spy.isValid());

    auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {1280, 0, 1280, 1024}};
    setup.set_outputs(geometries);

    QCOMPARE(outputs_changed_spy.count(), 1);
    test_outputs_geometries(geometries);

    // this should result in it getting announced, two new outputs are added...
    TRY_REQUIRE(outputAnnouncedSpy.size() == 2);

    // ... and afterward the previous output gets removed
    TRY_REQUIRE(outputRemovedSpy.size() == 1);
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
    outputs_changed_spy.clear();

    QSignalSpy o1RemovedSpy(o1.get(), &Output::removed);
    QVERIFY(o1RemovedSpy.isValid());
    QSignalSpy o2RemovedSpy(o2.get(), &Output::removed);
    QVERIFY(o2RemovedSpy.isValid());

    auto const geometries2 = std::vector<QRect>{{0, 0, 1280, 1024}};
    setup.set_outputs(geometries2);

    QCOMPARE(outputs_changed_spy.count(), 1);
    test_outputs_geometries(geometries2);

    TRY_REQUIRE(outputAnnouncedSpy.size() == 1);
    TRY_REQUIRE(o1RemovedSpy.size() == 1);
    TRY_REQUIRE(o2RemovedSpy.size() == 1);
    TRY_REQUIRE(outputRemovedSpy.size() == 2);
}

}
