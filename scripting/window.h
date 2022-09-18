/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor.h"
#include "kwin_export.h"
#include "win/actions.h"
#include "win/activation.h"
#include "win/controlling.h"
#include "win/desktop_get.h"
#include "win/meta.h"
#include "win/property_window.h"
#include "win/screen.h"
#include "win/transient.h"

namespace KWin::scripting
{

class KWIN_EXPORT window : public win::property_window
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

    /// @deprecated. Use frameGeometry instead.
    Q_PROPERTY(QRect geometry READ frameGeometry WRITE setFrameGeometry NOTIFY geometryChanged)

    /// @deprecated
    Q_PROPERTY(QStringList activities READ activities NOTIFY activitiesChanged)

    /// @deprecated
    Q_PROPERTY(bool shade READ isShade WRITE setShade NOTIFY shadeChanged)

    Q_PROPERTY(window* transientFor READ transientFor NOTIFY transientChanged)

    // TODO: Should this not also hold true for Wayland windows? The name is misleading.
    //       Wayland windows (with xdg-toplevel role) are also "managed" by the compositor.
    Q_PROPERTY(bool managed READ isClient CONSTANT)

    /**
     * X11 only properties
     */
    Q_PROPERTY(bool blocksCompositing READ isBlockingCompositing WRITE setBlockingCompositing NOTIFY
                   blockingCompositingChanged)

public:
    explicit window(win::window_qobject& qtwin);

    virtual bool isOnDesktop(unsigned int desktop) const = 0;
    virtual bool isOnCurrentDesktop() const = 0;

    QStringList activities() const;
    bool isShadeable() const;
    bool isShade() const;
    void setShade(bool set);

    window* transientFor() const override = 0;
    virtual bool isClient() const = 0;

Q_SIGNALS:
    void quickTileModeChanged();

    void moveResizeCursorChanged(input::cursor_shape);
    void clientStartUserMovedResized(KWin::scripting::window* window);
    void clientStepUserMovedResized(KWin::scripting::window* window, const QRect&);
    void clientFinishUserMovedResized(KWin::scripting::window* window);

    void closeableChanged(bool);
    void minimizeableChanged(bool);
    void shadeableChanged(bool);
    void maximizeableChanged(bool);

    void opacityChanged(KWin::scripting::window* client, qreal old_opacity);

    void activitiesChanged(KWin::scripting::window* client);

    void shadeChanged();

    void desktopPresenceChanged(KWin::scripting::window* window, int);

    void paletteChanged(const QPalette& p);

    void blockingCompositingChanged(KWin::scripting::window* window);

    void clientMinimized(KWin::scripting::window* window);
    void clientUnminimized(KWin::scripting::window* window);

    void
    clientMaximizedStateChanged(KWin::scripting::window* window, bool horizontal, bool vertical);

    /// Deprecated
    void clientManaging(KWin::scripting::window* window);

    /// Deprecated
    void clientFullScreenSet(KWin::scripting::window* window, bool fullscreen, bool user);

    // TODO: this signal is never emitted - remove?
    void clientMaximizeSet(KWin::scripting::window* window, bool horizontal, bool vertical);
};

