/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "win/control.h"
#include "win/move.h"
#include "win/space.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"
#include "win/wayland/xdg_activation.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_activation_v1.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/xdg_activation_v1.h>

namespace Clt = Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("xdg activation", "[win]")
{
    qRegisterMetaType<std::string>();
    qRegisterMetaType<Wrapland::Server::Surface*>();

    test::setup setup("xdg-activation");
    setup.start();
    setup_wayland_connection(global_selection::seat | global_selection::xdg_activation);

    SECTION("single client")
    {
        // Check that XDG Activation works for two different surfaces of a single client.

        std::unique_ptr<Clt::Surface> surface1(create_surface());
        auto shell_surface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(surface1);
        QVERIFY(shell_surface1);

        auto window1 = render_and_wait_for_shown(surface1, QSize(200, 100), Qt::red);
        QVERIFY(window1);
        QCOMPARE(win::render_geometry(window1).size(), QSize(200, 100));
        QCOMPARE(window1->geo.frame.size(), QSize(200, 100));
        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1);

        std::unique_ptr<Clt::Surface> surface2(create_surface());
        auto shell_surface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(surface2);
        QVERIFY(shell_surface2);

        auto window2 = render_and_wait_for_shown(surface2, QSize(400, 200), Qt::blue);
        QVERIFY(window2);
        QCOMPARE(win::render_geometry(window2).size(), QSize(400, 200));
        QCOMPARE(window2->geo.frame.size(), QSize(400, 200));
        QVERIFY(window2->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window2);

        auto activation = get_client().interfaces.xdg_activation.get();
        QVERIFY(activation);

        auto server_activation = setup.base->space->xdg_activation->interface.get();
        QVERIFY(server_activation);
        QSignalSpy token_spy(server_activation,
                             &Wrapland::Server::XdgActivationV1::token_requested);
        QVERIFY(token_spy.isValid());

        auto token = std::unique_ptr<Clt::XdgActivationTokenV1>(activation->create_token());
        token->set_serial(0, get_client().interfaces.seat.get());
        token->set_surface(surface2.get());
        token->set_app_id("testclient1");
        token->commit();

        QVERIFY(token_spy.wait());
        auto server_token
            = token_spy.front().front().value<Wrapland::Server::XdgActivationTokenV1*>();
        QCOMPARE(server_token->app_id(), "testclient1");

        auto const token_string = setup.base->space->xdg_activation->token;

        QSignalSpy done_spy(token.get(), &Clt::XdgActivationTokenV1::done);
        QVERIFY(done_spy.isValid());
        QVERIFY(done_spy.wait());

        QCOMPARE(done_spy.front().front().value<std::string>(), token_string);

        activation->activate(token_string, surface1.get());

        QSignalSpy xdg_activate_spy(server_activation,
                                    &Wrapland::Server::XdgActivationV1::activate);
        QVERIFY(xdg_activate_spy.isValid());
        QSignalSpy activated_spy(setup.base->space->qobject.get(),
                                 &win::space::qobject_t::clientActivated);
        QVERIFY(activated_spy.isValid());

        QVERIFY(activated_spy.wait());
        QVERIFY(!xdg_activate_spy.empty());

