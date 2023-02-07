/*
SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"
#include "window.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "debug/support_info.h"
#include "kwin_export.h"
#include "win/activation.h"
#include "win/active_window.h"
#include "win/move.h"
#include "win/output_space.h"
#include "win/screen.h"
#include "win/virtual_desktops.h"

#include <QObject>
#include <QQmlListProperty>
#include <QRect>
#include <QSize>
#include <QStringList>

#include <kwinglobals.h>

#include <memory>
#include <vector>

namespace KWin::scripting
{

class KWIN_EXPORT space : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVector<KWin::win::virtual_desktop*> desktops READ desktops NOTIFY desktopsChanged)
    Q_PROPERTY(KWin::win::virtual_desktop* currentDesktop READ currentDesktop WRITE
                   setCurrentDesktop NOTIFY currentDesktopChanged)
    Q_PROPERTY(KWin::scripting::window* activeClient READ activeClient WRITE setActiveClient NOTIFY
                   clientActivated)
    // TODO: write and notify?
    Q_PROPERTY(QSize desktopGridSize READ desktopGridSize NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int desktopGridWidth READ desktopGridWidth NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int desktopGridHeight READ desktopGridHeight NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int workspaceWidth READ workspaceWidth)
    Q_PROPERTY(int workspaceHeight READ workspaceHeight)
    Q_PROPERTY(QSize workspaceSize READ workspaceSize)
    Q_PROPERTY(int activeScreen READ activeScreen)
    Q_PROPERTY(int numScreens READ numScreens NOTIFY numberScreensChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity WRITE setCurrentActivity NOTIFY
                   currentActivityChanged)
    Q_PROPERTY(QStringList activities READ activityList NOTIFY activitiesChanged)
    /**
     * The bounding size of all screens combined. Overlapping areas
     * are not counted multiple times.
     * @see virtualScreenGeometry
     */
    Q_PROPERTY(QSize virtualScreenSize READ virtualScreenSize NOTIFY virtualScreenSizeChanged)
    /**
     * The bounding geometry of all outputs combined. Always starts at (0,0) and has
     * virtualScreenSize as it's size.
     * @see virtualScreenSize
     */
    Q_PROPERTY(
        QRect virtualScreenGeometry READ virtualScreenGeometry NOTIFY virtualScreenGeometryChanged)