template<typename RefWin>
class window_impl : public window
{
public:
    window_impl(RefWin* ref_win)
        : window(*ref_win->qobject)
        , m_client{ref_win}
    {
        auto qtwin = get_window_qobject();
        QObject::connect(qtwin,
                         &win::window_qobject::opacityChanged,
                         this,
                         [this](auto oldOpacity) { Q_EMIT opacityChanged(this, oldOpacity); });

        QObject::connect(qtwin,
                         &win::window_qobject::desktopPresenceChanged,
                         this,
                         [this](auto desktop) { Q_EMIT desktopPresenceChanged(this, desktop); });

        QObject::connect(qtwin, &win::window_qobject::clientMinimized, this, [this] {
            Q_EMIT clientMinimized(this);
        });
        QObject::connect(qtwin, &win::window_qobject::clientUnminimized, this, [this] {
            Q_EMIT clientUnminimized(this);
        });

        QObject::connect(
            qtwin, &win::window_qobject::maximize_mode_changed, this, [this](auto mode) {
                Q_EMIT clientMaximizedStateChanged(this,
                                                   flags(mode & win::maximize_mode::horizontal),
                                                   flags(mode & win::maximize_mode::vertical));
            });

        QObject::connect(qtwin,
                         &win::window_qobject::quicktiling_changed,
                         this,
                         &window_impl::quickTileModeChanged);

        QObject::connect(
            qtwin, &win::window_qobject::paletteChanged, this, &window_impl::paletteChanged);
        QObject::connect(qtwin,
                         &win::window_qobject::moveResizeCursorChanged,
                         this,
                         &window_impl::moveResizeCursorChanged);
        QObject::connect(qtwin, &win::window_qobject::clientStartUserMovedResized, this, [this] {
            Q_EMIT clientStartUserMovedResized(this);
        });
        QObject::connect(qtwin,
                         &win::window_qobject::clientStepUserMovedResized,
                         this,
                         [this](auto rect) { Q_EMIT clientStepUserMovedResized(this, rect); });
        QObject::connect(qtwin, &win::window_qobject::clientFinishUserMovedResized, this, [this] {
            Q_EMIT clientFinishUserMovedResized(this);
        });

        QObject::connect(
            qtwin, &win::window_qobject::closeableChanged, this, &window_impl::closeableChanged);
        QObject::connect(qtwin,
                         &win::window_qobject::minimizeableChanged,
                         this,
                         &window_impl::minimizeableChanged);
        QObject::connect(qtwin,
                         &win::window_qobject::maximizeableChanged,
                         this,
                         &window_impl::maximizeableChanged);

        // For backwards compatibility of scripts connecting to the old signal. We assume no script
        // is actually differentiating its behavior on the user parameter (if fullscreen was
        // triggered by the user or not) and always set it to being a user change.
        QObject::connect(qtwin, &win::window_qobject::fullScreenChanged, this, [this, ref_win] {
            Q_EMIT clientFullScreenSet(this, ref_win->control->fullscreen, true);
        });

        if (ref_win->isClient()) {
            QObject::connect(qtwin,
                             &win::window_qobject::blockingCompositingChanged,
                             this,
                             [this](auto /*block*/) {
                                 // TODO(romangg): Should we emit null if block is false?
                                 Q_EMIT blockingCompositingChanged(this);
                             });
        }
    }

    xcb_window_t frameId() const override
    {
        return m_client->frameId();
    }

    quint32 windowId() const override
    {
        return m_client->xcb_window;
    }

    QByteArray resourceName() const override
    {
        return m_client->resource_name;
    }

    QByteArray resourceClass() const override
    {
        return m_client->resource_class;
    }

    QString caption() const override
    {
        return win::caption(m_client);
    }

    QIcon icon() const override
    {
        return m_client->control->icon;
    }

    QRect iconGeometry() const override
    {
        return m_client->iconGeometry();
    }

    QUuid internalId() const override
    {
        return m_client->internal_id;
    }

    pid_t pid() const override
    {
        return m_client->pid();
    }

    QRect bufferGeometry() const override
    {
        return win::render_geometry(m_client);
    }

    QRect frameGeometry() const override
    {
        return m_client->frameGeometry();
    }

    void setFrameGeometry(QRect const& geo) override
    {
        m_client->setFrameGeometry(geo);
    }

    QPoint pos() const override
    {
        return m_client->pos();
    }

    QRect rect() const override
    {
        return QRect(QPoint(0, 0), m_client->size());
    }

    QRect visibleRect() const override
    {
        return win::visible_rect(m_client);
    }

    QSize size() const override
    {
        return m_client->size();
    }

    QSize minSize() const override
    {
        return m_client->minSize();
    }

    QSize maxSize() const override
    {
        return m_client->maxSize();
    }

    QPoint clientPos() const override
    {
        return win::frame_relative_client_rect(m_client).topLeft();
    }

