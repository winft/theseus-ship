/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "client.h"

#include "setup.h"

#include <QThread>
#include <QtTest>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Server/display.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace Clt = Wrapland::Client;

namespace KWin::Test
{

client::client(global_selection globals)
{
    int sx[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) >= 0);

    app()->base->server->display->createClient(sx[0]);

    // Setup connection.
    connection = new Clt::ConnectionThread;

    QSignalSpy connectedSpy(connection, &Clt::ConnectionThread::establishedChanged);
    QVERIFY(connectedSpy.isValid());

    connection->setSocketFd(sx[1]);

    thread.reset(new QThread(qApp));
    connection->moveToThread(thread.get());
    thread->start();

    connection->establishConnection();
    QVERIFY((connectedSpy.count() || connectedSpy.wait()));
    QCOMPARE(connectedSpy.count(), 1);
    QVERIFY(connection->established());

    queue.reset(new Clt::EventQueue);
    queue->setup(connection);
    QVERIFY(queue->isValid());

    registry.reset(new Clt::Registry);
    registry->setEventQueue(queue.get());

    connect_outputs();

    QSignalSpy allAnnounced(registry.get(), &Clt::Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());

    registry->create(connection);
    QVERIFY(registry->isValid());

    registry->setup();
    QVERIFY((allAnnounced.count() || allAnnounced.wait()));
    QCOMPARE(allAnnounced.count(), 1);

    interfaces.compositor.reset(registry->createCompositor(
        registry->interface(Clt::Registry::Interface::Compositor).name,
        registry->interface(Clt::Registry::Interface::Compositor).version));
    QVERIFY(interfaces.compositor->isValid());

    interfaces.subcompositor.reset(registry->createSubCompositor(
        registry->interface(Clt::Registry::Interface::SubCompositor).name,
        registry->interface(Clt::Registry::Interface::SubCompositor).version));
    QVERIFY(interfaces.subcompositor->isValid());

    interfaces.shm.reset(
        registry->createShmPool(registry->interface(Clt::Registry::Interface::Shm).name,
                                registry->interface(Clt::Registry::Interface::Shm).version));
    QVERIFY(interfaces.shm->isValid());

    interfaces.xdg_shell.reset(
        registry->createXdgShell(registry->interface(Clt::Registry::Interface::XdgShell).name,
                                 registry->interface(Clt::Registry::Interface::XdgShell).version));
    QVERIFY(interfaces.xdg_shell->isValid());

    interfaces.layer_shell.reset(registry->createLayerShellV1(
        registry->interface(Clt::Registry::Interface::LayerShellV1).name,
        registry->interface(Clt::Registry::Interface::LayerShellV1).version));
    QVERIFY(interfaces.layer_shell->isValid());

    if (flags(globals & global_selection::seat)) {
        interfaces.seat.reset(
            registry->createSeat(registry->interface(Clt::Registry::Interface::Seat).name,
                                 registry->interface(Clt::Registry::Interface::Seat).version));
        QVERIFY(interfaces.seat->isValid());
    }

    if (flags(globals & global_selection::shadow)) {
        interfaces.shadow_manager.reset(registry->createShadowManager(
            registry->interface(Clt::Registry::Interface::Shadow).name,
            registry->interface(Clt::Registry::Interface::Shadow).version));
        QVERIFY(interfaces.shadow_manager->isValid());
    }

    if (flags(globals & global_selection::plasma_shell)) {
        interfaces.plasma_shell.reset(registry->createPlasmaShell(
            registry->interface(Clt::Registry::Interface::PlasmaShell).name,
            registry->interface(Clt::Registry::Interface::PlasmaShell).version));
        QVERIFY(interfaces.plasma_shell->isValid());
    }

