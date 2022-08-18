/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2017 Roman Gilg <subdiff@gmail.com>

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
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

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

    KConfigGroup cfgGroup = kwinApp()->config()->group("NightColor");

    cfgGroup.writeEntry("Active", activeDefault);
    cfgGroup.writeEntry("Mode", modeDefault);

    kwinApp()->config()->sync();
    auto& manager = Test::app()->base.render->night_color;
    manager->reconfigure();

    QCOMPARE(manager->is_enabled(), activeDefault);
    QCOMPARE(manager->mode(), modeDefault);

    cfgGroup.writeEntry("Active", active);
    cfgGroup.writeEntry("Mode", mode);
    kwinApp()->config()->sync();

    manager->reconfigure();

    QCOMPARE(manager->is_enabled(), active);
    if (mode > 3 || mode < 0) {
        QCOMPARE(manager->mode(), 0);
    } else {
        QCOMPARE(manager->mode(), mode);
    }
}

}

WAYLANDTEST_MAIN(KWin::ColorCorrectNightColorTest)
#include "colorcorrect_nightcolor_test.moc"
