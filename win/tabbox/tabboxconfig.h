/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef TABBOXCONFIG_H
#define TABBOXCONFIG_H

#include <QString>

/**
 * @file
 * This file defines the class TabBoxConfig.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */

namespace KWin
{
namespace TabBox
{
class TabBoxConfigPrivate;

/**
 * The TabBoxConfig class holds all configuration options for the TabBox.
 * The TabBoxHandler contains a pointer to an object of this class and is
 * used by all classes of TabBox. The config defines what kind of data the
 * TabBox is displaying and how the layout looks like. There can be different
 * Config sets and by setting a new config in the TabBoxHandler the behaviour
 * of the TabBox is changed immediately.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */
class TabBoxConfig
{
public:
    /**
     * ClientDesktopMode defines whether windows from the current desktop or from all
     * desktops are included in the TabBoxClient List in the TabBoxClientModel
     */
    enum ClientDesktopMode {
        AllDesktopsClients,          ///< TabBoxClients from all desktops are included.
        OnlyCurrentDesktopClients,   ///< Only TabBoxClients on current desktop are included
        ExcludeCurrentDesktopClients ///< Exclude TabBoxClients on current desktop
    };
    /**
     * ClientApplicationsMode defines which windows from the current application or from all
     * applications are included in the TabBoxClient List in the TabBoxClientModel
     */
    enum ClientApplicationsMode {
        AllWindowsAllApplications,   ///< TabBoxClients from all applications are included
        OneWindowPerApplication,     ///< Only one TabBoxClient for each application is included
        AllWindowsCurrentApplication ///< Only TabBoxClients for the current application are
                                     ///< included
    };
    /**
     * ClientMinimizedMode defines which windows are included in the TabBoxClient List
     * in the TabBoxClientModel based on whether they are minimized or not
     */
    enum ClientMinimizedMode {
        IgnoreMinimizedStatus,   ///< TabBoxClients are included no matter they are minimized or not
        ExcludeMinimizedClients, ///< Exclude minimized TabBoxClients
        OnlyMinimizedClients     ///< Only minimized TabBoxClients are included
    };
    /**
     * ShowDesktopMode defines whether a TabBoxClient representing the desktop
     * is included in the TabBoxClient List in the TabBoxClientModel
     */
    enum ShowDesktopMode {
        DoNotShowDesktopClient, ///< A TabBoxClient representing the desktop is not included
        ShowDesktopClient       ///< A TabBoxClient representing the desktop is included
    };
    /**
     * ClientMultiScreenMode defines whether windows from the current screen, all but the current
     * one or from all screens are included in the TabBoxClient List in the TabBoxClientModel.
     */
    enum ClientMultiScreenMode {
        IgnoreMultiScreen, ///< TabBoxClients are included independently of the screen they are on
        OnlyCurrentScreenClients,   ///< Only TabBoxClients on current screen are included
        ExcludeCurrentScreenClients ///< Exclude TabBoxClients from the current screen
    };
    /**
     * ClientSwitchingMode defines the sorting of the TabBoxClients in the
     * TabBoxClientModel.
     */
    enum ClientSwitchingMode {
        FocusChainSwitching,   ///< Sort by recently used. Most recently used TabBoxClient is the
                               ///< first
        StackingOrderSwitching ///< Sort by current stacking order
    };
    /**
     * DesktopSwitchingMode defines the sorting of the desktops in the
     * TabBoxDesktopModel.
     */
    enum DesktopSwitchingMode {
        MostRecentlyUsedDesktopSwitching, ///< Sort by recently used. Most recently used desktop is
                                          ///< the first
        StaticDesktopSwitching            ///< Static sorting in numerical ascending order
    };
    /**
     * TabBoxMode defines what kind of items the TabBox is displaying and which
     * Model is used
     */
    enum TabBoxMode {
        ClientTabBox, ///< TabBox uses TabBoxClientModel
        DesktopTabBox ///< TabBox uses TabBoxDesktopModel
    };
    TabBoxConfig();
    ~TabBoxConfig();
    TabBoxConfig& operator=(const TabBoxConfig& object);