        QCOMPARE(xdg_activate_spy.front().front().value<std::string>(), token_string);
        QCOMPARE(xdg_activate_spy.front().back().value<Wrapland::Server::Surface*>(),
                 window1->surface);
        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1);
    }

    SECTION("multi client")
    {
        // Check that XDG Activation works for two different clients.

        std::unique_ptr<Clt::Surface> surface1(create_surface());
        auto shell_surface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(surface1);
        QVERIFY(shell_surface1);

        auto window1 = render_and_wait_for_shown(surface1, QSize(200, 100), Qt::red);
        QVERIFY(window1);
        QCOMPARE(win::render_geometry(window1).size(), QSize(200, 100));
        QCOMPARE(window1->geo.frame.size(), QSize(200, 100));
        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1);

        // Create a second client.
        setup_wayland_connection(global_selection::seat | global_selection::xdg_activation);
        auto& client2 = get_all_clients().back();

        std::unique_ptr<Clt::Surface> surface2(create_surface(client2));
        auto shell_surface2 = create_xdg_shell_toplevel(client2, surface2);
        QVERIFY(surface2);
        QVERIFY(shell_surface2);

        auto window2 = render_and_wait_for_shown(client2, surface2, QSize(400, 200), Qt::blue);
        QVERIFY(window2);
        QCOMPARE(win::render_geometry(window2).size(), QSize(400, 200));
        QCOMPARE(window2->geo.frame.size(), QSize(400, 200));
        QVERIFY(window2->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window2);

        auto activation2 = client2.interfaces.xdg_activation.get();
        QVERIFY(activation2);

        auto server_activation = setup.base->space->xdg_activation->interface.get();
        QVERIFY(server_activation);
        QSignalSpy token_spy(server_activation,
                             &Wrapland::Server::XdgActivationV1::token_requested);
        QVERIFY(token_spy.isValid());

        auto token = std::unique_ptr<Clt::XdgActivationTokenV1>(activation2->create_token());
        token->set_serial(0, client2.interfaces.seat.get());
        token->set_surface(surface2.get());
        token->set_app_id("testclient1");
        token->commit();

        QVERIFY(token_spy.wait());
        auto server_token
            = token_spy.front().front().value<Wrapland::Server::XdgActivationTokenV1*>();
        QCOMPARE(server_token->app_id(), "testclient1");

        auto const token_string = setup.base->space->xdg_activation->token;

        QSignalSpy done_spy(token.get(), &Clt::XdgActivationTokenV1::done);
        QVERIFY(done_spy.isValid());
        QVERIFY(done_spy.wait());

        QCOMPARE(done_spy.front().front().value<std::string>(), token_string);

        auto activation1 = get_client().interfaces.xdg_activation.get();
        activation1->activate(token_string, surface1.get());

        QSignalSpy xdg_activate_spy(server_activation,
                                    &Wrapland::Server::XdgActivationV1::activate);
        QVERIFY(xdg_activate_spy.isValid());
        QSignalSpy activated_spy(setup.base->space->qobject.get(),
                                 &win::space::qobject_t::clientActivated);
        QVERIFY(activated_spy.isValid());

        QVERIFY(activated_spy.wait());
        QVERIFY(!xdg_activate_spy.empty());

        QCOMPARE(xdg_activate_spy.front().front().value<std::string>(), token_string);
        QCOMPARE(xdg_activate_spy.front().back().value<Wrapland::Server::Surface*>(),
                 window1->surface);
        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1);
    }

    SECTION("plasma activation feedback")
    {
        // Check that Plasma activation feedback works together with xdg activation.

        std::unique_ptr<Clt::Surface> surface1(create_surface());
        auto shell_surface1 = create_xdg_shell_toplevel(surface1);
        QVERIFY(surface1);
        QVERIFY(shell_surface1);

        auto window1 = render_and_wait_for_shown(surface1, QSize(200, 100), Qt::red);
        QVERIFY(window1);
        QCOMPARE(win::render_geometry(window1).size(), QSize(200, 100));
        QCOMPARE(window1->geo.frame.size(), QSize(200, 100));
        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1);

        std::unique_ptr<Clt::Surface> surface2(create_surface());
        auto shell_surface2 = create_xdg_shell_toplevel(surface2);
        QVERIFY(surface2);
        QVERIFY(shell_surface2);

        auto window2 = render_and_wait_for_shown(surface2, QSize(400, 200), Qt::blue);
        QVERIFY(window2);
        QCOMPARE(win::render_geometry(window2).size(), QSize(400, 200));
        QCOMPARE(window2->geo.frame.size(), QSize(400, 200));
        QVERIFY(window2->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window2);

        QSignalSpy plasma_activation_spy(get_client().interfaces.plasma_activation_feedback.get(),
                                         &Wrapland::Client::plasma_activation_feedback::activation);
        QVERIFY(plasma_activation_spy.isValid());

        auto activation = get_client().interfaces.xdg_activation.get();
        QVERIFY(activation);

        auto server_activation = setup.base->space->xdg_activation->interface.get();
        QVERIFY(server_activation);
        QSignalSpy token_spy(server_activation,
                             &Wrapland::Server::XdgActivationV1::token_requested);
        QVERIFY(token_spy.isValid());

        auto token = std::unique_ptr<Clt::XdgActivationTokenV1>(activation->create_token());
        auto const appid = std::string("testclient1");
        token->set_serial(0, get_client().interfaces.seat.get());
        token->set_surface(surface2.get());
        token->set_app_id(appid);
        token->commit();

        QSignalSpy done_spy(token.get(), &Clt::XdgActivationTokenV1::done);
        QVERIFY(done_spy.isValid());

        QVERIFY(token_spy.wait());

        auto server_token
            = token_spy.front().front().value<Wrapland::Server::XdgActivationTokenV1*>();
        QCOMPARE(server_token->app_id(), appid);

        auto const token_string = setup.base->space->xdg_activation->token;

        QVERIFY(plasma_activation_spy.wait());
        QCOMPARE(plasma_activation_spy.size(), 1);
        QVERIFY(!done_spy.empty());
        QCOMPARE(done_spy.front().front().value<std::string>(), token_string);

        auto plasma_activation
            = plasma_activation_spy.front().front().value<Wrapland::Client::plasma_activation*>();

        QSignalSpy plasma_activation_appid_spy(
            plasma_activation, &Wrapland::Client::plasma_activation::app_id_changed);
        QSignalSpy plasma_activation_finished_spy(plasma_activation,
                                                  &Wrapland::Client::plasma_activation::finished);
        QVERIFY(plasma_activation_appid_spy.isValid());
        QVERIFY(plasma_activation_finished_spy.isValid());

        if (plasma_activation->app_id().empty()) {
            QVERIFY(plasma_activation_appid_spy.wait());
        }
        QCOMPARE(plasma_activation->app_id(), appid);

        activation->activate(token_string, surface1.get());
        QVERIFY(plasma_activation_finished_spy.wait());

        QVERIFY(window1->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.active), window1);

        QVERIFY(plasma_activation->is_finished());
        delete plasma_activation;
    }
}

}
