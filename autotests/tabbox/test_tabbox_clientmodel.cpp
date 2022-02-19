/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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
#include "test_tabbox_clientmodel.h"
#include "../testutils.h"
#include "mock_tabboxhandler.h"
#include "win/tabbox/tabbox_client_model.h"

#include <QX11Info>
#include <QtTest>
using namespace KWin;

void TestTabBoxClientModel::initTestCase()
{
    qApp->setProperty("x11Connection", QVariant::fromValue<void*>(QX11Info::connection()));
}

void TestTabBoxClientModel::testLongestCaptionWithNullClient()
{
    MockTabBoxHandler tabboxhandler;
    win::tabbox_client_model* clientModel = new win::tabbox_client_model(&tabboxhandler);
    clientModel->create_client_list();
    QCOMPARE(clientModel->longest_caption(), QString());
    // add a window to the mock
    tabboxhandler.createMockWindow(QString("test"));
    clientModel->create_client_list();
    QCOMPARE(clientModel->longest_caption(), QString("test"));
    // delete the one client in the list
    QModelIndex index = clientModel->index(0, 0);
    QVERIFY(index.isValid());
    win::tabbox_client* client = static_cast<win::tabbox_client*>(
        clientModel->data(index, win::tabbox_client_model::ClientRole).value<void*>());
    client->close();
    // internal model of ClientModel now contains a deleted pointer
    // longestCaption should behave just as if the window were not in the list
    QCOMPARE(clientModel->longest_caption(), QString());
}

void TestTabBoxClientModel::testCreateClientListNoActiveClient()
{
    MockTabBoxHandler tabboxhandler;
    tabboxhandler.set_config(win::tabbox_config());
    win::tabbox_client_model* clientModel = new win::tabbox_client_model(&tabboxhandler);
    clientModel->create_client_list();
    QCOMPARE(clientModel->rowCount(), 0);
    // create two windows, rowCount() should go to two
    auto client = tabboxhandler.createMockWindow(QString("test"));
    tabboxhandler.createMockWindow(QString("test2"));
    clientModel->create_client_list();
    QCOMPARE(clientModel->rowCount(), 2);
    // let's ensure there is no active client
    tabboxhandler.set_active_client(decltype(client)());
    // now it should still have two members in the list
    clientModel->create_client_list();
    QCOMPARE(clientModel->rowCount(), 2);
}

void TestTabBoxClientModel::testCreateClientListActiveClientNotInFocusChain()
{
    MockTabBoxHandler tabboxhandler;
    tabboxhandler.set_config(win::tabbox_config());
    win::tabbox_client_model* clientModel = new win::tabbox_client_model(&tabboxhandler);
    // create two windows, rowCount() should go to two
    auto client = tabboxhandler.createMockWindow(QString("test"));
    client = tabboxhandler.createMockWindow(QString("test2"));
    clientModel->create_client_list();
    QCOMPARE(clientModel->rowCount(), 2);

    // simulate that the active client is not in the focus chain
    // for that we use the closeWindow of the MockTabBoxHandler which
    // removes the Client from the Focus Chain but leaves the active window as it is
    auto clientOwner = client.lock();
    tabboxhandler.closeWindow(clientOwner.get());
    clientModel->create_client_list();
    QCOMPARE(clientModel->rowCount(), 1);
}

Q_CONSTRUCTOR_FUNCTION(forceXcb)
QTEST_MAIN(TestTabBoxClientModel)
