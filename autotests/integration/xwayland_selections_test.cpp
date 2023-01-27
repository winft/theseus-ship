/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "win/activation.h"
#include "win/space.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"
#include "xwl/data_bridge.h"
#include "xwl/xwayland.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace KWin
{

class XwaylandSelectionsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void testSync_data();
    void testSync();

private:
    QProcess* m_copyProcess = nullptr;
    QProcess* m_pasteProcess = nullptr;
};

void XwaylandSelectionsTest::initTestCase()
{
    qRegisterMetaType<QProcess::ExitStatus>();

    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void XwaylandSelectionsTest::cleanup()
{
    if (m_copyProcess) {
        m_copyProcess->terminate();
        QVERIFY(m_copyProcess->waitForFinished());
        m_copyProcess = nullptr;
    }
    if (m_pasteProcess) {
        m_pasteProcess->terminate();
        QVERIFY(m_pasteProcess->waitForFinished());
        m_pasteProcess = nullptr;
    }
}

void XwaylandSelectionsTest::testSync_data()
{
    QTest::addColumn<QString>("clipboardMode");
    QTest::addColumn<QString>("copyPlatform");
    QTest::addColumn<QString>("pastePlatform");

    QTest::newRow("Clipboard x11->wayland")
        << QStringLiteral("Clipboard") << QStringLiteral("xcb") << QStringLiteral("wayland");
    QTest::newRow("Clipboard wayland->x11")
        << QStringLiteral("Clipboard") << QStringLiteral("wayland") << QStringLiteral("xcb");
    QTest::newRow("primary_selection x11->wayland")
        << QStringLiteral("Selection") << QStringLiteral("xcb") << QStringLiteral("wayland");
    QTest::newRow("primary_selection wayland->x11")
        << QStringLiteral("Selection") << QStringLiteral("wayland") << QStringLiteral("xcb");
}

void XwaylandSelectionsTest::testSync()
{
    QFETCH(QString, clipboardMode);

    // this test verifies the syncing of X11 to Wayland clipboard
    const QString copy = QFINDTESTDATA(QStringLiteral("copy"));
    QVERIFY(!copy.isEmpty());
    const QString paste = QFINDTESTDATA(QStringLiteral("paste"));
    QVERIFY(!paste.isEmpty());

    QSignalSpy clientAddedSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::clientAdded);
    QVERIFY(clientAddedSpy.isValid());
    QSignalSpy shellClientAddedSpy(Test::app()->base.space->qobject.get(),
                                   &win::space::qobject_t::wayland_window_added);
    QVERIFY(shellClientAddedSpy.isValid());

    QSignalSpy clipboardChangedSpy = [clipboardMode]() {
        if (clipboardMode == "Clipboard") {
            return QSignalSpy(Test::app()->base.server->seat(),
                              &Wrapland::Server::Seat::selectionChanged);
        }
        if (clipboardMode == "Selection") {
            return QSignalSpy(Test::app()->base.server->seat(),
                              &Wrapland::Server::Seat::primarySelectionChanged);
        }
        std::terminate();
    }();
    QVERIFY(clipboardChangedSpy.isValid());

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    // start the copy process
    QFETCH(QString, copyPlatform);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), copyPlatform);
    m_copyProcess = new QProcess();
    m_copyProcess->setProcessEnvironment(environment);
    m_copyProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    m_copyProcess->setProgram(copy);
    m_copyProcess->setArguments({clipboardMode});
    m_copyProcess->start();
    QVERIFY(m_copyProcess->waitForStarted());

    std::optional<Test::space::window_t> copyClient;
    if (copyPlatform == QLatin1String("xcb")) {
        QVERIFY(clientAddedSpy.wait());
        auto copy_client_id = clientAddedSpy.first().first().value<quint32>();
        copyClient = Test::app()->base.space->windows_map.at(copy_client_id);
    } else {
        QVERIFY(shellClientAddedSpy.wait());
        auto copy_client_id = shellClientAddedSpy.first().first().value<quint32>();
        copyClient = Test::app()->base.space->windows_map.at(copy_client_id);
    }
    QVERIFY(copyClient);
    if (Test::app()->base.space->stacking.active != *copyClient) {
        std::visit(
            overload{[](auto&& win) { win::activate_window(*Test::app()->base.space, *win); }},
            *copyClient);
    }
    QCOMPARE(Test::app()->base.space->stacking.active, copyClient);
    if (copyPlatform == QLatin1String("xcb")) {
        QVERIFY(clipboardChangedSpy.isEmpty());
        QVERIFY(clipboardChangedSpy.wait());
    } else {
        // TODO: it would be better to be able to connect to a signal, instead of waiting
        // the idea is to make sure that the clipboard is updated, thus we need to give it
        // enough time before starting the paste process which creates another window
        QTest::qWait(250);
    }

    // start the paste process
    m_pasteProcess = new QProcess();
    QSignalSpy finishedSpy(
        m_pasteProcess,
        static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished));
    QVERIFY(finishedSpy.isValid());
    QFETCH(QString, pastePlatform);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), pastePlatform);
    m_pasteProcess->setProcessEnvironment(environment);
    m_pasteProcess->setProcessChannelMode(QProcess::ForwardedChannels);
    m_pasteProcess->setProgram(paste);
    m_pasteProcess->setArguments({clipboardMode});
    m_pasteProcess->start();
    QVERIFY(m_pasteProcess->waitForStarted());

    std::optional<Test::space::window_t> pasteClient;
    if (pastePlatform == QLatin1String("xcb")) {
        QVERIFY(clientAddedSpy.wait());
        auto paste_client_id = clientAddedSpy.last().first().value<quint32>();
        pasteClient = Test::app()->base.space->windows_map.at(paste_client_id);
    } else {
        QVERIFY(shellClientAddedSpy.wait());
        auto paste_client_id = shellClientAddedSpy.last().first().value<quint32>();
        pasteClient = Test::app()->base.space->windows_map.at(paste_client_id);
    }
    QCOMPARE(clientAddedSpy.count(), 1);
    QCOMPARE(shellClientAddedSpy.count(), 1);
    QVERIFY(pasteClient);

    if (Test::app()->base.space->stacking.active != pasteClient) {
        QSignalSpy clientActivatedSpy(Test::app()->base.space->qobject.get(),
                                      &win::space::qobject_t::clientActivated);
        QVERIFY(clientActivatedSpy.isValid());
        std::visit(
            overload{[](auto&& win) { win::activate_window(*Test::app()->base.space, *win); }},
            *pasteClient);
        QVERIFY(clientActivatedSpy.wait());
    }
    QTRY_COMPARE(Test::app()->base.space->stacking.active, pasteClient);
    QVERIFY(finishedSpy.wait());
    QCOMPARE(finishedSpy.first().first().toInt(), 0);
    delete m_pasteProcess;
    m_pasteProcess = nullptr;
}

}

WAYLANDTEST_MAIN(KWin::XwaylandSelectionsTest)
#include "xwayland_selections_test.moc"
