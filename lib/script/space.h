/*
SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"
#include "window.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "base/platform.h"
#include "debug/support_info.h"
#include "kwin_export.h"
#include "win/activation.h"
#include "win/active_window.h"
#include "win/move.h"
#include "win/output_space.h"
#include "win/screen.h"
#include <kwin_export.h>
#include <win/subspace.h>
#include <win/subspace_manager.h>

#include <QObject>
#include <QQmlEngine>
#include <QQmlListProperty>
#include <QRect>
#include <QSize>
#include <QStringList>

#include <memory>
#include <vector>

namespace KWin::scripting
{

class KWIN_EXPORT space : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVector<KWin::win::subspace*> desktops READ desktops NOTIFY desktopsChanged)
    Q_PROPERTY(KWin::win::subspace* currentDesktop READ currentDesktop WRITE setCurrentDesktop
                   NOTIFY currentDesktopChanged)
    Q_PROPERTY(KWin::scripting::window* activeWindow READ activeWindow WRITE setActiveWindow NOTIFY
                   windowActivated)
    // TODO: write and notify?
    Q_PROPERTY(QSize desktopGridSize READ desktopGridSize NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int desktopGridWidth READ desktopGridWidth NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int desktopGridHeight READ desktopGridHeight NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int workspaceWidth READ workspaceWidth)
    Q_PROPERTY(int workspaceHeight READ workspaceHeight)
    Q_PROPERTY(QSize workspaceSize READ workspaceSize)

    Q_PROPERTY(KWin::scripting::output* activeScreen READ activeScreen)
    Q_PROPERTY(QList<KWin::scripting::output*> screens READ screens NOTIFY screensChanged)

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
    /**
     * List of Clients currently managed by KWin, orderd by
     * their visibility (later ones cover earlier ones).
     */
    Q_PROPERTY(QList<KWin::scripting::window*> stackingOrder READ stackingOrder)

