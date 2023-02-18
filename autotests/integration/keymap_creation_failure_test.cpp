/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/layout_manager.h"
#include "win/space.h"
#include "win/virtual_desktops.h"

#include <KConfigGroup>
#include <KGlobalAccel>

#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin
{

class KeymapCreationFailureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testPointerButton();
};

void KeymapCreationFailureTest::initTestCase()
{
    // situation for for BUG 381210
    // this will fail to create keymap
    qputenv("XKB_DEFAULT_RULES", "no");
    qputenv("XKB_DEFAULT_MODEL", "no");
    qputenv("XKB_DEFAULT_LAYOUT", "no");
    qputenv("XKB_DEFAULT_VARIANT", "no");
    qputenv("XKB_DEFAULT_OPTIONS", "no");

    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());

    Test::app()->base->input->xkb.setConfig(KSharedConfig::openConfig({}, KConfig::SimpleConfig));
    auto layoutGroup = Test::app()->base->input->config.xkb->group("Layout");
    layoutGroup.writeEntry("LayoutList", QStringLiteral("no"));
    layoutGroup.writeEntry("Model", "no");
    layoutGroup.writeEntry("Options", "no");
    layoutGroup.sync();
}

void KeymapCreationFailureTest::init()
{
    Test::setup_wayland_connection();
}

void KeymapCreationFailureTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void KeymapCreationFailureTest::testPointerButton()
{
    // test case for BUG 381210
    // pressing a pointer button results in crash

    // now create the crashing condition
    // which is sending in a pointer event
    Test::pointer_button_pressed(BTN_LEFT, 0);
    Test::pointer_button_released(BTN_LEFT, 1);
}

}

WAYLANDTEST_MAIN(KWin::KeymapCreationFailureTest)
#include "keymap_creation_failure_test.moc"
