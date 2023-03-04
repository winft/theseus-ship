/*
SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <QTest>

// Included here because later includes pull in XLib which collides with some defintions in QtCore.
#include <QtCore>

#include <QX11Info>

#include <KPluginMetaData>

#include <memory>

#include "base/backend/x11/platform.h"
#include "base/seat/backend/logind/session.h"
#include "base/x11/grabs.h"
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

    void start();

    using base_t = base::backend::x11::platform;
    base_t base;
};

X11TestApplication::X11TestApplication(int& argc, char** argv)
    : Application(argc, argv)
    , base{base::config(KConfig::OpenFlag::SimpleConfig)}
{
    base.x11_data.connection = QX11Info::connection();
    base.x11_data.root_window = QX11Info::appRootWindow();

    // move directory containing executable to front, so that KPluginMetaData::findPluginById
    // prefers the plugins in the build dir over system installed ones
    const auto ownPath = libraryPaths().constLast();
    removeLibraryPath(ownPath);
    addLibraryPath(ownPath);

    base.session = std::make_unique<base::seat::backend::logind::session>();
    base.render = std::make_unique<render::backend::x11::platform<base::x11::platform>>(base);
}

X11TestApplication::~X11TestApplication()
{
}

void X11TestApplication::start()
{
    prepare_start();
    base.render->compositor = std::make_unique<base_t::render_t::compositor_t>(*base.render);
    base.space = std::make_unique<base_t::space_t>(base);
    base.render->compositor->start(*base.space);
}

}

static KWin::X11TestApplication* s_app{nullptr};

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
    KWin::base::x11::update_time_from_clock(s_app->base);
    QCOMPARE(KWin::base::x11::grab_keyboard(s_app->base.x11_data), true);
    KWin::base::x11::ungrab_keyboard(s_app->base.x11_data.connection);

    // now let's change the timestamp
    KWin::base::x11::advance_time(s_app->base.x11_data, s_app->base.x11_data.time + 5 * 60 * 1000);

    // now grab keyboard should fail
    QCOMPARE(KWin::base::x11::grab_keyboard(s_app->base.x11_data), false);

    // let's update timestamp, now it should work again
    KWin::base::x11::update_time_from_clock(s_app->base);
    QCOMPARE(KWin::base::x11::grab_keyboard(s_app->base.x11_data), true);
    KWin::base::x11::ungrab_keyboard(s_app->base.x11_data.connection);
}

void X11TimestampUpdateTest::testBeforeLastGrabTime()
{
    // this test tries to grab the X keyboard with a timestamp before the
    // last grab time on the server. That should fail, but after updating the X11
    // timestamp it should work again

    // first set the grab timestamp
    KWin::base::x11::update_time_from_clock(s_app->base);
    QCOMPARE(KWin::base::x11::grab_keyboard(s_app->base.x11_data), true);
    KWin::base::x11::ungrab_keyboard(s_app->base.x11_data.connection);

    // now go to past
    auto const timestamp = s_app->base.x11_data.time;
    KWin::base::x11::set_time(s_app->base.x11_data, s_app->base.x11_data.time - 5 * 60 * 1000);
    QCOMPARE(s_app->base.x11_data.time, timestamp - 5 * 60 * 1000);

    // now grab keyboard should fail
    QCOMPARE(KWin::base::x11::grab_keyboard(s_app->base.x11_data), false);

    // let's update timestamp, now it should work again
    KWin::base::x11::update_time_from_clock(s_app->base);
    QVERIFY(s_app->base.x11_data.time >= timestamp);
    QCOMPARE(KWin::base::x11::grab_keyboard(s_app->base.x11_data), true);
    KWin::base::x11::ungrab_keyboard(s_app->base.x11_data.connection);
}

int main(int argc, char* argv[])
{
    setenv("QT_QPA_PLATFORM", "xcb", true);
    KWin::X11TestApplication app(argc, argv);
    s_app = &app;
    app.setAttribute(Qt::AA_Use96Dpi, true);
    X11TimestampUpdateTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_x11_timestamp_update.moc"