    if (flags(globals & global_selection::window_management)) {
        interfaces.window_management.reset(registry->createPlasmaWindowManagement(
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).name,
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).version));
        QVERIFY(interfaces.window_management->isValid());
    }

    if (flags(globals & global_selection::pointer_constraints)) {
        interfaces.pointer_constraints.reset(registry->createPointerConstraints(
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).name,
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).version));
        QVERIFY(interfaces.pointer_constraints->isValid());
    }

    if (flags(globals & global_selection::pointer_gestures)) {
        interfaces.pointer_gestures.reset(registry->createPointerGestures(
            registry->interface(Clt::Registry::Interface::PointerGesturesUnstableV1).name,
            registry->interface(Clt::Registry::Interface::PointerGesturesUnstableV1).version));
        QVERIFY(interfaces.pointer_gestures->isValid());
    }

    interfaces.idle_notifier.reset(registry->createIdleNotifierV1(
        registry->interface(Clt::Registry::Interface::IdleNotifierV1).name,
        registry->interface(Clt::Registry::Interface::IdleNotifierV1).version));
    QVERIFY(interfaces.idle_notifier->isValid());

    if (flags(globals & global_selection::idle_inhibition)) {
        interfaces.idle_inhibit.reset(registry->createIdleInhibitManager(
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).name,
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).version));
        QVERIFY(interfaces.idle_inhibit->isValid());
    }

    if (flags(globals & global_selection::appmenu)) {
        interfaces.app_menu.reset(registry->createAppMenuManager(
            registry->interface(Clt::Registry::Interface::AppMenu).name,
            registry->interface(Clt::Registry::Interface::AppMenu).version));
        QVERIFY(interfaces.app_menu->isValid());
    }

    if (flags(globals & global_selection::xdg_activation)) {
        interfaces.xdg_activation.reset(registry->createXdgActivationV1(
            registry->interface(Clt::Registry::Interface::XdgActivationV1).name,
            registry->interface(Clt::Registry::Interface::XdgActivationV1).version));
        QVERIFY(interfaces.xdg_activation->isValid());

        interfaces.plasma_activation_feedback.reset(registry->createPlasmaActivationFeedback(
            registry->interface(Clt::Registry::Interface::PlasmaActivationFeedback).name,
            registry->interface(Clt::Registry::Interface::PlasmaActivationFeedback).version));
        QVERIFY(interfaces.plasma_activation_feedback->isValid());
    }

    if (flags(globals & global_selection::xdg_decoration)) {
        interfaces.xdg_decoration.reset(registry->createXdgDecorationManager(
            registry->interface(Clt::Registry::Interface::XdgDecorationUnstableV1).name,
            registry->interface(Clt::Registry::Interface::XdgDecorationUnstableV1).version));
        QVERIFY(interfaces.xdg_decoration->isValid());
    }

    if (flags(globals & global_selection::input_method_v2)) {
        interfaces.input_method_manager_v2.reset(registry->createInputMethodManagerV2(
            registry->interface(Clt::Registry::Interface::InputMethodManagerV2).name,
            registry->interface(Clt::Registry::Interface::InputMethodManagerV2).version));
        QVERIFY(interfaces.input_method_manager_v2->isValid());
    }

    if (flags(globals & global_selection::text_input_manager_v3)) {
        interfaces.text_input_manager_v3.reset(registry->createTextInputManagerV3(
            registry->interface(Clt::Registry::Interface::TextInputManagerV3).name,
            registry->interface(Clt::Registry::Interface::TextInputManagerV3).version));
        QVERIFY(interfaces.text_input_manager_v3->isValid());
    }

    if (flags(globals & global_selection::virtual_keyboard_manager_v1)) {
        interfaces.virtual_keyboard_manager_v1.reset(registry->createVirtualKeyboardManagerV1(
            registry->interface(Clt::Registry::Interface::VirtualKeyboardManagerV1).name,
            registry->interface(Clt::Registry::Interface::VirtualKeyboardManagerV1).version));
        QVERIFY(interfaces.virtual_keyboard_manager_v1->isValid());
    }
}

client::client(client&& other) noexcept
{
    *this = std::move(other);
}

client& client::operator=(client&& other) noexcept
{
    cleanup();

    QObject::disconnect(other.output_announced);
    for (auto& con : other.output_removals) {
        QObject::disconnect(con);
    }

    connection = other.connection;
    other.connection = nullptr;

    thread = std::move(other.thread);
    queue = std::move(other.queue);
    registry = std::move(other.registry);
    interfaces = std::move(other.interfaces);

    connect_outputs();

    return *this;
}

client::~client()
{
    cleanup();
}

void client::connect_outputs()
{
    output_announced = QObject::connect(
        registry.get(), &Clt::Registry::outputAnnounced, [&](quint32 name, quint32 version) {
            auto output = std::unique_ptr<Clt::Output>(
                registry->createOutput(name, version, registry.get()));
            output_removals.push_back(output_removal_connection(output.get()));
            interfaces.outputs.push_back(std::move(output));
        });

    std::transform(interfaces.outputs.begin(),
                   interfaces.outputs.end(),
                   std::back_inserter(output_removals),
                   [this](auto const& output) { return output_removal_connection(output.get()); });
}

QMetaObject::Connection client::output_removal_connection(Wrapland::Client::Output* output)
{
    return QObject::connect(output, &Clt::Output::removed, [output, this]() {
        output->deleteLater();
        auto& outs = interfaces.outputs;
        outs.erase(std::remove_if(outs.begin(),
                                  outs.end(),
                                  [output](auto const& out) { return out.get() == output; }),
                   outs.end());
    });
}

void client::cleanup()
{
    if (!connection) {
        return;
    }
    interfaces = {};
    registry.reset();
    queue.reset();

    if (thread) {
        QSignalSpy spy(connection, &QObject::destroyed);
        QVERIFY(spy.isValid());

        connection->deleteLater();
        QVERIFY((!spy.isEmpty() || spy.wait()));
        QCOMPARE(spy.count(), 1);

        thread->quit();
        thread->wait();
        thread.reset();
        connection = nullptr;
    } else {
        delete connection;
        connection = nullptr;
    }
}

}