public:
    //------------------------------------------------------------------
    // enums copy&pasted from kwinglobals.h because qtscript is evil

    enum ClientAreaOption {
        ///< geometry where a window will be initially placed after being mapped
        PlacementArea,
        ///< window movement snapping area?  ignore struts
        MovementArea,
        ///< geometry to which a window will be maximized
        MaximizeArea,
        ///< like MaximizeArea, but ignore struts - used e.g. for topmenu
        MaximizeFullArea,
        ///< area for fullscreen windows
        FullScreenArea,
        ///< whole workarea (all screens together)
        WorkArea,
        ///< whole area (all screens together), ignore struts
        FullArea,
        ///< one whole screen, ignore struts
        ScreenArea
    };
    Q_ENUM(ClientAreaOption)

    enum ElectricBorder {
        ElectricTop,
        ElectricTopRight,
        ElectricRight,
        ElectricBottomRight,
        ElectricBottom,
        ElectricBottomLeft,
        ElectricLeft,
        ElectricTopLeft,
        ELECTRIC_COUNT,
        ElectricNone
    };
    Q_ENUM(ElectricBorder)

    virtual win::virtual_desktop* currentDesktop() const = 0;
    virtual void setCurrentDesktop(win::virtual_desktop* desktop) = 0;
    virtual QVector<KWin::win::virtual_desktop*> desktops() const = 0;

    Q_INVOKABLE int screenAt(const QPointF& pos) const;

    /// Deprecated
    QString currentActivity() const
    {
        return {};
    }
    void setCurrentActivity(QString /*activity*/)
    {
    }

    virtual window* activeClient() const = 0;

    virtual void setActiveClient(window* win) = 0;

    virtual QSize desktopGridSize() const = 0;
    int desktopGridWidth() const;
    int desktopGridHeight() const;
    int workspaceWidth() const;
    int workspaceHeight() const;
    QSize workspaceSize() const;
    int displayWidth() const;
    int displayHeight() const;
    virtual QSize displaySize() const = 0;
    virtual int activeScreen() const = 0;
    virtual int numScreens() const = 0;
    QStringList activityList() const;
    QSize virtualScreenSize() const;
    QRect virtualScreenGeometry() const;

    virtual std::vector<window*> windows() const = 0;

    /**
     * Returns the geometry a Client can use with the specified option.
     * This method should be preferred over other methods providing screen sizes as the
     * various options take constraints such as struts set on panels into account.
     * This method is also multi screen aware, but there are also options to get full areas.
     * @param option The type of area which should be considered
     * @param screen The screen for which the area should be considered
     * @param desktop The desktop for which the area should be considered, in general there should
     * not be a difference
     * @returns The specified screen geometry
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option,
                                  base::output* output,
                                  win::virtual_desktop* desktop) const
    {
        return client_area_impl(static_cast<clientAreaOption>(option), output, desktop);
    }

    /**
     * Overloaded method for convenience.
     * @param option The type of area which should be considered
     * @param point The coordinates which have to be included in the area
     * @param desktop The desktop for which the area should be considered, in general there should
     * not be a difference
     * @returns The specified screen geometry
     * @deprecated use clientArea(ClientAreaOption option, QPoint const& point,
     * KWin::win::virtual_desktop* desktop)
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option, QPoint const& point, int desktop) const
    {
        return client_area_impl(static_cast<clientAreaOption>(option), point, desktop);
    }
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option,
                                  QPoint const& point,
                                  win::virtual_desktop* desktop) const
    {
        return client_area_impl(
            static_cast<clientAreaOption>(option), point, desktop->x11DesktopNumber());
    }

    /**
     * Overloaded method for convenience.
     * @param client The Client for which the area should be retrieved
     * @returns The specified screen geometry
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option, KWin::scripting::window* window) const
    {
        return client_area_impl(static_cast<clientAreaOption>(option), window);
    }

    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option,
                                  KWin::scripting::window const* window) const
    {
        return client_area_impl(static_cast<clientAreaOption>(option), window);
    }

    /**
     * Returns the name for the given @p desktop.
     */
    Q_SCRIPTABLE QString desktopName(int desktop) const
    {
        return desktop_name_impl(desktop);
    }
    /**
     * Create a new virtual desktop at the requested position.
     * @param position The position of the desktop. It should be in range [0, count].
     * @param name The name for the new desktop, if empty the default name will be used.
     */
    Q_SCRIPTABLE void createDesktop(int position, QString const& name) const
    {
        return create_desktop_impl(position, name);
    }
    /**
     * Remove the virtual desktop at the requested position
     * @param position The position of the desktop to be removed. It should be in range [0, count -
     * 1].
     */
    Q_SCRIPTABLE void removeDesktop(int position) const
    {
        return remove_desktop_impl(position);
    }

    Q_SCRIPTABLE void slotSwitchDesktopNext() const
    {
        switch_desktop_next_impl();
    }
    Q_SCRIPTABLE void slotSwitchDesktopPrevious() const
    {
        switch_desktop_previous_impl();
    }
    Q_SCRIPTABLE void slotSwitchDesktopRight() const
    {
        switch_desktop_right_impl();
    }
    Q_SCRIPTABLE void slotSwitchDesktopLeft() const
    {
        switch_desktop_left_impl();
    }
    Q_SCRIPTABLE void slotSwitchDesktopUp() const
    {
        switch_desktop_up_impl();
    }
    Q_SCRIPTABLE void slotSwitchDesktopDown() const
    {
        switch_desktop_down_impl();
    }

    /**
     * Provides support information about the currently running KWin instance.
     */
    virtual Q_SCRIPTABLE QString supportInformation() const = 0;

    /**
     * Finds the Client with the given @p windowId.
     * @param windowId The window Id of the Client
     * @return The found Client or @c null
     */
    Q_SCRIPTABLE KWin::scripting::window* getClient(qulonglong windowId);

