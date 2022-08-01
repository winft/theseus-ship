/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2017 Martin Gräßlin <mgraesslin@kde.org>

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
#include <QTest>

// Included here because later includes pull in XLib which collides with some defintions in QtCore.
#include <QtCore>

#include <QX11Info>

#include <KPluginMetaData>

#include <memory>

#include "base/backend/x11/platform.h"
#include "base/x11/grabs.h"
#include "debug/console/x11/x11_console.h"
#include "main.h"
#include "render/backend/x11/platform.h"
#include "render/x11/compositor.h"
#include "win/x11/space.h"

namespace KWin
{

class X11TestApplication : public Application
{
    Q_OBJECT
public:
    X11TestApplication(int& argc, char** argv);
    ~X11TestApplication() override;

    base::platform& get_base() override;
    debug::console* create_debug_console() override;

    void start();

    base::backend::x11::platform base;
    std::unique_ptr<render::x11::compositor> compositor;
    std::unique_ptr<win::x11::space<base::x11::platform>> workspace;
};

X11TestApplication::X11TestApplication(int& argc, char** argv)
    : Application(OperationModeX11, argc, argv)
{
    setX11Connection(QX11Info::connection());
    setX11RootWindow(QX11Info::appRootWindow());

    // move directory containing executable to front, so that KPluginMetaData::findPluginById
    // prefers the plugins in the build dir over system installed ones
    const auto ownPath = libraryPaths().constLast();
    removeLibraryPath(ownPath);
    addLibraryPath(ownPath);

    base.render = std::make_unique<render::backend::x11::platform>(base);
}

X11TestApplication::~X11TestApplication()
{
}

base::platform& X11TestApplication::get_base()
{
    return base;
}

debug::console* X11TestApplication::create_debug_console()
{
    return new debug::x11_console(*workspace);
}

void X11TestApplication::start()
{
    prepare_start();
    compositor = std::make_unique<render::x11::compositor>(*base.render);
    workspace = std::make_unique<win::x11::space<base::x11::platform>>(base);
    compositor->start(*workspace);
}

}

class X11TimestampUpdateTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testGrabAfterServerTime();
    void testBeforeLastGrabTime();
};

void X11TimestampUpdateTest::testGrabAfterServerTime()
{
    // this test tries to grab the X keyboard with a timestamp in future
    // that should fail, but after updating the X11 timestamp, it should
    // work again
    KWin::kwinApp()->update_x11_time_from_clock();
    QCOMPARE(KWin::base::x11::grab_keyboard(), true);
    KWin::base::x11::ungrab_keyboard();

    // now let's change the timestamp
    KWin::kwinApp()->setX11Time(KWin::xTime() + 5 * 60 * 1000);

    // now grab keyboard should fail
    QCOMPARE(KWin::base::x11::grab_keyboard(), false);

    // let's update timestamp, now it should work again
    KWin::kwinApp()->update_x11_time_from_clock();
    QCOMPARE(KWin::base::x11::grab_keyboard(), true);
    KWin::base::x11::ungrab_keyboard();
}

void X11TimestampUpdateTest::testBeforeLastGrabTime()
{
    // this test tries to grab the X keyboard with a timestamp before the
    // last grab time on the server. That should fail, but after updating the X11
    // timestamp it should work again

    // first set the grab timestamp
    KWin::kwinApp()->update_x11_time_from_clock();
    QCOMPARE(KWin::base::x11::grab_keyboard(), true);
    KWin::base::x11::ungrab_keyboard();

    // now go to past
    const auto timestamp = KWin::xTime();
    KWin::kwinApp()->setX11Time(KWin::xTime() - 5 * 60 * 1000,
                                KWin::Application::TimestampUpdate::Always);
    QCOMPARE(KWin::xTime(), timestamp - 5 * 60 * 1000);

    // now grab keyboard should fail
    QCOMPARE(KWin::base::x11::grab_keyboard(), false);

    // let's update timestamp, now it should work again
    KWin::kwinApp()->update_x11_time_from_clock();
    QVERIFY(KWin::xTime() >= timestamp);
    QCOMPARE(KWin::base::x11::grab_keyboard(), true);
    KWin::base::x11::ungrab_keyboard();
}

int main(int argc, char* argv[])
{
    setenv("QT_QPA_PLATFORM", "xcb", true);
    KWin::X11TestApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    X11TimestampUpdateTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_x11_timestamp_update.moc"
