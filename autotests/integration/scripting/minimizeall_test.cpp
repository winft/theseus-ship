/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "scripting/platform.h"
#include "scripting/script.h"
#include "win/control.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <KPackage/PackageLoader>
#include <Wrapland/Client/surface.h>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

namespace KWin::detail::test
{

namespace
{

static const QString s_scriptName = QStringLiteral("minimizeall");

static QString locateMainScript(const QString& pluginName)
{
    const QList<KPluginMetaData> offers = KPackage::PackageLoader::self()->findPackages(
        QStringLiteral("KWin/Script"),
        QStringLiteral("kwin/scripts"),
        [&](const KPluginMetaData& metaData) { return metaData.pluginId() == pluginName; });
    if (offers.isEmpty()) {
        return QString();
    }
    auto const& metaData = offers.first();
    QString const mainScriptFileName = metaData.value(QStringLiteral("X-Plasma-MainScript"));
    QFileInfo const metaDataFileInfo(metaData.fileName());
    return metaDataFileInfo.path() + QLatin1String("/contents/") + mainScriptFileName;
}

}

TEST_CASE("minimize all", "[script]")
{
    // This test verifies that all windows are minimized when Meta+Shift+D
    // is pressed, and unminimized when the shortcut is pressed once again.
    using namespace Wrapland::Client;

    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("minimize-all", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection();

    setup.base->space->scripting->loadScript(locateMainScript(s_scriptName), s_scriptName);
    QTRY_VERIFY(setup.base->space->scripting->isScriptLoaded(s_scriptName));

    auto script = setup.base->space->scripting->findScript(s_scriptName);
    QVERIFY(script);
    QSignalSpy runningChangedSpy(script, &scripting::abstract_script::runningChanged);
    QVERIFY(runningChangedSpy.isValid());
    script->run();
    QTRY_COMPARE(runningChangedSpy.count(), 1);

    // Create a couple of test clients.
    std::unique_ptr<Surface> surface1(create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(create_xdg_shell_toplevel(surface1));
    auto client1 = render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(client1);
    QVERIFY(client1->control->active);
    QVERIFY(client1->isMinimizable());

    std::unique_ptr<Surface> surface2(create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
    auto client2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
    QVERIFY(client2);
    QVERIFY(client2->control->active);
    QVERIFY(client2->isMinimizable());

    // Minimize the windows.
    quint32 timestamp = 1;
    keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    keyboard_key_pressed(KEY_D, timestamp++);
    keyboard_key_released(KEY_D, timestamp++);
    keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    keyboard_key_released(KEY_LEFTMETA, timestamp++);

    QTRY_VERIFY(client1->control->minimized);
    QTRY_VERIFY(client2->control->minimized);

    // Unminimize the windows.
    keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
    keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    keyboard_key_pressed(KEY_D, timestamp++);
    keyboard_key_released(KEY_D, timestamp++);
    keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    keyboard_key_released(KEY_LEFTMETA, timestamp++);

    QTRY_VERIFY(!client1->control->minimized);
    QTRY_VERIFY(!client2->control->minimized);

    // Destroy test clients.
    shellSurface2.reset();
    QVERIFY(wait_for_destroyed(client2));
    shellSurface1.reset();
    QVERIFY(wait_for_destroyed(client1));
}

}
