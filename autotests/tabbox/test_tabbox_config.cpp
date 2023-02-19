/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "../../win/tabbox/tabbox_config.h"

#include <QtTest>

using namespace KWin;
using namespace KWin::win;

class TestTabBoxConfig : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testDefaultCtor();
    void testAssignmentOperator();
};

void TestTabBoxConfig::testDefaultCtor()
{
    tabbox_config config;
    QCOMPARE(config.is_show_tabbox(), tabbox_config::default_show_tabbox());
    QCOMPARE(config.is_highlight_windows(), tabbox_config::default_highlight_window());
    QCOMPARE(config.tabbox_mode(), tabbox_config::ClientTabBox);
    QCOMPARE(config.client_desktop_mode(), tabbox_config::default_desktop_mode());
    QCOMPARE(config.client_applications_mode(), tabbox_config::default_applications_mode());
    QCOMPARE(config.client_minimized_mode(), tabbox_config::default_minimized_mode());
    QCOMPARE(config.show_desktop_mode(), tabbox_config::default_show_desktop_mode());
    QCOMPARE(config.client_multi_screen_mode(), tabbox_config::default_multi_screen_mode());
    QCOMPARE(config.client_switching_mode(), tabbox_config::default_switching_mode());
    QCOMPARE(config.desktop_switching_mode(), tabbox_config::MostRecentlyUsedDesktopSwitching);
    QCOMPARE(config.layout_name(), tabbox_config::default_layout_name());
}

void TestTabBoxConfig::testAssignmentOperator()
{
    tabbox_config config;
    // changing all values of the config object
    config.set_show_tabbox(!tabbox_config::default_show_tabbox());
    config.set_highlight_windows(!tabbox_config::default_highlight_window());
    config.set_tabbox_mode(tabbox_config::DesktopTabBox);
    config.set_client_desktop_mode(tabbox_config::AllDesktopsClients);
    config.set_client_applications_mode(tabbox_config::OneWindowPerApplication);
    config.set_client_minimized_mode(tabbox_config::ExcludeMinimizedClients);
    config.set_show_desktop_mode(tabbox_config::ShowDesktopClient);
    config.set_client_multi_screen_mode(tabbox_config::ExcludeCurrentScreenClients);
    config.set_client_switching_mode(tabbox_config::StackingOrderSwitching);
    config.set_desktop_switching_mode(tabbox_config::StaticDesktopSwitching);
    config.set_layout_name(QStringLiteral("grid"));
    tabbox_config config2;
    config2 = config;
    // verify the config2 values
    QCOMPARE(config2.is_show_tabbox(), !tabbox_config::default_show_tabbox());
    QCOMPARE(config2.is_highlight_windows(), !tabbox_config::default_highlight_window());
    QCOMPARE(config2.tabbox_mode(), tabbox_config::DesktopTabBox);
    QCOMPARE(config2.client_desktop_mode(), tabbox_config::AllDesktopsClients);
    QCOMPARE(config2.client_applications_mode(), tabbox_config::OneWindowPerApplication);
    QCOMPARE(config2.client_minimized_mode(), tabbox_config::ExcludeMinimizedClients);
    QCOMPARE(config2.show_desktop_mode(), tabbox_config::ShowDesktopClient);
    QCOMPARE(config2.client_multi_screen_mode(), tabbox_config::ExcludeCurrentScreenClients);
    QCOMPARE(config2.client_switching_mode(), tabbox_config::StackingOrderSwitching);
    QCOMPARE(config2.desktop_switching_mode(), tabbox_config::StaticDesktopSwitching);
    QCOMPARE(config2.layout_name(), QStringLiteral("grid"));
}

QTEST_MAIN(TestTabBoxConfig)

#include "test_tabbox_config.moc"