    QSize clientSize() const override
    {
        return win::frame_to_client_size(m_client, m_client->size());
    }

    int x() const override
    {
        return m_client->pos().x();
    }

    int y() const override
    {
        return m_client->pos().y();
    }

    int width() const override
    {
        return m_client->size().width();
    }

    int height() const override
    {
        return m_client->size().height();
    }

    bool isMove() const override
    {
        return win::is_move(m_client);
    }

    bool isResize() const override
    {
        return win::is_resize(m_client);
    }

    bool hasAlpha() const override
    {
        return m_client->hasAlpha();
    }

    qreal opacity() const override
    {
        return m_client->opacity();
    }

    void setOpacity(qreal opacity) override
    {
        m_client->setOpacity(opacity);
    }

    bool isFullScreen() const override
    {
        return m_client->control->fullscreen;
    }

    void setFullScreen(bool set) override
    {
        m_client->setFullScreen(set);
    }

    int screen() const override
    {
        if (!m_client->central_output) {
            return 0;
        }
        return base::get_output_index(m_client->space.base.outputs, *m_client->central_output);
    }

    int desktop() const override
    {
        return m_client->desktop();
    }

    void setDesktop(int desktop) override
    {
        win::set_desktop(m_client, desktop);
    }

    QVector<uint> x11DesktopIds() const override
    {
        return win::x11_desktop_ids(m_client);
    }

    bool isOnAllDesktops() const override
    {
        return win::on_all_desktops(m_client);
    }

    void setOnAllDesktops(bool set) override
    {
        win::set_on_all_desktops(m_client, set);
    }

    bool isOnDesktop(unsigned int desktop) const override
    {
        return win::on_desktop(m_client, desktop);
    }

    bool isOnCurrentDesktop() const override
    {
        return win::on_current_desktop(m_client);
    }

    QByteArray windowRole() const override
    {
        return m_client->windowRole();
    }

    NET::WindowType windowType() const override
    {
        return m_client->windowType();
    }

    bool isDesktop() const override
    {
        return win::is_desktop(m_client);
    }

    bool isDock() const override
    {
        return win::is_dock(m_client);
    }

    bool isToolbar() const override
    {
        return win::is_toolbar(m_client);
    }

    bool isMenu() const override
    {
        return win::is_menu(m_client);
    }

    bool isNormalWindow() const override
    {
        return win::is_normal(m_client);
    }

    bool isDialog() const override
    {
        return win::is_dialog(m_client);
    }

    bool isSplash() const override
    {
        return win::is_splash(m_client);
    }

    bool isUtility() const override
    {
        return win::is_utility(m_client);
    }

    bool isDropdownMenu() const override
    {
        return win::is_dropdown_menu(m_client);
    }

    bool isPopupMenu() const override
    {
        return win::is_popup_menu(m_client);
    }

    bool isTooltip() const override
    {
        return win::is_tooltip(m_client);
    }

    bool isNotification() const override
    {
        return win::is_notification(m_client);
    }

    bool isCriticalNotification() const override
    {
        return win::is_critical_notification(m_client);
    }

    bool isAppletPopup() const override
    {
        return win::is_applet_popup(m_client);
    }

    bool isOnScreenDisplay() const override
    {
        return win::is_on_screen_display(m_client);
    }

    bool isComboBox() const override
    {
        return win::is_combo_box(m_client);
    }

    bool isDNDIcon() const override
    {
        return win::is_dnd_icon(m_client);
    }

    bool isPopupWindow() const override
    {
        return win::is_popup(m_client);
    }

    bool isSpecialWindow() const override
    {
        return win::is_special_window(m_client);
    }

    bool isCloseable() const override
    {
        return m_client->isCloseable();
    }

    bool isMovable() const override
    {
        return m_client->isMovable();
    }

    bool isMovableAcrossScreens() const override
    {
        return m_client->isMovableAcrossScreens();
    }

    bool isResizable() const override
    {
        return m_client->isResizable();
    }

    bool isMinimizable() const override
    {
        return m_client->isMinimizable();
    }

