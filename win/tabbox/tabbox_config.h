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
 * This file defines the class tabbox_config.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */

namespace KWin
{
namespace win
{
class tabbox_config_private;

/**
 * The tabbox_config class holds all configuration options for the tabbox.
 * The tabbox_handler contains a pointer to an object of this class and is
 * used by all classes of tabbox. The config defines what kind of data the
 * tabbox is displaying and how the layout looks like. There can be different
 * Config sets and by setting a new config in the tabbox_handler the behaviour
 * of the tabbox is changed immediately.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 * @since 4.4
 */
class tabbox_config
{
public:
    /**
     * ClientDesktopMode defines whether windows from the current desktop or from all
     * desktops are included in the tabbox_client List in the client_model
     */
    enum ClientDesktopMode {
        AllDesktopsClients,          ///< tabbox_clients from all desktops are included.
        OnlyCurrentDesktopClients,   ///< Only tabbox_clients on current desktop are included
        ExcludeCurrentDesktopClients ///< Exclude tabbox_clients on current desktop
    };
    /**
     * ClientApplicationsMode defines which windows from the current application or from all
     * applications are included in the tabbox_client List in the client_model
     */
    enum ClientApplicationsMode {
        AllWindowsAllApplications,   ///< tabbox_clients from all applications are included
        OneWindowPerApplication,     ///< Only one tabbox_client for each application is included
        AllWindowsCurrentApplication ///< Only tabbox_clients for the current application are
                                     ///< included
    };
    /**
     * ClientMinimizedMode defines which windows are included in the tabbox_client List
     * in the client_model based on whether they are minimized or not
     */
    enum ClientMinimizedMode {
        IgnoreMinimizedStatus, ///< tabbox_clients are included no matter they are minimized or not
        ExcludeMinimizedClients, ///< Exclude minimized tabbox_clients
        OnlyMinimizedClients     ///< Only minimized tabbox_clients are included
    };
    /**
     * ShowDesktopMode defines whether a tabbox_client representing the desktop
     * is included in the tabbox_client List in the client_model
     */
    enum ShowDesktopMode {
        DoNotShowDesktopClient, ///< A tabbox_client representing the desktop is not included
        ShowDesktopClient       ///< A tabbox_client representing the desktop is included
    };
    /**
     * ClientMultiScreenMode defines whether windows from the current screen, all but the current
     * one or from all screens are included in the tabbox_client List in the client_model.
     */
    enum ClientMultiScreenMode {
        IgnoreMultiScreen, ///< tabbox_clients are included independently of the screen they are on
        OnlyCurrentScreenClients,   ///< Only tabbox_clients on current screen are included
        ExcludeCurrentScreenClients ///< Exclude tabbox_clients from the current screen
    };
    /**
     * ClientSwitchingMode defines the sorting of the tabbox_clients in the
     * client_model.
     */
    enum ClientSwitchingMode {
        FocusChainSwitching,   ///< Sort by recently used. Most recently used tabbox_client is the
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
        ClientTabBox, ///< tabbox uses client_model
        DesktopTabBox ///< tabbox uses desktop_model
    };

    tabbox_config();
    tabbox_config(tabbox_config const& other);
    tabbox_config& operator=(tabbox_config const& other);
    tabbox_config(tabbox_config&& other) noexcept;
    tabbox_config& operator=(tabbox_config&& other) noexcept;

    ~tabbox_config();

    // getters
    /**
     * @return If the tabbox should be shown or not
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see set_show_tabbox
     * @see default_show_tabbox
     */
    bool is_show_tabbox() const;
    /**
     * @return If Highlight Window effect should be used.
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see set_highlight_windows
     * @see default_highlight_windows
     */
    bool is_highlight_windows() const;
    /**
     * @return The current TabBoxMode
     * @see set_tabbox_mode
     */
    TabBoxMode tabbox_mode() const;
    /**
     * @return The current ClientDesktopMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see set_client_desktop_mode
     * @see default_desktop_mode
     */
    ClientDesktopMode client_desktop_mode() const;
    /**
     * @return The current ClientApplicationsMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see set_client_applications_mode
     * @see default_applications_mode
     */
    ClientApplicationsMode client_applications_mode() const;
    /**
     * @return The current ClientMinimizedMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see set_client_minimized_mode
     * @see default_minimized_mode
     */
    ClientMinimizedMode client_minimized_mode() const;
    /**
     * @return The current ShowDesktopMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see set_show_desktop_mode
     * @see default_show_desktop_mode
     */
    ShowDesktopMode show_desktop_mode() const;
    /**
     * @return The current ClientMultiScreenMode
     * This option only applies for TabBoxMode ClientTabBox.
     * @see set_client_multi_screen_mode
     * @see default_multi_screen_mode
     */
    ClientMultiScreenMode client_multi_screen_mode() const;
    /**
     * @return The current ClientSwitchingMode.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see set_client_switching_mode
     * @see default_switching_mode
     */
    ClientSwitchingMode client_switching_mode() const;
    /**
     * @return The current DesktopSwitchingMode
     * This option only applies for TabBoxMode DesktopTabBox.
     * @see set_desktop_switching_mode
     */
    DesktopSwitchingMode desktop_switching_mode() const;
    /**
     * @return Then name of the current item_layout
     * @see set_layout_name
     */
    QString& layout_name() const;

    // setters
    /**
     * @param show The tabbox should be shown or not.
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see is_show_tabbox
     */
    void set_show_tabbox(bool show);
    /**
     * @param highlight Highlight Windows effect should be used or not.
     * This option does not apply for TabBoxMode DesktopTabBox.
     * @see is_highlight_windows
     */
    void set_highlight_windows(bool highlight);
    /**
     * @param mode The new TabBoxMode to be used.
     * @see tabBoxMode
     */
    void set_tabbox_mode(TabBoxMode mode);
    /**
     * @param desktop_mode The new ClientDesktopMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see client_desktop_mode
     */
    void set_client_desktop_mode(ClientDesktopMode desktop_mode);
    /**
     * @param applications_mode The new ClientApplicationsMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see client_applications_mode
     */
    void set_client_applications_mode(ClientApplicationsMode applications_mode);
    /**
     * @param minimized_mode The new ClientMinimizedMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see client_minimized_mode
     */
    void set_client_minimized_mode(ClientMinimizedMode minimized_mode);
    /**
     * @param show_desktop_mode The new ShowDesktopMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see show_desktop_mode
     */
    void set_show_desktop_mode(ShowDesktopMode show_desktop_mode);
    /**
     * @param multi_screen_mode The new ClientMultiScreenMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see client_multi_screen_mode
     */
    void set_client_multi_screen_mode(ClientMultiScreenMode multi_screen_mode);
    /**
     * @param switching_mode The new ClientSwitchingMode to be used.
     * This option only applies for TabBoxMode ClientTabBox.
     * @see client_switching_mode
     */
    void set_client_switching_mode(ClientSwitchingMode switching_mode);
    /**
     * @param switching_mode The new DesktopSwitchingMode to be used.
     * This option only applies for TabBoxMode DesktopTabBox.
     * @see desktop_switching_mode
     */
    void set_desktop_switching_mode(DesktopSwitchingMode switching_mode);
    /**
     * @param name The new item_layout config name
     * @see layout_name
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
    tabbox_config_private* d{nullptr};
};

} // namespace TabBox
} // namespace KWin

#endif // TABBOXCONFIG_H
