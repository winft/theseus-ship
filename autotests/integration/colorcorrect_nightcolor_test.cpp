/*
SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "render/platform.h"
#include "render/post/constants.h"
#include "render/post/night_color_manager.h"

#include <KConfigGroup>

namespace KWin
{

class ColorCorrectNightColorTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testConfigRead_data();
    void testConfigRead();
};

void ColorCorrectNightColorTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void ColorCorrectNightColorTest::init()
{
    Test::setup_wayland_connection();
}

void ColorCorrectNightColorTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void ColorCorrectNightColorTest::testConfigRead_data()
{
    QTest::addColumn<bool>("active");
    QTest::addColumn<int>("mode");

    QTest::newRow("activeMode0") << true << 0;
    QTest::newRow("activeMode1") << true << 1;
    QTest::newRow("activeMode2") << true << 3;
    QTest::newRow("notActiveMode2") << false << 2;
    QTest::newRow("wrongData1") << false << 4;
}

void ColorCorrectNightColorTest::testConfigRead()
{
    QFETCH(bool, active);
    QFETCH(int, mode);

    const bool activeDefault = true;
    const int modeDefault = 0;

    auto cfgGroup = Test::app()->base->config.main->group("NightColor");

    cfgGroup.writeEntry("Active", activeDefault);
    cfgGroup.writeEntry("Mode", modeDefault);

    cfgGroup.sync();
    auto& manager = Test::app()->base->render->night_color;
    manager->reconfigure();

    QCOMPARE(manager->data.enabled, activeDefault);
    QCOMPARE(manager->data.mode, modeDefault);

    cfgGroup.writeEntry("Active", active);
    cfgGroup.writeEntry("Mode", mode);
    cfgGroup.sync();

    manager->reconfigure();

    QCOMPARE(manager->data.enabled, active);
    if (mode > 3 || mode < 0) {
        QCOMPARE(manager->data.mode, 0);
    } else {
        QCOMPARE(manager->data.mode, mode);
    }
}

}

WAYLANDTEST_MAIN(KWin::ColorCorrectNightColorTest)
#include "colorcorrect_nightcolor_test.moc"