    // getters
    /**
     * @return If the TabBox should be shown or not
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see setShowTabBox
     * @see defaultShowTabBox
     */
    bool is_show_tabbox() const;
    /**
     * @return If Highlight Window effect should be used.
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see setHighlightWindows
     * @see defaultHighlightWindows
     */
    bool is_highlight_windows() const;
    /**
     * @return The current TabBoxMode
     * @see setTabBoxMode
     */
    TabBoxMode tabbox_mode() const;
    /**
     * @return The current ClientDesktopMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see setClientDesktopMode
     * @see defaultDesktopMode
     */
    ClientDesktopMode client_desktop_mode() const;
    /**
     * @return The current ClientApplicationsMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see setClientApplicationsMode
     * @see defaultApplicationsMode
     */
    ClientApplicationsMode client_applications_mode() const;
    /**
     * @return The current ClientMinimizedMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see setClientMinimizedMode
     * @see defaultMinimizedMode
     */
    ClientMinimizedMode client_minimized_mode() const;
    /**
     * @return The current ShowDesktopMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see setShowDesktopMode
     * @see defaultShowDesktopMode
     */
    ShowDesktopMode show_desktop_mode() const;
    /**
     * @return The current ClientMultiScreenMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see setClientMultiScreenMode
     * @see defaultMultiScreenMode
     */
    ClientMultiScreenMode client_multi_screen_mode() const;
    /**
     * @return The current ClientSwitchingMode.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see setClientSwitchingMode
     * @see defaultSwitchingMode
     */
    ClientSwitchingMode client_switching_mode() const;
    /**
     * @return The current DesktopSwitchingMode
     * This option only applies for TabBoxMode DesktopTabBox.
     * @see setDesktopSwitchingMode
     */
    DesktopSwitchingMode desktop_switching_mode() const;
    /**
     * @return Then name of the current ItemLayout
     * @see setlayoutName
     */
    QString& layout_name() const;

    // setters
    /**
     * @param show The tabbox should be shown or not.
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see isShowTabBox
     */
    void set_show_tabbox(bool show);
    /**
     * @param highlight Highlight Windows effect should be used or not.
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see isHighlightWindows
     */
    void set_highlight_windows(bool highlight);
    /**
     * @param mode The new TabBoxMode to be used.
     * @see tabBoxMode
     */
    void set_tabbox_mode(TabBoxMode mode);
    /**
     * @param desktopMode The new ClientDesktopMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see clientDesktopMode
     */
    void set_client_desktop_mode(ClientDesktopMode desktop_mode);
    /**
     * @param applicationsMode The new ClientApplicationsMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see clientApplicationsMode
     */
    void set_client_applications_mode(ClientApplicationsMode applications_mode);
    /**
     * @param minimizedMode The new ClientMinimizedMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see clientMinimizedMode
     */
    void set_client_minimized_mode(ClientMinimizedMode minimized_mode);
    /**
     * @param showDesktopMode The new ShowDesktopMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see showDesktopMode
     */
    void set_show_desktop_mode(ShowDesktopMode show_desktop_mode);
    /**
     * @param multiScreenMode The new ClientMultiScreenMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see clientMultiScreenMode
     */
    void set_client_multi_screen_mode(ClientMultiScreenMode multi_screen_mode);
    /**
     * @param switchingMode The new ClientSwitchingMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see clientSwitchingMode
     */
    void set_client_switching_mode(ClientSwitchingMode switching_mode);
    /**
     * @param switchingMode The new DesktopSwitchingMode to be used.
     * This option only applies for TabBoxMode DesktopTabBox.
     * @see desktopSwitchingMode
     */
    void set_desktop_switching_mode(DesktopSwitchingMode switching_mode);
    /**
     * @param name The new ItemLayout config name
     * @see layoutName
     */
    void set_layout_name(const QString& name);

    // some static methods to access default values
    static ClientDesktopMode default_desktop_mode()
    {
        return OnlyCurrentDesktopClients;
    }
    static ClientApplicationsMode default_applications_mode()
    {
        return AllWindowsAllApplications;
    }
    static ClientMinimizedMode default_minimized_mode()
    {
        return IgnoreMinimizedStatus;
    }
    static ShowDesktopMode default_show_desktop_mode()
    {
        return DoNotShowDesktopClient;
    }
    static ClientMultiScreenMode default_multi_screen_mode()
    {
        return IgnoreMultiScreen;
    }
    static ClientSwitchingMode default_switching_mode()
    {
        return FocusChainSwitching;
    }
    static bool default_show_tabbox()
    {
        return true;
    }
    static bool default_highlight_window()
    {
        return true;
    }
    static QString default_layout_name()
    {
        return QStringLiteral("thumbnail_grid");
    }

private:
    TabBoxConfigPrivate* d;
};

} // namespace TabBox
} // namespace KWin

#endif // TABBOXCONFIG_H
