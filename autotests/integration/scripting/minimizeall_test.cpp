/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#include "screens.h"
#include "scripting/platform.h"
#include "scripting/script.h"
#include "win/control.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <KPackage/PackageLoader>
#include <Wrapland/Client/surface.h>

#include <linux/input.h>

namespace KWin
{

static const QString s_scriptName = QStringLiteral("minimizeall");

class MinimizeAllScriptTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMinimizeUnminimize();
};

void MinimizeAllScriptTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    Test::test_outputs_default();
}

static QString locateMainScript(const QString& pluginName)
{
    const QList<KPluginMetaData> offers = KPackage::PackageLoader::self()->findPackages(
        QStringLiteral("KWin/Script"),
        QStringLiteral("kwin/scripts"),
        [&](const KPluginMetaData& metaData) { return metaData.pluginId() == pluginName; });
    if (offers.isEmpty()) {
        return QString();
    }
    const KPluginMetaData& metaData = offers.first();
    const QString mainScriptFileName = metaData.value(QStringLiteral("X-Plasma-MainScript"));
    const QFileInfo metaDataFileInfo(metaData.fileName());
    return metaDataFileInfo.path() + QLatin1String("/contents/") + mainScriptFileName;
}

void MinimizeAllScriptTest::init()
{
    Test::setup_wayland_connection();

    workspace()->scripting->loadScript(locateMainScript(s_scriptName), s_scriptName);
    QTRY_VERIFY(workspace()->scripting->isScriptLoaded(s_scriptName));

    auto script = workspace()->scripting->findScript(s_scriptName);
    QVERIFY(script);
    QSignalSpy runningChangedSpy(script, &scripting::abstract_script::runningChanged);
    QVERIFY(runningChangedSpy.isValid());
    script->run();
    QTRY_COMPARE(runningChangedSpy.count(), 1);
}

void MinimizeAllScriptTest::cleanup()
{
    Test::destroy_wayland_connection();

    workspace()->scripting->unloadScript(s_scriptName);
    QTRY_VERIFY(!workspace()->scripting->isScriptLoaded(s_scriptName));
}

void MinimizeAllScriptTest::testMinimizeUnminimize()
{
    // This test verifies that all windows are minimized when Meta+Shift+D
    // is pressed, and unminimized when the shortcut is pressed once again.

    using namespace Wrapland::Client;

    // Create a couple of test clients.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active());
    QVERIFY(client1->isMinimizable());

    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
    QVERIFY(client2);
    QVERIFY(client2->control->active());
    QVERIFY(client2->isMinimizable());

    // Minimize the windows.
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_pressed(KEY_D, timestamp++);
    Test::keyboard_key_released(KEY_D, timestamp++);
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);

    QTRY_VERIFY(client1->control->minimized());
    QTRY_VERIFY(client2->control->minimized());

    // Unminimize the windows.
    Test::keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_pressed(KEY_D, timestamp++);
    Test::keyboard_key_released(KEY_D, timestamp++);
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    Test::keyboard_key_released(KEY_LEFTMETA, timestamp++);

    QTRY_VERIFY(!client1->control->minimized());
    QTRY_VERIFY(!client2->control->minimized());

    // Destroy test clients.
    shellSurface2.reset();
    QVERIFY(Test::wait_for_destroyed(client2));
    shellSurface1.reset();
    QVERIFY(Test::wait_for_destroyed(client1));
}

}

WAYLANDTEST_MAIN(KWin::MinimizeAllScriptTest)
#include "minimizeall_test.moc"
