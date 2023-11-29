/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("xwayland selections", "[win],[xwl]")
{
    test::setup setup("xwayland-selections", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection();

    SECTION("sync")
    {
        enum class sync_direction {
            wayland_to_x11,
            x11_to_wayland,
        };

        auto clipboard_mode = GENERATE(as<std::string>{}, "Clipboard", "Selection");
        auto direction = GENERATE(sync_direction::wayland_to_x11, sync_direction::x11_to_wayland);

        QString copy_platform = QStringLiteral("wayland");
        QString paste_platform = QStringLiteral("xcb");

        if (direction == sync_direction::x11_to_wayland) {
            copy_platform = QStringLiteral("xcb");
            paste_platform = QStringLiteral("wayland");
        } else {
            REQUIRE(direction == sync_direction::wayland_to_x11);
        }

        // this test verifies the syncing of X11 to Wayland clipboard
        QString const copy = QFINDTESTDATA(QStringLiteral("copy"));
        QVERIFY(!copy.isEmpty());
        const QString paste = QFINDTESTDATA(QStringLiteral("paste"));
        QVERIFY(!paste.isEmpty());

        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::clientAdded);
        QVERIFY(clientAddedSpy.isValid());
        QSignalSpy shellClientAddedSpy(setup.base->mod.space->qobject.get(),
                                       &space::qobject_t::wayland_window_added);
        QVERIFY(shellClientAddedSpy.isValid());

        QSignalSpy clipboardChangedSpy = [&setup, &clipboard_mode]() {
            if (clipboard_mode == "Clipboard") {
                return QSignalSpy(setup.base->server->seat(),
                                  &Wrapland::Server::Seat::selectionChanged);
            }
            if (clipboard_mode == "Selection") {
                return QSignalSpy(setup.base->server->seat(),
                                  &Wrapland::Server::Seat::primarySelectionChanged);
            }
            std::terminate();
        }();

        QVERIFY(clipboardChangedSpy.isValid());

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

        // start the copy process
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), copy_platform);
        auto copy_process = new QProcess();
        copy_process->setProcessEnvironment(environment);
        copy_process->setProcessChannelMode(QProcess::ForwardedChannels);
        copy_process->setProgram(copy);
        copy_process->setArguments({QString::fromStdString(clipboard_mode)});
        copy_process->start();
        QVERIFY(copy_process->waitForStarted());

        std::optional<space::window_t> copyClient;
        if (copy_platform == QLatin1String("xcb")) {
            QVERIFY(clientAddedSpy.wait());
            auto copy_client_id = clientAddedSpy.first().first().value<quint32>();
            copyClient = setup.base->mod.space->windows_map.at(copy_client_id);
        } else {
            QVERIFY(shellClientAddedSpy.wait());
            auto copy_client_id = shellClientAddedSpy.first().first().value<quint32>();
            copyClient = setup.base->mod.space->windows_map.at(copy_client_id);
        }
        QVERIFY(copyClient);
        if (setup.base->mod.space->stacking.active != *copyClient) {
            std::visit(overload{[&setup](auto&& win) {
                           win::activate_window(*setup.base->mod.space, *win);
                       }},
                       *copyClient);
        }
        QCOMPARE(setup.base->mod.space->stacking.active, copyClient);
        if (copy_platform == QLatin1String("xcb")) {
            QVERIFY(clipboardChangedSpy.isEmpty());
            QVERIFY(clipboardChangedSpy.wait());
        } else {
            // TODO: it would be better to be able to connect to a signal, instead of waiting
            // the idea is to make sure that the clipboard is updated, thus we need to give it
            // enough time before starting the paste process which creates another window
            QTest::qWait(250);
        }

        // start the paste process
        auto paste_process = new QProcess();
        QSignalSpy finishedSpy(
            paste_process,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished));
        QVERIFY(finishedSpy.isValid());
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), paste_platform);
        paste_process->setProcessEnvironment(environment);
        paste_process->setProcessChannelMode(QProcess::ForwardedChannels);
        paste_process->setProgram(paste);
        paste_process->setArguments({QString::fromStdString(clipboard_mode)});
        paste_process->start();
        QVERIFY(paste_process->waitForStarted());

        std::optional<space::window_t> pasteClient;
        if (paste_platform == QLatin1String("xcb")) {
            QVERIFY(clientAddedSpy.wait());
            auto paste_client_id = clientAddedSpy.last().first().value<quint32>();
            pasteClient = setup.base->mod.space->windows_map.at(paste_client_id);
        } else {
            QVERIFY(shellClientAddedSpy.wait());
            auto paste_client_id = shellClientAddedSpy.last().first().value<quint32>();
            pasteClient = setup.base->mod.space->windows_map.at(paste_client_id);
        }
        QCOMPARE(clientAddedSpy.count(), 1);
        QCOMPARE(shellClientAddedSpy.count(), 1);
        QVERIFY(pasteClient);

        if (setup.base->mod.space->stacking.active != pasteClient) {
            QSignalSpy clientActivatedSpy(setup.base->mod.space->qobject.get(),
                                          &space::qobject_t::clientActivated);
            QVERIFY(clientActivatedSpy.isValid());
            std::visit(overload{[&setup](auto&& win) {
                           win::activate_window(*setup.base->mod.space, *win);
                       }},
                       *pasteClient);
            QVERIFY(clientActivatedSpy.wait());
        }
        QTRY_COMPARE(setup.base->mod.space->stacking.active, pasteClient);
        QVERIFY(finishedSpy.wait());
        QCOMPARE(finishedSpy.first().first().toInt(), 0);
        delete paste_process;
        paste_process = nullptr;

        if (copy_process) {
            copy_process->terminate();
            QVERIFY(copy_process->waitForFinished());
            copy_process = nullptr;
        }
        if (paste_process) {
            paste_process->terminate();
            QVERIFY(paste_process->waitForFinished());
            paste_process = nullptr;
        }
    }
}

}