public Q_SLOTS:
    virtual void slotSwitchToNextScreen() = 0;
    virtual void slotWindowToNextScreen() = 0;
    virtual void slotToggleShowDesktop() = 0;

    virtual void slotWindowMaximize() = 0;
    virtual void slotWindowMaximizeVertical() = 0;
    virtual void slotWindowMaximizeHorizontal() = 0;
    virtual void slotWindowMinimize() = 0;

    /// Deprecated
    void slotWindowShade()
    {
    }

    virtual void slotWindowRaise() = 0;
    virtual void slotWindowLower() = 0;
    virtual void slotWindowRaiseOrLower() = 0;
    virtual void slotActivateAttentionWindow() = 0;

    virtual void slotWindowMoveLeft() = 0;
    virtual void slotWindowMoveRight() = 0;
    virtual void slotWindowMoveUp() = 0;
    virtual void slotWindowMoveDown() = 0;

    virtual void slotWindowExpandHorizontal() = 0;
    virtual void slotWindowExpandVertical() = 0;
    virtual void slotWindowShrinkHorizontal() = 0;
    virtual void slotWindowShrinkVertical() = 0;

    virtual void slotWindowQuickTileLeft() = 0;
    virtual void slotWindowQuickTileRight() = 0;
    virtual void slotWindowQuickTileTop() = 0;
    virtual void slotWindowQuickTileBottom() = 0;
    virtual void slotWindowQuickTileTopLeft() = 0;
    virtual void slotWindowQuickTileTopRight() = 0;
    virtual void slotWindowQuickTileBottomLeft() = 0;
    virtual void slotWindowQuickTileBottomRight() = 0;

    virtual void slotSwitchWindowUp() = 0;
    virtual void slotSwitchWindowDown() = 0;
    virtual void slotSwitchWindowRight() = 0;
    virtual void slotSwitchWindowLeft() = 0;

    virtual void slotIncreaseWindowOpacity() = 0;
    virtual void slotLowerWindowOpacity() = 0;

    virtual void slotWindowOperations() = 0;
    virtual void slotWindowClose() = 0;
    virtual void slotWindowMove() = 0;
    virtual void slotWindowResize() = 0;
    virtual void slotWindowAbove() = 0;
    virtual void slotWindowBelow() = 0;
    virtual void slotWindowOnAllDesktops() = 0;
    virtual void slotWindowFullScreen() = 0;
    virtual void slotWindowNoBorder() = 0;

    virtual void slotWindowToNextDesktop() = 0;
    virtual void slotWindowToPreviousDesktop() = 0;
    virtual void slotWindowToDesktopRight() = 0;
    virtual void slotWindowToDesktopLeft() = 0;
    virtual void slotWindowToDesktopUp() = 0;
    virtual void slotWindowToDesktopDown() = 0;

#undef QUICKTILE_SLOT
#undef SWITCH_VD_SLOT

    /**
     * Sends the window to the given @p screen.
     */
    virtual void sendClientToScreen(KWin::scripting::window* client, int screen) = 0;

    /**
     * Shows an outline at the specified @p geometry.
     * If an outline is already shown the outline is moved to the new position.
     * Use hideOutline to remove the outline again.
     */
    virtual void showOutline(QRect const& geometry) = 0;
    /**
     * Overloaded method for convenience.
     */
    void showOutline(int x, int y, int width, int height);
    /**
     * Hides the outline previously shown by showOutline.
     */
    virtual void hideOutline() = 0;