    bool isMaximizable() const override
    {
        return m_client->isMaximizable();
    }

    bool isFullScreenable() const override
    {
        return m_client->control->can_fullscreen();
    }

    bool isOutline() const override
    {
        return m_client->is_outline;
    }

    bool isShape() const override
    {
        return m_client->is_shape;
    }

    bool keepAbove() const override
    {
        return m_client->control->keep_above;
    }

    void setKeepAbove(bool set) override
    {
        win::set_keep_above(m_client, set);
    }

    bool keepBelow() const override
    {
        return m_client->control->keep_below;
    }

    void setKeepBelow(bool set) override
    {
        win::set_keep_below(m_client, set);
    }

    bool isMinimized() const override
    {
        return m_client->control->minimized;
    }

    void setMinimized(bool set) override
    {
        win::set_minimized(m_client, set);
    }

    bool skipTaskbar() const override
    {
        return m_client->control->skip_taskbar();
    }

    void setSkipTaskbar(bool set) override
    {
        win::set_skip_taskbar(m_client, set);
    }

    bool skipPager() const override
    {
        return m_client->control->skip_pager();
    }

    void setSkipPager(bool set) override
    {
        win::set_skip_pager(m_client, set);
    }

    bool skipSwitcher() const override
    {
        return m_client->control->skip_switcher();
    }

    void setSkipSwitcher(bool set) override
    {
        win::set_skip_switcher(m_client, set);
    }

    bool skipsCloseAnimation() const override
    {
        return m_client->skipsCloseAnimation();
    }

    void setSkipCloseAnimation(bool set) override
    {
        m_client->setSkipCloseAnimation(set);
    }

    bool isActive() const override
    {
        return m_client->control->active;
    }

    bool isDemandingAttention() const override
    {
        return m_client->control->demands_attention;
    }

    void demandAttention(bool set) override
    {
        win::set_demands_attention(m_client, set);
    }

    bool wantsInput() const override
    {
        return m_client->wantsInput();
    }

    bool applicationMenuActive() const override
    {
        return m_client->control->appmenu.active;
    }

    bool unresponsive() const override
    {
        return m_client->control->unresponsive;
    }

    bool isTransient() const override
    {
        return m_client->transient()->lead();
    }

    window* transientFor() const override
    {
        auto parent = m_client->transient()->lead();
        if (!parent) {
            return nullptr;
        }

        assert(parent->control);
        return parent->control->scripting.get();
    }

    bool isModal() const override
    {
        return m_client->transient()->modal();
    }

    bool decorationHasAlpha() const override
    {
        return win::decoration_has_alpha(m_client);
    }

    bool hasNoBorder() const override
    {
        return m_client->noBorder();
    }

    void setNoBorder(bool set) override
    {
        m_client->setNoBorder(set);
    }

    QString colorScheme() const override
    {
        return m_client->control->palette.color_scheme;
    }

    QByteArray desktopFileName() const override
    {
        return m_client->control->desktop_file_name;
    }

    bool hasApplicationMenu() const override
    {
        return m_client->control->has_application_menu();
    }

    bool providesContextHelp() const override
    {
        return m_client->providesContextHelp();
    }

    bool isClient() const override
    {
        return m_client->isClient();
    }

    bool isDeleted() const override
    {
        return static_cast<bool>(m_client->remnant);
    }

    quint32 surfaceId() const override
    {
        return m_client->surface_id;
    }

    Wrapland::Server::Surface* surface() const override
    {
        return m_client->surface;
    }

    QSize basicUnit() const override
    {
        return m_client->basicUnit();
    }

    bool isBlockingCompositing() override
    {
        return m_client->isBlockingCompositing();
    }

    void setBlockingCompositing(bool block) override
    {
        m_client->setBlockingCompositing(block);
    }

    RefWin* client() const
    {
        return m_client;
    }

private:
    RefWin* m_client;
};

}

Q_DECLARE_METATYPE(KWin::scripting::window*)
Q_DECLARE_METATYPE(QList<KWin::scripting::window*>)
