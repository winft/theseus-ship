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
    TabBoxConfig config;
    QCOMPARE(config.is_show_tabbox(), TabBoxConfig::default_show_tabbox());
    QCOMPARE(config.is_highlight_windows(), TabBoxConfig::default_highlight_window());
    QCOMPARE(config.tabbox_mode(), TabBoxConfig::ClientTabBox);
    QCOMPARE(config.client_desktop_mode(), TabBoxConfig::default_desktop_mode());
    QCOMPARE(config.client_applications_mode(), TabBoxConfig::default_applications_mode());
    QCOMPARE(config.client_minimized_mode(), TabBoxConfig::default_minimized_mode());
    QCOMPARE(config.show_desktop_mode(), TabBoxConfig::default_show_desktop_mode());
    QCOMPARE(config.client_multi_screen_mode(), TabBoxConfig::default_multi_screen_mode());
    QCOMPARE(config.client_switching_mode(), TabBoxConfig::default_switching_mode());
    QCOMPARE(config.desktop_switching_mode(), TabBoxConfig::MostRecentlyUsedDesktopSwitching);
    QCOMPARE(config.layout_name(), TabBoxConfig::default_layout_name());
}

void TestTabBoxConfig::testAssignmentOperator()
{
    TabBoxConfig config;
    // changing all values of the config object
    config.set_show_tabbox(!TabBoxConfig::default_show_tabbox());
    config.set_highlight_windows(!TabBoxConfig::default_highlight_window());
    config.set_tabbox_mode(TabBoxConfig::DesktopTabBox);
    config.set_client_desktop_mode(TabBoxConfig::AllDesktopsClients);
    config.set_client_applications_mode(TabBoxConfig::OneWindowPerApplication);
    config.set_client_minimized_mode(TabBoxConfig::ExcludeMinimizedClients);
    config.set_show_desktop_mode(TabBoxConfig::ShowDesktopClient);
    config.set_client_multi_screen_mode(TabBoxConfig::ExcludeCurrentScreenClients);
    config.set_client_switching_mode(TabBoxConfig::StackingOrderSwitching);
    config.set_desktop_switching_mode(TabBoxConfig::StaticDesktopSwitching);
    config.set_layout_name(QStringLiteral("grid"));
    TabBoxConfig config2;
    config2 = config;
    // verify the config2 values
    QCOMPARE(config2.is_show_tabbox(), !TabBoxConfig::default_show_tabbox());
    QCOMPARE(config2.is_highlight_windows(), !TabBoxConfig::default_highlight_window());
    QCOMPARE(config2.tabbox_mode(), TabBoxConfig::DesktopTabBox);
    QCOMPARE(config2.client_desktop_mode(), TabBoxConfig::AllDesktopsClients);
    QCOMPARE(config2.client_applications_mode(), TabBoxConfig::OneWindowPerApplication);
    QCOMPARE(config2.client_minimized_mode(), TabBoxConfig::ExcludeMinimizedClients);
    QCOMPARE(config2.show_desktop_mode(), TabBoxConfig::ShowDesktopClient);
    QCOMPARE(config2.client_multi_screen_mode(), TabBoxConfig::ExcludeCurrentScreenClients);
    QCOMPARE(config2.client_switching_mode(), TabBoxConfig::StackingOrderSwitching);
    QCOMPARE(config2.desktop_switching_mode(), TabBoxConfig::StaticDesktopSwitching);
    QCOMPARE(config2.layout_name(), QStringLiteral("grid"));
}

QTEST_MAIN(TestTabBoxConfig)

#include "test_tabbox_config.moc"