Q_SIGNALS:
    void desktopPresenceChanged(KWin::scripting::window* client, int desktop);
    void clientAdded(KWin::scripting::window* client);
    void clientRemoved(KWin::scripting::window* client);

    void clientMinimized(KWin::scripting::window* client);
    void clientUnminimized(KWin::scripting::window* client);
    void clientRestored(KWin::scripting::window* client);
    void clientMaximizeSet(KWin::scripting::window* client, bool h, bool v);
    void killWindowCalled(KWin::scripting::window* client);
    void clientActivated(KWin::scripting::window* client);
    void clientSetKeepAbove(KWin::scripting::window* client, bool keepAbove);

    /// This signal is emitted when a virtual desktop is added or removed.
    void desktopsChanged();
    /**
     * Signal emitted whenever the layout of virtual desktops changed.
     * That is desktopGrid(Size/Width/Height) will have new values.
     * @since 4.11
     */
    void desktopLayoutChanged();
    /**
     * The demands attention state for Client @p c changed to @p set.
     * @param c The Client for which demands attention changed
     * @param set New value of demands attention
     */
    void clientDemandsAttentionChanged(KWin::scripting::window* window, bool set);
    /**
     * Signal emitted when the number of screens changes.
     * @param count The new number of screens
     */
    void numberScreensChanged(int count);
    /**
     * Signal emitted whenever the current activity changed.
     * @param id id of the new activity
     */
    void currentActivityChanged(const QString& id);
    /**
     * Signal emitted whenever the list of activities changed.
     * @param id id of the new activity
     */
    void activitiesChanged(const QString& id);
    /**
     * This signal is emitted when a new activity is added
     * @param id id of the new activity
     */
    void activityAdded(const QString& id);
    /**
     * This signal is emitted when the activity
     * is removed
     * @param id id of the removed activity
     */
    void activityRemoved(const QString& id);
    /**
     * Emitted whenever the virtualScreenSize changes.
     * @see virtualScreenSize()
     * @since 5.0
     */
    void virtualScreenSizeChanged();
    /**
     * Emitted whenever the virtualScreenGeometry changes.
     * @see virtualScreenGeometry()
     * @since 5.0
     */
    void virtualScreenGeometryChanged();
    void currentDesktopChanged();

protected:
    space() = default;

    virtual QRect client_area_impl(clientAreaOption option, int screen, int desktop) const = 0;
    virtual QRect client_area_impl(clientAreaOption option,
                                   base::output* output,
                                   win::virtual_desktop* desktop) const
        = 0;
    virtual QRect client_area_impl(clientAreaOption option, QPoint const& point, int desktop) const
        = 0;
    virtual QRect client_area_impl(clientAreaOption option, window* window) const = 0;
    virtual QRect client_area_impl(clientAreaOption option, window const* window) const = 0;

    virtual QString desktop_name_impl(int desktop) const = 0;
    virtual void create_desktop_impl(int position, QString const& name) const = 0;
    virtual void remove_desktop_impl(int position) const = 0;

    virtual void switch_desktop_next_impl() const = 0;
    virtual void switch_desktop_previous_impl() const = 0;
    virtual void switch_desktop_left_impl() const = 0;
    virtual void switch_desktop_right_impl() const = 0;
    virtual void switch_desktop_up_impl() const = 0;
    virtual void switch_desktop_down_impl() const = 0;

    virtual window* get_client_impl(qulonglong windowId) = 0;

    // TODO: make this private. Remove dynamic inheritance?
    std::vector<std::unique_ptr<window>> m_windows;
    int windows_count{0};

protected:
    void setupAbstractClientConnections(window* window);

private:
    Q_DISABLE_COPY(space)
};

class KWIN_EXPORT qt_script_space : public space
{
    Q_OBJECT

public:
    qt_script_space();
    ~qt_script_space() override;