public:
    //------------------------------------------------------------------
    // enums copy&pasted from win/types header because qtscript is evil

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

    virtual win::subspace* currentDesktop() const = 0;
    virtual void setCurrentDesktop(win::subspace* desktop) = 0;
    virtual QVector<KWin::win::subspace*> desktops() const = 0;

    Q_INVOKABLE output* screenAt(QPointF const& pos) const
    {
        return screen_at_impl(pos);
    }

    /// Deprecated
    QString currentActivity() const
    {
        return {};
    }
    void setCurrentActivity(QString /*activity*/)
    {
    }

    virtual window* activeWindow() const = 0;

    virtual void setActiveWindow(window* win) = 0;

    virtual QSize desktopGridSize() const = 0;
    int desktopGridWidth() const;
    int desktopGridHeight() const;
    int workspaceWidth() const;
    int workspaceHeight() const;
    QSize workspaceSize() const;
    int displayWidth() const;
    int displayHeight() const;
    virtual QSize displaySize() const = 0;
    virtual output* activeScreen() const = 0;
    virtual QList<output*> screens() const = 0;
    QStringList activityList() const;
    QSize virtualScreenSize() const;
    QRect virtualScreenGeometry() const;

    virtual std::vector<window*> get_windows() const = 0;

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
                                  scripting::output* output,
                                  win::subspace* desktop) const
    {
        return client_area_impl(static_cast<win::area_option>(option), output, desktop);
    }

    /**
     * Overloaded method for convenience.
     * @param client The Client for which the area should be retrieved
     * @returns The specified screen geometry
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option, KWin::scripting::window* window) const
    {
        return client_area_impl(static_cast<win::area_option>(option), window);
    }

    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option,
                                  KWin::scripting::window const* window) const
    {
        return client_area_impl(static_cast<win::area_option>(option), window);
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
     * List of Clients managed by KWin, orderd by their visibility (later ones cover earlier ones).
     */
    virtual QList<KWin::scripting::window*> stackingOrder() const = 0;

    /**
     * Raises a Window  above all others on the screen.
     * @param window The Window to raise
     */
    Q_INVOKABLE void raiseWindow(KWin::scripting::window* window)
    {
        raise_window_impl(window);
    }

    /**
     * Finds the Client with the given @p windowId.
     * @param windowId The window Id of the Client
     * @return The found Client or @c null
     */
    Q_SCRIPTABLE KWin::scripting::window* getClient(qulonglong windowId);

    /**
     * Finds up to count windows at a particular location, prioritizing the topmost one first.  A
     * negative count returns all matching clients.
     * @param pos The location to look for
     * @param count The number of clients to return
     * @return A list of Client objects
     */
    Q_INVOKABLE QList<KWin::scripting::window*> windowAt(QPointF const& pos, int count = 1) const
    {
        return window_at_impl(pos, count);
    }

    /**
     * Checks if a specific effect is currently active.
     * @param pluginId The plugin Id of the effect to check.
     * @return @c true if the effect is loaded and currently active, @c false otherwise.
     */
    Q_INVOKABLE bool isEffectActive(QString const& plugin_id) const
    {
        return is_effect_active_impl(plugin_id);
    }

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
    virtual void sendClientToScreen(KWin::scripting::window* client, scripting::output* output) = 0;

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
    void windowAdded(KWin::scripting::window*);
    void windowRemoved(KWin::scripting::window*);
    void windowActivated(KWin::scripting::window*);

    /// This signal is emitted when a virtual desktop is added or removed.
    void desktopsChanged();
    /**
     * Signal emitted whenever the layout of virtual desktops changed.
     * That is desktopGrid(Size/Width/Height) will have new values.
     * @since 4.11
     */
    void desktopLayoutChanged();
    void screensChanged();
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
    virtual output* screen_at_impl(QPointF const& pos) const = 0;

    virtual QRect client_area_impl(win::area_option option,
                                   scripting::output* output,
                                   win::subspace* desktop) const
        = 0;
    virtual QRect client_area_impl(win::area_option option, window* window) const = 0;
    virtual QRect client_area_impl(win::area_option option, window const* window) const = 0;

    virtual QString desktop_name_impl(int desktop) const = 0;
    virtual void create_desktop_impl(int position, QString const& name) const = 0;
    virtual void remove_desktop_impl(int position) const = 0;

    virtual void switch_desktop_next_impl() const = 0;
    virtual void switch_desktop_previous_impl() const = 0;
    virtual void switch_desktop_left_impl() const = 0;
    virtual void switch_desktop_right_impl() const = 0;
    virtual void switch_desktop_up_impl() const = 0;
    virtual void switch_desktop_down_impl() const = 0;

    virtual void raise_window_impl(scripting::window* window) = 0;
    virtual window* get_client_impl(qulonglong windowId) = 0;
    virtual QList<scripting::window*> window_at_impl(QPointF const& pos, int count) const = 0;
    virtual bool is_effect_active_impl(QString const& plugin_id) const = 0;

    // TODO: make this private. Remove dynamic inheritance?
    std::vector<std::unique_ptr<window>> m_windows;
    int windows_count{0};

private:
    Q_DISABLE_COPY(space)
};

class KWIN_EXPORT qt_script_space : public space
{
    Q_OBJECT

public:
    qt_script_space();
    ~qt_script_space() override;

    /// List of windows managed by KWin.
    Q_INVOKABLE QList<KWin::scripting::window*> windowList() const;
};

class KWIN_EXPORT declarative_script_space : public space
{
    Q_OBJECT
    Q_PROPERTY(QQmlListProperty<KWin::scripting::window> windows READ windows)

public:
    declarative_script_space() = default;

    QQmlListProperty<window> windows();
    static qsizetype countWindowList(QQmlListProperty<window>* windows);
    static window* atWindowList(QQmlListProperty<window>* windows, qsizetype index);
};

