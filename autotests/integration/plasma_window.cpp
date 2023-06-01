/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/geo.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/plasmawindowmanagement.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Server/keyboard_pool.h>
#include <Wrapland/Server/seat.h>

#include <KScreenLocker/KsldApp>

#include <QPainter>
#include <QRasterWindow>
#include <xcb/xcb_icccm.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

namespace
{

struct wayland_test_window {
    wayland_test_window(test::setup& /*setup*/, QSize const& size, QColor const& color)
    {
        client.surface = create_surface();
        client.toplevel = create_xdg_shell_toplevel(client.surface);

        server.window = render_and_wait_for_shown(client.surface, size, color);
        QVERIFY(server.window);
        QVERIFY(server.window->control->active);

        QSignalSpy plasma_window_spy(get_client().interfaces.window_management.get(),
                                     &Wrapland::Client::PlasmaWindowManagement::windowCreated);
        QVERIFY(plasma_window_spy.isValid());
        QVERIFY(plasma_window_spy.wait());
        QCOMPARE(plasma_window_spy.size(), 1);

        client.plasma = plasma_window_spy.first().first().value<Wrapland::Client::PlasmaWindow*>();
        QVERIFY(client.plasma);
    }

    wayland_test_window(setup& setup)
        : wayland_test_window(setup, QSize(100, 50), Qt::blue)
    {
    }

    struct {
        std::unique_ptr<Wrapland::Client::Surface> surface;
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
        Wrapland::Client::PlasmaWindow* plasma{nullptr};
    } client;

    struct {
        wayland_window* window{nullptr};
    } server;
};

struct x11_test_window {
    x11_test_window(test::setup& setup, QSize const& size)
    {
        client.connection = xcb_connect(nullptr, nullptr);
        QVERIFY(!xcb_connection_has_error(client.connection));

        auto const geo = QRect({0, 0}, size);
        client.window = xcb_generate_id(client.connection);
        xcb_create_window(client.connection,
                          XCB_COPY_FROM_PARENT,
                          client.window,
                          setup.base->x11_data.root_window,
                          geo.x(),
                          geo.y(),
                          geo.width(),
                          geo.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);

        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, geo.x(), geo.y());
        xcb_icccm_size_hints_set_size(&hints, 1, geo.width(), geo.height());
        xcb_icccm_set_wm_normal_hints(client.connection, client.window, &hints);
        xcb_map_window(client.connection, client.window);
        xcb_flush(client.connection);

        QSignalSpy window_spy(setup.base->space->qobject.get(),
                              &win::space::qobject_t::clientAdded);
        QVERIFY(window_spy.isValid());
        QVERIFY(window_spy.wait());

        auto window_id = window_spy.first().first().value<quint32>();
        server.window = get_x11_window(setup.base->space->windows_map.at(window_id));
        QVERIFY(server.window);
        QCOMPARE(server.window->xcb_windows.client, client.window);
        QVERIFY(win::decoration(server.window));
        QVERIFY(server.window->control->active);

        if (!server.window->surface) {
            QVERIFY(!setup.base->server->seat()->keyboards().get_focus().surface);
            QSignalSpy surface_spy(server.window->qobject.get(),
                                   &win::window_qobject::surfaceChanged);
            QVERIFY(surface_spy.isValid());
            QVERIFY(surface_spy.wait());
        }
        QVERIFY(server.window->surface);

        QSignalSpy plasma_window_spy(get_client().interfaces.window_management.get(),
                                     &Wrapland::Client::PlasmaWindowManagement::windowCreated);
        QVERIFY(plasma_window_spy.isValid());
        QVERIFY(plasma_window_spy.wait());
        QCOMPARE(plasma_window_spy.size(), 1);

        client.plasma = plasma_window_spy.first().first().value<Wrapland::Client::PlasmaWindow*>();
        QVERIFY(client.plasma);
    }

    x11_test_window(test::setup& setup)
        : x11_test_window(setup, QSize(100, 50))
    {
    }

    x11_test_window(x11_test_window&& other) noexcept
    {
        *this = std::move(other);
    }

    x11_test_window& operator=(x11_test_window&& other) noexcept
    {
        client = other.client;
        server = other.server;

        other.client = {};
        other.server = {};
        return *this;
    }

    ~x11_test_window()
    {
        if (client.window) {
            assert(client.connection);
            xcb_destroy_window(client.connection, client.window);
        }
        if (client.connection) {
            xcb_disconnect(client.connection);
        }
    }

    struct {
        xcb_connection_t* connection{nullptr};
        xcb_window_t window{XCB_WINDOW_NONE};
        Wrapland::Client::PlasmaWindow* plasma{nullptr};
    } client;