    /**
     * List of Clients currently managed by KWin.
     */
    Q_INVOKABLE QList<KWin::scripting::window*> clientList() const;
};

class KWIN_EXPORT declarative_script_space : public space
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<KWin::scripting::window> clients READ clients)

public:
    declarative_script_space() = default;

    QQmlListProperty<KWin::scripting::window> clients();
    static int countClientList(QQmlListProperty<KWin::scripting::window>* clients);
    static window* atClientList(QQmlListProperty<KWin::scripting::window>* clients, int index);
};

template<typename Space, typename RefSpace>
class template_space : public Space
{
public:
    using window_t = window_impl<typename RefSpace::window_t>;
    using base_t = typename RefSpace::base_t;

    template_space(RefSpace* ref_space)
        : ref_space{ref_space}
    {
        using space_qobject = typename RefSpace::qobject_t;

        QObject::connect(ref_space->qobject.get(),
                         &space_qobject::desktopPresenceChanged,
                         this,
                         [this](auto win_id, auto desktop) {
                             auto ref_win = this->ref_space->windows_map.at(win_id);
                             auto window = get_window(ref_win);
                             Q_EMIT Space::desktopPresenceChanged(window, desktop);
                         });

        QObject::connect(
            ref_space->qobject.get(), &space_qobject::clientAdded, this, [this](auto win_id) {
                auto ref_win = this->ref_space->windows_map.at(win_id);
                std::visit(overload{[this](auto&& win) { handle_client_added(win); }}, ref_win);
            });
        QObject::connect(
            ref_space->qobject.get(), &space_qobject::clientRemoved, this, [this](auto win_id) {
                auto ref_win = this->ref_space->windows_map.at(win_id);
                std::visit(overload{[this](auto&& win) { handle_client_removed(win); }}, ref_win);
            });
        QObject::connect(ref_space->qobject.get(),
                         &space_qobject::wayland_window_added,
                         this,
                         [this](auto win_id) {
                             auto ref_win = this->ref_space->windows_map.at(win_id);
                             std::visit(overload{[&](auto&& win) { handle_client_added(win); }},
                                        ref_win);
                         });

        QObject::connect(ref_space->qobject.get(), &space_qobject::clientActivated, this, [this] {
            if (auto act = this->ref_space->stacking.active) {
                Q_EMIT Space::clientActivated(get_window(*act));
            }
        });

        QObject::connect(ref_space->qobject.get(),
                         &space_qobject::clientDemandsAttentionChanged,
                         this,
                         [this](auto win_id, auto set) {
                             auto ref_win = this->ref_space->windows_map.at(win_id);
                             auto window = get_window(ref_win);
                             Q_EMIT Space::clientDemandsAttentionChanged(window, set);
                         });

        auto& vds = ref_space->virtual_desktop_manager;
        QObject::connect(vds->qobject.get(),
                         &win::virtual_desktop_manager_qobject::countChanged,
                         this,
                         &space::desktopsChanged);
        QObject::connect(vds->qobject.get(),
                         &win::virtual_desktop_manager_qobject::layoutChanged,
                         this,
                         &space::desktopLayoutChanged);
        QObject::connect(vds->qobject.get(),
                         &win::virtual_desktop_manager_qobject::currentChanged,
                         this,
                         &space::currentDesktopChanged);

        auto& base = ref_space->base;
        QObject::connect(
            &base, &base::platform::topology_changed, this, [this](auto old_topo, auto new_topo) {
                if (old_topo.size != new_topo.size) {
                    Q_EMIT this->virtualScreenSizeChanged();
                    Q_EMIT this->virtualScreenGeometryChanged();
                }
            });
        QObject::connect(&base, &base::platform::output_added, this, [this] {
            Q_EMIT Space::numberScreensChanged(this->ref_space->base.outputs.size());
        });
        QObject::connect(&base, &base::platform::output_removed, this, [this] {
            Q_EMIT Space::numberScreensChanged(this->ref_space->base.outputs.size());
        });

        for (auto win : ref_space->windows) {
            std::visit(overload{[&](auto&& win) { handle_client_added(win); }}, win);
        }
    }