template<typename Space, typename RefSpace>
class template_space : public Space
{
public:
    using type = template_space<Space, RefSpace>;
    using window_t = window_impl<typename RefSpace::window_t, type>;
    using base_t = typename RefSpace::base_t;

    template_space(RefSpace* ref_space)
        : ref_space{ref_space}
    {
        using space_qobject = typename RefSpace::qobject_t;

        QObject::connect(
            ref_space->qobject.get(), &space_qobject::clientAdded, this, [this](auto win_id) {
                auto ref_win = this->ref_space->windows_map.at(win_id);
                std::visit(overload{[&, this](auto&& win) { handle_client_added(win); }}, ref_win);
            });
        QObject::connect(ref_space->qobject.get(),
                         &space_qobject::clientRemoved,
                         this,
                         [this](auto win_id) { handle_client_removed(win_id); });
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
                Q_EMIT Space::windowActivated(get_window(*act));
            }
        });

        auto& vds = ref_space->subspace_manager;
        QObject::connect(vds->qobject.get(),
                         &decltype(vds->qobject)::element_type::countChanged,
                         this,
                         &space::desktopsChanged);
        QObject::connect(vds->qobject.get(),
                         &decltype(vds->qobject)::element_type::layoutChanged,
                         this,
                         &space::desktopLayoutChanged);
        QObject::connect(vds->qobject.get(),
                         &decltype(vds->qobject)::element_type::current_changed,
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
        QObject::connect(&base, &base::platform::output_added, this, [this](auto output) {
            using out_t = typename RefSpace::base_t::output_t;
            auto& out = static_cast<out_t&>(*output);
            outputs.emplace_back(std::make_unique<output_impl<out_t>>(out));
            Q_EMIT Space::screensChanged();
        });
        QObject::connect(&base, &base::platform::output_removed, this, [this](auto output) {
            remove_all_if(outputs, [output](auto&& out) { return &out->ref_out == output; });
            Q_EMIT Space::screensChanged();
        });

        for (auto win : ref_space->windows) {
            std::visit(overload{[&](auto&& win) { handle_client_added(win); }}, win);
        }
    }

    win::subspace* currentDesktop() const override
    {
        return ref_space->subspace_manager->current;
    }

    QVector<KWin::win::subspace*> desktops() const override
    {
        QVector<win::subspace*> ret;
        auto const& subs = ref_space->subspace_manager->subspaces;
        std::copy(subs.begin(), subs.end(), std::back_inserter(ret));
        return ret;
    }

    void setCurrentDesktop(win::subspace* desktop) override
    {
        assert(desktop);
        win::subspaces_set_current(*ref_space->subspace_manager, *desktop);
    }

    std::vector<window*> get_windows() const override
    {
        std::vector<window*> ret;
        for (auto const& [key, win] : windows_map) {
            ret.push_back(win.get());
        }
        return ret;
    }

    window* activeWindow() const override
    {
        auto active_client = ref_space->stacking.active;
        if (!active_client) {
            return nullptr;
        }
        return get_window(*active_client);
    }

    void setActiveWindow(window* win) override
    {
        std::visit(overload{[this](auto&& ref_win) { win::activate_window(*ref_space, *ref_win); }},
                   static_cast<window_t*>(win)->client());
    }

    QSize desktopGridSize() const override
    {
        return ref_space->subspace_manager->grid.size();
    }

    QSize displaySize() const override
    {
        return ref_space->base.topology.size;
    }

    output* activeScreen() const override
    {
        auto output = win::get_current_output(*ref_space);
        return get_output(output);
    }

    QList<output*> screens() const override
    {
        QList<output*> ret;
        for (auto&& out : outputs) {
            ret.push_back(out.get());
        }
        return ret;
    }

    void sendClientToScreen(window* win, scripting::output* output) override
    {
        if (!output) {
            return;
        }
        auto out_impl = static_cast<output_impl<typename RefSpace::base_t::output_t>*>(output);
        std::visit(overload{[out_impl, this](auto&& ref_win) {
                       win::send_to_screen(*ref_space, ref_win, out_impl->ref_out);
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
        win::active_window_set_on_all_subspaces(*ref_space);
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
        win::active_window_to_next_subspace(*ref_space);
    }

    void slotWindowToPreviousDesktop() override
    {
        win::active_window_to_prev_subspace(*ref_space);
    }

    void slotWindowToDesktopRight() override
    {
        win::active_window_to_right_subspace(*ref_space);
    }

    void slotWindowToDesktopLeft() override
    {
        win::active_window_to_left_subspace(*ref_space);
    }

    void slotWindowToDesktopUp() override
    {
        win::active_window_to_above_subspace(*ref_space);
    }

    void slotWindowToDesktopDown() override
    {
        win::active_window_to_below_subspace(*ref_space);
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

    std::unordered_map<uint32_t, std::unique_ptr<window>> windows_map;

protected:
    output* screen_at_impl(QPointF const& pos) const override
    {
        auto output = base::get_nearest_output(ref_space->base.outputs, pos.toPoint());
        auto ret = get_output(output);
        QQmlEngine::setObjectOwnership(ret, QQmlEngine::CppOwnership);
        return ret;
    }

    QRect client_area_impl(win::area_option option,
                           scripting::output* output,
                           win::subspace* subspace) const override
    {
        if (!output) {
            return {};
        }
        auto out_impl = static_cast<output_impl<typename RefSpace::base_t::output_t>*>(output);
        return win::space_window_area(
            *ref_space, option, &out_impl->ref_out, subspace->x11DesktopNumber());
    }

    QRect client_area_impl(win::area_option option, window* win) const override
    {
        return std::visit(overload{[&, this](auto&& ref_win) {
                              return win::space_window_area(*ref_space, option, ref_win);
                          }},
                          static_cast<window_t*>(win)->client());
    }

    QRect client_area_impl(win::area_option option, window const* win) const override
    {
        return std::visit(overload{[&, this](auto&& ref_win) {
                              return win::space_window_area(*ref_space, option, ref_win);
                          }},
                          static_cast<window_t const*>(win)->client());
    }

    QString desktop_name_impl(int desktop) const override
    {
        return win::subspace_manager_get_subspace_name(*ref_space->subspace_manager, desktop);
    }
    void create_desktop_impl(int position, QString const& name) const override
    {
        win::subspace_manager_create_subspace(*ref_space->subspace_manager, position, name);
    }
    void remove_desktop_impl(int position) const override
    {
        if (auto vd = win::subspaces_get_for_x11id(*ref_space->subspace_manager, position + 1)) {
            win::subspace_manager_remove_subspace(*ref_space->subspace_manager, vd);
        }
    }

    void switch_desktop_next_impl() const override
    {
        auto& vdm = ref_space->subspace_manager;
        win::subspaces_set_current(*vdm, win::subspaces_get_successor_of_current(*vdm));
    }
    void switch_desktop_previous_impl() const override
    {
        auto& vdm = ref_space->subspace_manager;
        win::subspaces_set_current(*vdm, win::subspaces_get_predecessor_of_current(*vdm));
    }
    void switch_desktop_left_impl() const override
    {
        auto& vdm = ref_space->subspace_manager;
        win::subspaces_set_current(*vdm, win::subspaces_get_west_of_current(*vdm));
    }
    void switch_desktop_right_impl() const override
    {
        auto& vdm = ref_space->subspace_manager;
        win::subspaces_set_current(*vdm, win::subspaces_get_east_of_current(*vdm));
    }
    void switch_desktop_up_impl() const override
    {
        auto& vdm = ref_space->subspace_manager;
        win::subspaces_set_current(*vdm, win::subspaces_get_north_of_current(*vdm));
    }
    void switch_desktop_down_impl() const override
    {
        auto& vdm = ref_space->subspace_manager;
        win::subspaces_set_current(*vdm, win::subspaces_get_south_of_current(*vdm));
    }

    QString supportInformation() const override
    {
        return debug::get_support_info(*ref_space);
    }

    QList<KWin::scripting::window*> stackingOrder() const override
    {
        QList<KWin::scripting::window*> ret;
        for (auto&& win : ref_space->stacking.order.stack) {
            if (auto swin = get_window(win)) {
                ret << swin;
            }
        }
        return ret;
    }

    void raise_window_impl(KWin::scripting::window* window) override
    {
        std::visit(overload{[this](auto&& win) { win::raise_window(*ref_space, win); }},
                   static_cast<window_t*>(window)->client());
    }

    QList<KWin::scripting::window*> window_at_impl(QPointF const& pos, int count) const override
    {
        QList<KWin::scripting::window*> result;

        int found = 0;
        auto const& stacking = ref_space->stacking.order.stack;

        if (stacking.empty()) {
            return result;
        }

        auto it = stacking.end();

        do {
            if (found == count) {
                return result;
            }

            --it;

            std::visit(overload{[&](auto&& win) {
                           if (!win->control) {
                               return;
                           }
                           if (!win::on_current_subspace(*win) || win->control->minimized
                               || win->isHiddenInternal()) {
                               return;
                           }
                           if (win->geo.frame.contains(pos.toPoint())) {
                               result.append(get_window(win));
                               found++;
                           }
                       }},
                       *it);
        } while (it != stacking.begin());

        return result;
    }

    bool is_effect_active_impl(QString const& plugin_id) const override
    {
        if (auto& effects = ref_space->base.render->effects) {
            return effects->is_effect_active(plugin_id);
        }
        return false;
    }

    window* get_client_impl(qulonglong windowId) override
    {
        for (auto& win : ref_space->windows) {
            if (auto scr_win
                = std::visit(overload{[&](auto&& win) -> window* {
                                 if constexpr (requires(decltype(win) win) { win->xcb_windows; }) {
                                     if (win->xcb_windows.client == windowId
                                         && windows_map.contains(win->meta.signal_id)) {
                                         return windows_map.at(win->meta.signal_id).get();
                                     }
                                 }
                                 return nullptr;
                             }},
                             win)) {
                QQmlEngine::setObjectOwnership(scr_win, QQmlEngine::CppOwnership);
                return scr_win;
            }
        }
        return nullptr;
    }

    window* get_window(typename RefSpace::window_t win) const
    {
        auto id
            = std::visit(overload{[](auto&& win) -> uint32_t { return win->meta.signal_id; }}, win);
        if (!windows_map.contains(id)) {
            return nullptr;
        }

        return windows_map.at(id).get();
    }

    template<typename RefWin>
    void handle_client_added(RefWin* win)
    {
        if (!win->control) {
            // Only windows with control are made available to the scripting system.
            return;
        }

        auto key = win->meta.signal_id;
        windows_map.insert({key, std::make_unique<window_t>(win, *this)});

        Space::windows_count++;
        Q_EMIT Space::windowAdded(static_cast<window*>(windows_map.at(key).get()));
    }

    void handle_client_removed(uint32_t id)
    {
        if (!windows_map.contains(id)) {
            return;
        }

        Space::windows_count--;
        Q_EMIT Space::windowRemoved(static_cast<window*>(windows_map.at(id).get()));
        windows_map.erase(id);
    }

    output_impl<typename RefSpace::base_t::output_t>* get_output(base::output const* output) const
    {
        auto it = std::find_if(outputs.begin(), outputs.end(), [output](auto&& out) {
            return &out->ref_out == output;
        });
        if (it == outputs.end()) {
            return nullptr;
        }
        return it->get();
    }

    std::vector<std::unique_ptr<output_impl<typename RefSpace::base_t::output_t>>> outputs;
    RefSpace* ref_space;
};

}