    struct {
        win::wayland::xwl_window<space>* window{nullptr};
    } server;
};

class HelperWindow : public QRasterWindow
{
    Q_OBJECT
public:
    HelperWindow();
    ~HelperWindow() override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

HelperWindow::HelperWindow()
    : QRasterWindow(nullptr)
{
}

HelperWindow::~HelperWindow() = default;

void HelperWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), Qt::red);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_disconnect);
}

template<typename Win>
std::string get_internal_id(Win& win)
{
    return win.server.window->meta.internal_id.toString().toStdString();
}

}

TEST_CASE("plasma window", "[win]")
{
    test::setup setup("plasma-window", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    cursor()->set_pos(QPoint(640, 512));

    setenv("QMLSCENE_DEVICE", "softwarecontext", true);

    setup_wayland_connection(global_selection::window_management);
    auto window_management = get_client().interfaces.window_management.get();

    SECTION("create destroy x11 plasma window")
    {
        // this test verifies that a PlasmaWindow gets unmapped on Client side when an X11 client is
        // destroyed
        QSignalSpy plasmaWindowCreatedSpy(window_management,
                                          &PlasmaWindowManagement::windowCreated);
        QVERIFY(plasmaWindowCreatedSpy.isValid());

        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_icccm_set_wm_class(
            c.get(), w, 51, "org.kwinft.wm_class.name\0org.kwinft.wm_class.class");
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &win::space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.first().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(win::decoration(client));
        QVERIFY(client->control->active);
        // verify that it gets the keyboard focus
        if (!client->surface) {
            // we don't have a surface yet, so focused keyboard surface if set is not ours
            QVERIFY(!setup.base->server->seat()->keyboards().get_focus().surface);
            QSignalSpy surfaceChangedSpy(client->qobject.get(),
                                         &win::window_qobject::surfaceChanged);
            QVERIFY(surfaceChangedSpy.isValid());
            QVERIFY(surfaceChangedSpy.wait());
        }
        QVERIFY(client->surface);
        QCOMPARE(setup.base->server->seat()->keyboards().get_focus().surface, client->surface);

        // now that should also give it to us on client side
        QVERIFY(plasmaWindowCreatedSpy.wait());
        QCOMPARE(plasmaWindowCreatedSpy.count(), 1);
        QCOMPARE(window_management->windows().count(), 1);

        auto pw = window_management->windows().constFirst();
        QCOMPARE(pw->geometry(), client->geo.frame);
        QCOMPARE(pw->resource_name(), "org.kwinft.wm_class.name");

        QSignalSpy res_name_spy(pw, &Wrapland::Client::PlasmaWindow::resource_name_changed);
        QVERIFY(res_name_spy.isValid());

        xcb_icccm_set_wm_class(
            c.get(), w, 53, "org.kwinft.wm_class.name2\0org.kwinft.wm_class.class2");
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QVERIFY(res_name_spy.wait());
        QCOMPARE(pw->resource_name(), "org.kwinft.wm_class.name2");

        QSignalSpy unmappedSpy(window_management->windows().constFirst(), &PlasmaWindow::unmapped);
        QVERIFY(unmappedSpy.isValid());
        QSignalSpy destroyedSpy(window_management->windows().constFirst(), &QObject::destroyed);
        QVERIFY(destroyedSpy.isValid());

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
        xcb_destroy_window(c.get(), w);
        c.reset();

        TRY_REQUIRE(unmappedSpy.size() == 1);
        TRY_REQUIRE(destroyedSpy.size() == 1);
    }

    SECTION("internal window no plasma window")
    {
        // this test verifies that an internal window is not added as a PlasmaWindow to the client
        QSignalSpy plasmaWindowCreatedSpy(window_management,
                                          &PlasmaWindowManagement::windowCreated);
        QVERIFY(plasmaWindowCreatedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();

        QVERIFY(!plasmaWindowCreatedSpy.wait(500));
    }

    SECTION("popup window no plasma window")
    {
        // this test verifies that for a popup window no PlasmaWindow is sent to the client
        QSignalSpy plasmaWindowCreatedSpy(window_management,
                                          &PlasmaWindowManagement::windowCreated);
        QVERIFY(plasmaWindowCreatedSpy.isValid());

        // first create the parent window
        std::unique_ptr<Surface> parentSurface(create_surface());
        std::unique_ptr<XdgShellToplevel> parentShellSurface(
            create_xdg_shell_toplevel(parentSurface));
        auto parentClient = render_and_wait_for_shown(parentSurface, QSize(100, 50), Qt::blue);
        QVERIFY(parentClient);
        QVERIFY(plasmaWindowCreatedSpy.wait());
        QCOMPARE(plasmaWindowCreatedSpy.count(), 1);

        // now let's create a popup window for it
        Wrapland::Client::xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(10, 10);
        pos_data.anchor.rect = QRect(0, 0, 10, 10);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::RightEdge;
        pos_data.gravity = pos_data.anchor.edge;

        std::unique_ptr<Surface> popupSurface(create_surface());
        std::unique_ptr<XdgShellPopup> popupShellSurface(
            create_xdg_shell_popup(popupSurface, parentShellSurface, pos_data));
        auto popupClient = render_and_wait_for_shown(popupSurface, pos_data.size, Qt::blue);
        QVERIFY(popupClient);
        QVERIFY(!plasmaWindowCreatedSpy.wait(100));
        QCOMPARE(plasmaWindowCreatedSpy.count(), 1);

        // let's destroy the windows
        popupShellSurface.reset();
        QVERIFY(wait_for_destroyed(popupClient));
        parentShellSurface.reset();
        QVERIFY(wait_for_destroyed(parentClient));
    }

    SECTION("lockscreen no plasma window")
    {
        // this test verifies that lock screen windows are not exposed to PlasmaWindow
        QSignalSpy plasmaWindowCreatedSpy(window_management,
                                          &PlasmaWindowManagement::windowCreated);
        QVERIFY(plasmaWindowCreatedSpy.isValid());

        // this time we use a QSignalSpy on XdgShellClient as it'a a little bit more complex setup
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());

        // lock
        ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);

        // The lock screen creates one client per screen.
        auto outputs_count = setup.base->outputs.size();
        TRY_REQUIRE(clientAddedSpy.count() == static_cast<int>(outputs_count));

        QVERIFY(get_wayland_window(setup.base->space->windows_map.at(
                                       clientAddedSpy.first().first().value<quint32>()))
                    ->isLockScreen());

        // should not be sent to the client
        QVERIFY(plasmaWindowCreatedSpy.isEmpty());
        QVERIFY(!plasmaWindowCreatedSpy.wait(500));

        // fake unlock
        QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                       &ScreenLocker::KSldApp::lockStateChanged);
        QVERIFY(lockStateChangedSpy.isValid());
        const auto children = ScreenLocker::KSldApp::self()->children();
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (qstrcmp((*it)->metaObject()->className(), "LogindIntegration") != 0) {
                continue;
            }
            QMetaObject::invokeMethod(*it, "requestUnlock");
            break;
        }
        QVERIFY(lockStateChangedSpy.wait());
        QVERIFY(!base::wayland::is_screen_locked(setup.base));
    }

    SECTION("destroyed but not unmapped")
    {
        // this test verifies that also when a ShellSurface gets destroyed without a prior unmap
        // the PlasmaWindow gets destroyed on Client side
        QSignalSpy plasmaWindowCreatedSpy(window_management,
                                          &PlasmaWindowManagement::windowCreated);
        QVERIFY(plasmaWindowCreatedSpy.isValid());

        // first create the parent window
        std::unique_ptr<Surface> parentSurface(create_surface());
        std::unique_ptr<XdgShellToplevel> parentShellSurface(
            create_xdg_shell_toplevel(parentSurface));
        // map that window
        render(parentSurface, QSize(100, 50), Qt::blue);
        // this should create a plasma window
        QVERIFY(plasmaWindowCreatedSpy.wait());
        QCOMPARE(plasmaWindowCreatedSpy.count(), 1);
        auto window = plasmaWindowCreatedSpy.first().first().value<PlasmaWindow*>();
        QVERIFY(window);
        QSignalSpy destroyedSpy(window, &QObject::destroyed);
        QVERIFY(destroyedSpy.isValid());

        // now destroy without an unmap
        parentShellSurface.reset();
        parentSurface.reset();
        QVERIFY(destroyedSpy.wait());
    }

    SECTION("send to output")
    {
        wayland_test_window test_window(setup);

        auto& outputs = setup.base->outputs;
        QCOMPARE(outputs.size(), 2);

        QCOMPARE(get_output(0), test_window.server.window->topo.central_output);

        auto& client_outputs = get_client().interfaces.outputs;
        QCOMPARE(client_outputs.size(), 2);
        QCOMPARE(test_window.client.surface->outputs().size(), 1);

        auto old_client_output = client_outputs.at(0).get();
        QCOMPARE(old_client_output, test_window.client.surface->outputs().at(0));

        QSignalSpy output_entered_spy(test_window.client.surface.get(),
                                      &Wrapland::Client::Surface::outputEntered);
        QVERIFY(output_entered_spy.isValid());

        auto target_client_output = client_outputs.at(1).get();
        QVERIFY(target_client_output != old_client_output);

        test_window.client.plasma->request_send_to_output(target_client_output);
        QVERIFY(output_entered_spy.wait());

        QCOMPARE(test_window.client.surface->outputs().size(), 1);
        QCOMPARE(target_client_output, test_window.client.surface->outputs().at(0));
        QCOMPARE(get_output(1), test_window.server.window->topo.central_output);
    }

    SECTION("stacking order")
    {
        QSignalSpy stacking_spy(
            window_management,
            &Wrapland::Client::PlasmaWindowManagement::stacking_order_uuid_changed);
        QVERIFY(stacking_spy.isValid());

        std::vector<std::variant<wayland_test_window, x11_test_window>> windows;

        // Create first window.
        windows.emplace_back(wayland_test_window(setup));

        auto compare_stacks = [&]() {
            auto const& plasma_stack = window_management->stacking_order_uuid();
            auto const& unfiltered_stack = setup.base->space->stacking.order.stack;
            auto stack = std::decay_t<decltype(unfiltered_stack)>();

            std::copy_if(unfiltered_stack.begin(),
                         unfiltered_stack.end(),
                         std::back_inserter(stack),
                         [](auto win) {
                             return std::visit(overload{[&](auto&& win) { return !win->remnant; }},
                                               win);
                         });
            QCOMPARE(plasma_stack.size(), stack.size());

            for (size_t index = 0; index < windows.size(); ++index) {
                QCOMPARE(plasma_stack.at(index),
                         std::visit(overload{[&](auto&& win) {
                                        return win->meta.internal_id.toString().toStdString();
                                    }},
                                    stack.at(index)));
            }
        };

        QCOMPARE(stacking_spy.size(), 1);
        QCOMPARE(window_management->stacking_order_uuid().size(), 1);
        QCOMPARE(window_management->stacking_order_uuid().back(),
                 get_internal_id(std::get<wayland_test_window>(windows.back())));
        compare_stacks();

        // Create second window (Xwayland).
        windows.emplace_back(x11_test_window(setup));

        QCOMPARE(stacking_spy.size(), 2);
        QCOMPARE(window_management->stacking_order_uuid().size(), 2);
        QCOMPARE(window_management->stacking_order_uuid().back(),
                 get_internal_id(std::get<x11_test_window>(windows.back())));
        compare_stacks();

        // Create third window.
        windows.emplace_back(wayland_test_window(setup));

        QCOMPARE(stacking_spy.size(), 3);
        QCOMPARE(window_management->stacking_order_uuid().size(), 3);
        QCOMPARE(window_management->stacking_order_uuid().back(),
                 get_internal_id(std::get<wayland_test_window>(windows.back())));
        compare_stacks();

        // Now raise the Xwayland window.
        win::raise_window(*setup.base->space,
                          std::get<x11_test_window>(windows.at(1)).server.window);

        QVERIFY(stacking_spy.wait());
        QCOMPARE(stacking_spy.size(), 4);
        QCOMPARE(window_management->stacking_order_uuid().size(), 3);
        QCOMPARE(window_management->stacking_order_uuid().back(),
                 get_internal_id(std::get<x11_test_window>(windows.at(1))));
        compare_stacks();

        // Close the first window.
        windows.erase(windows.begin());

        QVERIFY(stacking_spy.wait());
        QCOMPARE(stacking_spy.size(), 5);
        QCOMPARE(window_management->stacking_order_uuid().size(), 2);
        QCOMPARE(window_management->stacking_order_uuid().front(),
                 get_internal_id(std::get<wayland_test_window>(windows.back())));
        QCOMPARE(window_management->stacking_order_uuid().back(),
                 get_internal_id(std::get<x11_test_window>(windows.front())));
        compare_stacks();

        // Close both remaining windows.
        windows.clear();
        QVERIFY(stacking_spy.wait());
        if (!window_management->stacking_order_uuid().empty()) {
            // Wait a bit longer for the second signal. We should get two signals at different point
            // in times due to Wayland and and X11 windows being closed through respective
            // protocols.
            QVERIFY(stacking_spy.wait());
        }
        QCOMPARE(stacking_spy.size(), 7);
        QVERIFY(window_management->stacking_order_uuid().empty());
        compare_stacks();
    }
}

}

#include "plasma_window.moc"