    win::virtual_desktop* currentDesktop() const override
    {
        return ref_space->virtual_desktop_manager->currentDesktop();
    }

    QVector<KWin::win::virtual_desktop*> desktops() const override
    {
        return ref_space->virtual_desktop_manager->desktops();
    }

    void setCurrentDesktop(win::virtual_desktop* desktop) override
    {
        ref_space->virtual_desktop_manager->setCurrent(desktop);
    }

    std::vector<window*> windows() const override
    {
        std::vector<window*> ret;
        for (auto const& win : ref_space->windows) {
            std::visit(overload{[&](auto&& win) {
                           if (win->control && win->control->scripting) {
                               ret.push_back(win->control->scripting.get());
                           }
                       }},
                       win);
        }
        return ret;
    }

    window* activeClient() const override
    {
        auto active_client = ref_space->stacking.active;
        if (!active_client) {
            return nullptr;
        }
        return get_window(*active_client);
    }

    void setActiveClient(window* win) override
    {
        std::visit(overload{[this](auto&& ref_win) { win::activate_window(*ref_space, *ref_win); }},
                   static_cast<window_t*>(win)->client());
    }

    QSize desktopGridSize() const override
    {
        return ref_space->virtual_desktop_manager->grid().size();
    }

    QSize displaySize() const override
    {
        return ref_space->base.topology.size;
    }

    int activeScreen() const override
    {
        auto output = win::get_current_output(*ref_space);
        if (!output) {
            return 0;
        }
        return base::get_output_index(ref_space->base.outputs, *output);
    }

    int numScreens() const override
    {
        return ref_space->base.outputs.size();
    }

    void sendClientToScreen(window* win, int screen) override
    {
        auto output = base::get_output(ref_space->base.outputs, screen);
        if (!output) {
            return;
        }
        std::visit(overload{[output, this](auto&& ref_win) {
                       win::send_to_screen(*ref_space, ref_win, *output);
                   }},
                   static_cast<window_t*>(win)->client());
    }

    void showOutline(QRect const& geometry) override
    {
        ref_space->outline->show(geometry);
    }

    void hideOutline() override
    {
        ref_space->outline->hide();
    }

    void slotSwitchToNextScreen() override
    {
        win::switch_to_next_output(*ref_space);
    }

    void slotWindowToNextScreen() override
    {
        win::active_window_to_next_output(*ref_space);
    }

    void slotToggleShowDesktop() override
    {
        win::toggle_show_desktop(*ref_space);
    }

    void slotWindowMaximize() override
    {
        win::active_window_maximize(*ref_space);
    }

    void slotWindowMaximizeVertical() override
    {
        win::active_window_maximize_vertical(*ref_space);
    }

    void slotWindowMaximizeHorizontal() override
    {
        win::active_window_maximize_horizontal(*ref_space);
    }

    void slotWindowMinimize() override
    {
        win::active_window_minimize(*ref_space);
    }

    void slotWindowRaise() override
    {
        win::active_window_raise(*ref_space);
    }

    void slotWindowLower() override
    {
        win::active_window_lower(*ref_space);
    }

    void slotWindowRaiseOrLower() override
    {
        win::active_window_raise_or_lower(*ref_space);
    }

    void slotActivateAttentionWindow() override
    {
        win::activate_attention_window(*ref_space);
    }

    void slotWindowMoveLeft() override
    {
        win::active_window_pack_left(*ref_space);
    }

    void slotWindowMoveRight() override
    {
        win::active_window_pack_right(*ref_space);
    }

    void slotWindowMoveUp() override
    {
        win::active_window_pack_up(*ref_space);
    }

