/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../testutils.h"
#include "mock_tabboxhandler.h"
#include "win/tabbox/tabbox_client_model.h"
#include <QX11Info>
#include <QtTest>

using namespace KWin;

class TestTabBoxHandler : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    /**
     * Test to verify that update outline does not crash
     * if the ModelIndex for which the outline should be
     * shown is not valid. That is accessing the Pointer
     * to the Client returns an invalid QVariant.
     * BUG: 304620
     */
    void testDontCrashUpdateOutlineNullClient();
};

void TestTabBoxHandler::testDontCrashUpdateOutlineNullClient()
{
    MockTabBoxHandler tabboxhandler;
    win::tabbox_config config;
    config.set_tabbox_mode(win::tabbox_config::ClientTabBox);
    config.set_show_tabbox(false);
    config.set_highlight_windows(false);
    tabboxhandler.set_config(config);
    // now show the tabbox which will attempt to show the outline
    tabboxhandler.show();
}

Q_CONSTRUCTOR_FUNCTION(forceXcb)
QTEST_MAIN(TestTabBoxHandler)

#include "test_tabbox_handler.moc"
