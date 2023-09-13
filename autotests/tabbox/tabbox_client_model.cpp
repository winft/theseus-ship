/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../integration/lib/catch_macros.h"
#include "mock_tabbox_handler.h"

#include "win/tabbox/tabbox_client.h"
#include "win/tabbox/tabbox_client_model.h"

namespace KWin::detail::test
{

TEST_CASE("tabbox client model", "[unit],[win]")
{
    SECTION("longest caption")
    {
        MockTabBoxHandler tabboxhandler;
        auto clientModel = new win::tabbox_client_model(&tabboxhandler);
        clientModel->create_client_list();
        QCOMPARE(clientModel->longest_caption(), QString());

        // add a window to the mock
        tabboxhandler.createMockWindow(QString("test"));
        clientModel->create_client_list();
        QCOMPARE(clientModel->longest_caption(), QString("test"));
    }

    SECTION("create client list no active client")
    {
        MockTabBoxHandler tabboxhandler;
        tabboxhandler.set_config(win::tabbox_config());
        auto clientModel = new win::tabbox_client_model(&tabboxhandler);
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

    SECTION("create client list active client not in focus chain")
    {
        MockTabBoxHandler tabboxhandler;
        tabboxhandler.set_config(win::tabbox_config());
        auto clientModel = new win::tabbox_client_model(&tabboxhandler);
        // create two windows, rowCount() should go to two
        auto client = tabboxhandler.createMockWindow(QString("test"));
        client = tabboxhandler.createMockWindow(QString("test2"));
        clientModel->create_client_list();
        QCOMPARE(clientModel->rowCount(), 2);

        // simulate that the active client is not in the focus chain
        // for that we use the closeWindow of the MockTabBoxHandler which
        // removes the Client from the Focus Chain but leaves the active window as it is
        tabboxhandler.closeWindow(client);
        clientModel->create_client_list();
        QCOMPARE(clientModel->rowCount(), 1);
    }
}

}