    void slotWindowMoveDown() override
    {
        win::active_window_pack_down(*ref_space);
    }

    void slotWindowExpandHorizontal() override
    {
        win::active_window_grow_horizontal(*ref_space);
    }

    void slotWindowExpandVertical() override
    {
        win::active_window_grow_vertical(*ref_space);
    }

    void slotWindowShrinkHorizontal() override
    {
        win::active_window_shrink_horizontal(*ref_space);
    }

    void slotWindowShrinkVertical() override
    {
        win::active_window_shrink_vertical(*ref_space);
    }

    void slotIncreaseWindowOpacity() override
    {
        win::active_window_increase_opacity(*ref_space);
    }

    void slotLowerWindowOpacity() override
    {
        win::active_window_lower_opacity(*ref_space);
    }

    void slotWindowOperations() override
    {
        win::active_window_show_operations_popup(*ref_space);
    }

    void slotWindowClose() override
    {
        win::active_window_close(*ref_space);
    }

    void slotWindowMove() override
    {
        win::active_window_move(*ref_space);
    }

    void slotWindowResize() override
    {
        win::active_window_resize(*ref_space);
    }

    void slotWindowAbove() override
    {
        win::active_window_set_keep_above(*ref_space);
    }

    void slotWindowBelow() override
    {
        win::active_window_set_keep_below(*ref_space);
    }

    void slotWindowOnAllDesktops() override
    {
        win::active_window_set_on_all_desktops(*ref_space);
    }

    void slotWindowFullScreen() override
    {
        win::active_window_set_fullscreen(*ref_space);
    }

    void slotWindowNoBorder() override
    {
        win::active_window_set_no_border(*ref_space);
    }

    void slotWindowToNextDesktop() override
    {
        win::active_window_to_next_desktop(*ref_space);
    }

    void slotWindowToPreviousDesktop() override
    {
        win::active_window_to_prev_desktop(*ref_space);
    }

    void slotWindowToDesktopRight() override
    {
        win::active_window_to_right_desktop(*ref_space);
    }

    void slotWindowToDesktopLeft() override
    {
        win::active_window_to_left_desktop(*ref_space);
    }

    void slotWindowToDesktopUp() override
    {
        win::active_window_to_above_desktop(*ref_space);
    }

    void slotWindowToDesktopDown() override
    {
        win::active_window_to_below_desktop(*ref_space);
    }

    void slotWindowQuickTileLeft() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::left);
    }
    void slotWindowQuickTileRight() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::right);
    }
    void slotWindowQuickTileTop() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::top);
    }
    void slotWindowQuickTileBottom() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::bottom);
    }
    void slotWindowQuickTileTopLeft() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::top | win::quicktiles::left);
    }
    void slotWindowQuickTileTopRight() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::top | win::quicktiles::right);
    }
    void slotWindowQuickTileBottomLeft() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::bottom | win::quicktiles::left);
    }
    void slotWindowQuickTileBottomRight() override
    {
        win::active_window_quicktile(*ref_space, win::quicktiles::bottom | win::quicktiles::right);
    }

    void slotSwitchWindowUp() override
    {
        win::activate_window_direction(*ref_space, win::direction::north);
    }
    void slotSwitchWindowDown() override
    {
        win::activate_window_direction(*ref_space, win::direction::south);
    }
    void slotSwitchWindowRight() override
    {
        win::activate_window_direction(*ref_space, win::direction::east);
    }
    void slotSwitchWindowLeft() override
    {
        win::activate_window_direction(*ref_space, win::direction::west);
    }

protected:
    QRect client_area_impl(clientAreaOption option, int screen, int desktop) const override
    {
        auto output = base::get_output(ref_space->base.outputs, screen);
        return win::space_window_area(*ref_space, option, output, desktop);
    }

    QRect client_area_impl(clientAreaOption option,
                           base::output* client_output,
                           win::virtual_desktop* desktop) const override
    {
        typename base_t::output_t const* output
            = static_cast<typename base_t::output_t*>(client_output);
        return win::space_window_area(*ref_space, option, output, desktop->x11DesktopNumber());
    }

    QRect client_area_impl(clientAreaOption option, QPoint const& point, int desktop) const override
    {
        return win::space_window_area(*ref_space, option, point, desktop);
    }

    QRect client_area_impl(clientAreaOption option, window* win) const override
    {
        return std::visit(overload{[&, this](auto&& ref_win) {
                              return win::space_window_area(*ref_space, option, ref_win);
                          }},
                          static_cast<window_t*>(win)->client());
    }

    QRect client_area_impl(clientAreaOption option, window const* win) const override
    {
        return std::visit(overload{[&, this](auto&& ref_win) {
                              return win::space_window_area(*ref_space, option, ref_win);
                          }},
                          static_cast<window_t const*>(win)->client());
    }

    QString desktop_name_impl(int desktop) const override
    {
        return ref_space->virtual_desktop_manager->name(desktop);
    }
    void create_desktop_impl(int position, QString const& name) const override
    {
        ref_space->virtual_desktop_manager->createVirtualDesktop(position, name);
    }
    void remove_desktop_impl(int position) const override
    {
        if (auto vd = ref_space->virtual_desktop_manager->desktopForX11Id(position + 1)) {
            ref_space->virtual_desktop_manager->removeVirtualDesktop(vd->id());
        }
    }

    template<typename Direction>
    void switch_desktop() const
    {
        ref_space->virtual_desktop_manager->template moveTo<Direction>(
            ref_space->base.options->qobject->isRollOverDesktops());
    }
    void switch_desktop_next_impl() const override
    {
        switch_desktop<win::virtual_desktop_next>();
    }
    void switch_desktop_previous_impl() const override
    {
        switch_desktop<win::virtual_desktop_previous>();
    }
    void switch_desktop_left_impl() const override
    {
        switch_desktop<win::virtual_desktop_left>();
    }
    void switch_desktop_right_impl() const override
    {
        switch_desktop<win::virtual_desktop_right>();
    }
    void switch_desktop_up_impl() const override
    {
        switch_desktop<win::virtual_desktop_above>();
    }
    void switch_desktop_down_impl() const override
    {
        switch_desktop<win::virtual_desktop_below>();
    }

    QString supportInformation() const override
    {
        return debug::get_support_info(*ref_space);
    }

    window* get_client_impl(qulonglong windowId) override
    {
        for (auto& win : ref_space->windows) {
            if (auto scr_win
                = std::visit(overload{[&](auto&& win) -> window* {
                                 if constexpr (requires(decltype(win) win) { win->xcb_windows; }) {
                                     if (win->control && win->xcb_windows.client == windowId) {
                                         return win->control->scripting.get();
                                     }
                                 }
                                 return nullptr;
                             }},
                             win)) {
                return scr_win;
            }
        }
        return nullptr;
    }

    static window* get_window(typename RefSpace::window_t win)
    {
        return std::visit(overload{[](auto&& win) -> window* {
                              if (!win->control) {
                                  return nullptr;
                              }
                              return win->control->scripting.get();
                          }},
                          win);
    }

    template<typename RefWin>
    void handle_client_added(RefWin* win)
    {
        if (!win->control) {
            // Only windows with control are made available to the scripting system.
            return;
        }

        win->control->scripting = std::make_unique<window_t>(win);
        auto scr_win = win->control->scripting.get();

        Space::setupAbstractClientConnections(scr_win);
        Space::windows_count++;
        Q_EMIT Space::clientAdded(scr_win);
    }

    template<typename RefWin>
    void handle_client_removed(RefWin* client)
    {
        if (client->control) {
            Space::windows_count--;
            Q_EMIT Space::clientRemoved(client->control->scripting.get());
        }
    }

    RefSpace* ref_space;
};

}
