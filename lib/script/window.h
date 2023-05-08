/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

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

#include <variant>

namespace KWin::scripting
{

class KWIN_EXPORT window : public win::property_window
{
    Q_OBJECT

    Q_PROPERTY(KWin::scripting::output* output READ output NOTIFY outputChanged)

    /// @deprecated
    Q_PROPERTY(QStringList activities READ activities NOTIFY activitiesChanged)

    /// @deprecated
    Q_PROPERTY(bool shade READ isShade WRITE setShade NOTIFY shadeChanged)

    Q_PROPERTY(int stackingOrder READ stackingOrder NOTIFY stackingOrderChanged)

public:
    explicit window(win::window_qobject& qtwin);

    virtual scripting::output* output() const = 0;
    virtual int desktop() const = 0;
    virtual bool isOnDesktop(unsigned int desktop) const = 0;
    virtual bool isOnDesktop(win::virtual_desktop* desktop) const = 0;
    virtual bool isOnCurrentDesktop() const = 0;
    virtual bool isOnOutput(base::output* output) const = 0;
    virtual int stackingOrder() const = 0;

    QStringList activities() const;
    bool isShadeable() const;
    bool isShade() const;
    void setShade(bool set);

public Q_SLOTS:
    virtual void closeWindow() = 0;

Q_SIGNALS:
    void outputChanged();
    void quickTileModeChanged();

    void moveResizeCursorChanged(input::cursor_shape);
    void clientStartUserMovedResized(KWin::scripting::window* window);
    void clientStepUserMovedResized(KWin::scripting::window* window, const QRect&);
    void clientFinishUserMovedResized(KWin::scripting::window* window);

    void minimizeableChanged(bool);
    void shadeableChanged(bool);
    void maximizeableChanged(bool);

    void activitiesChanged(KWin::scripting::window* client);

    void shadeChanged();

    void paletteChanged(const QPalette& p);

    void clientMinimized(KWin::scripting::window* window);
    void clientUnminimized(KWin::scripting::window* window);

    void
    clientMaximizedStateChanged(KWin::scripting::window* window, bool horizontal, bool vertical);

    void damaged(KWin::scripting::window* client, const QRegion& damage);
    void stackingOrderChanged();

    // TODO: this signal is never emitted - remove?
    void clientMaximizeSet(KWin::scripting::window* window, bool horizontal, bool vertical);
};

template<typename RefWin>
class window_impl : public window
{
public:
    template<typename Win>
    window_impl(Win* ref_win)
        : window(*ref_win->qobject)
        , ref_win{ref_win}
    {
        auto qtwin = get_window_qobject();
        QObject::connect(qtwin,
                         &win::window_qobject::opacityChanged,
                         this,
                         [this](auto oldOpacity) { Q_EMIT opacityChanged(this, oldOpacity); });
        QObject::connect(
            qtwin, &win::window_qobject::frame_geometry_changed, this, [this](auto oldGeometry) {
                Q_EMIT frameGeometryChanged(this, oldGeometry);
            });

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

        QObject::connect(qtwin, &win::window_qobject::damaged, this, [this](auto damage) {
            Q_EMIT damaged(this, damage);
        });
    }

    QString resourceName() const override
    {
        return std::visit(overload{[](auto&& win) { return win->meta.wm_class.res_name; }},
                          ref_win);
    }

    QString resourceClass() const override
    {
        return std::visit(overload{[](auto&& win) { return win->meta.wm_class.res_class; }},
                          ref_win);
    }

    QString caption() const override
    {
        return std::visit(overload{[](auto&& win) { return win::caption(win); }}, ref_win);
    }

    QIcon icon() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->icon; }}, ref_win);
    }

    QRect iconGeometry() const override
    {
        return std::visit(overload{[](auto&& win) { return win::get_icon_geometry(*win); }},
                          ref_win);
    }

    QUuid internalId() const override
    {
        return std::visit(overload{[](auto&& win) { return win->meta.internal_id; }}, ref_win);
    }

    pid_t pid() const override
    {
        return std::visit(overload{[](auto&& win) { return win->pid(); }}, ref_win);
    }

    QRect bufferGeometry() const override
    {
        return std::visit(overload{[](auto&& win) { return win::render_geometry(win); }}, ref_win);
    }

    QRect frameGeometry() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.frame; }}, ref_win);
    }

    void setFrameGeometry(QRect const& geo) override
    {
        std::visit(overload{[&](auto&& win) { win->setFrameGeometry(geo); }}, ref_win);
    }

    QPoint pos() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.pos(); }}, ref_win);
    }

    QRect rect() const override
    {
        return std::visit(overload{[](auto&& win) { return QRect({}, win->geo.size()); }}, ref_win);
    }

    QRect visibleRect() const override
    {
        return std::visit(overload{[](auto&& win) { return win::visible_rect(win); }}, ref_win);
    }

    QSize size() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.size(); }}, ref_win);
    }

    QSize minSize() const override
    {
        return std::visit(overload{[](auto&& win) { return win->minSize(); }}, ref_win);
    }

    QSize maxSize() const override
    {
        return std::visit(overload{[](auto&& win) { return win->maxSize(); }}, ref_win);
    }

    QPoint clientPos() const override
    {
        return std::visit(
            overload{[](auto&& win) { return win::frame_relative_client_rect(win).topLeft(); }},
            ref_win);
    }

    QSize clientSize() const override
    {
        return std::visit(
            overload{[](auto&& win) { return win::frame_to_client_size(win, win->geo.size()); }},
            ref_win);
    }

    int x() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.pos().x(); }}, ref_win);
    }

    int y() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.pos().y(); }}, ref_win);
    }

    int width() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.size().width(); }}, ref_win);
    }

    int height() const override
    {
        return std::visit(overload{[](auto&& win) { return win->geo.size().height(); }}, ref_win);
    }

    bool isMove() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_move(win); }}, ref_win);
    }

    bool isResize() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_resize(win); }}, ref_win);
    }

    bool hasAlpha() const override
    {
        return std::visit(overload{[](auto&& win) { return win::has_alpha(*win); }}, ref_win);
    }

    qreal opacity() const override
    {
        return std::visit(overload{[](auto&& win) { return win->opacity(); }}, ref_win);
    }

    void setOpacity(qreal opacity) override
    {
        std::visit(overload{[=](auto&& win) { win->setOpacity(opacity); }}, ref_win);
    }

    bool isFullScreen() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->fullscreen; }}, ref_win);
    }

    void setFullScreen(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win->setFullScreen(set); }}, ref_win);
    }

    scripting::output* output() const override
    {
        return std::visit(
            overload{[](auto&& win) -> scripting::output* {
                if (!win->topo.central_output) {
                    return nullptr;
                }
                using space_t = std::remove_reference_t<decltype(win->space)>;
                auto outputs = win->space.base.script->workspaceWrapper()->screens();
                auto it = std::find_if(outputs.begin(), outputs.end(), [win](auto out) {
                    auto out_impl
                        = static_cast<output_impl<typename space_t::base_t::output_t>*>(out);
                    return win->topo.central_output == &out_impl->ref_out;
                });
                if (it == outputs.end()) {
                    return nullptr;
                }
                return *it;
            }},
            ref_win);
    }

    int desktop() const override
    {
        return std::visit(overload{[](auto&& win) { return win::get_desktop(*win); }}, ref_win);
    }

    QVector<win::virtual_desktop*> desktops() const override
    {
        return std::visit(overload{[](auto&& win) { return win::get_desktops(*win); }}, ref_win);
    }

    void setDesktops(QVector<win::virtual_desktop*> desktops) override
    {
        std::visit(overload{[=](auto&& win) { win::set_desktops(win, desktops); }}, ref_win);
    }

    bool isOnAllDesktops() const override
    {
        return std::visit(overload{[](auto&& win) { return win::on_all_desktops(win); }}, ref_win);
    }

    void setOnAllDesktops(bool set) override
    {
        std::visit(overload{[set](auto&& win) { win::set_on_all_desktops(win, set); }}, ref_win);
    }

    bool isOnDesktop(unsigned int desktop) const override
    {
        return std::visit(overload{[desktop](auto&& win) { return win::on_desktop(win, desktop); }},
                          ref_win);
    }

    bool isOnDesktop(win::virtual_desktop* desktop) const override
    {
        return std::visit(overload{[desktop](auto&& win) { return win::on_desktop(win, desktop); }},
                          ref_win);
    }

    bool isOnCurrentDesktop() const override
    {
        return std::visit(overload{[](auto&& win) { return win::on_current_desktop(win); }},
                          ref_win);
    }

    bool isOnOutput(base::output* output) const override
    {
        return std::visit(overload{[output](auto&& win) { return win::on_screen(win, output); }},
                          ref_win);
    }

    QString windowRole() const override
    {
        return std::visit(overload{[](auto&& win) { return win->windowRole(); }}, ref_win);
    }

    bool isDesktop() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_desktop(win); }}, ref_win);
    }

    bool isDock() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_dock(win); }}, ref_win);
    }

    bool isToolbar() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_toolbar(win); }}, ref_win);
    }

    bool isMenu() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_menu(win); }}, ref_win);
    }

    bool isNormalWindow() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_normal(win); }}, ref_win);
    }

    bool isDialog() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_dialog(win); }}, ref_win);
    }

    bool isSplash() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_splash(win); }}, ref_win);
    }

    bool isUtility() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_utility(win); }}, ref_win);
    }

    bool isDropdownMenu() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_dropdown_menu(win); }}, ref_win);
    }

    bool isPopupMenu() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_popup_menu(win); }}, ref_win);
    }

    bool isTooltip() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_tooltip(win); }}, ref_win);
    }

    bool isNotification() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_notification(win); }}, ref_win);
    }

    bool isCriticalNotification() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_critical_notification(win); }},
                          ref_win);
    }

    bool isAppletPopup() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_applet_popup(win); }}, ref_win);
    }

    bool isOnScreenDisplay() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_on_screen_display(win); }},
                          ref_win);
    }

    bool isComboBox() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_combo_box(win); }}, ref_win);
    }

    bool isDNDIcon() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_dnd_icon(win); }}, ref_win);
    }

    bool isPopupWindow() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_popup(win); }}, ref_win);
    }

    bool isSpecialWindow() const override
    {
        return std::visit(overload{[](auto&& win) { return win::is_special_window(win); }},
                          ref_win);
    }

    bool isCloseable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isCloseable(); }}, ref_win);
    }

    bool isMovable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isMovable(); }}, ref_win);
    }

    bool isMovableAcrossScreens() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isMovableAcrossScreens(); }},
                          ref_win);
    }

    bool isResizable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isResizable(); }}, ref_win);
    }

    bool isMinimizable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isMinimizable(); }}, ref_win);
    }

    bool isMaximizable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->isMaximizable(); }}, ref_win);
    }

    bool isFullScreenable() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->can_fullscreen(); }},
                          ref_win);
    }

    bool isOutline() const override
    {
        return std::visit(overload{[](auto&& win) {
                              if constexpr (requires(decltype(win) win) { win->is_outline; }) {
                                  return win->is_outline;
                              }
                              return false;
                          }},
                          ref_win);
    }

    bool keepAbove() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->keep_above; }}, ref_win);
    }

    void setKeepAbove(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_keep_above(win, set); }}, ref_win);
    }

    bool keepBelow() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->keep_below; }}, ref_win);
    }

    void setKeepBelow(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_keep_below(win, set); }}, ref_win);
    }

    bool isMinimized() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->minimized; }}, ref_win);
    }

    void setMinimized(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_minimized(win, set); }}, ref_win);
    }

    bool skipTaskbar() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->skip_taskbar(); }},
                          ref_win);
    }

    void setSkipTaskbar(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_skip_taskbar(win, set); }}, ref_win);
    }

    bool skipPager() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->skip_pager(); }}, ref_win);
    }

    void setSkipPager(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_skip_pager(win, set); }}, ref_win);
    }

    bool skipSwitcher() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->skip_switcher(); }},
                          ref_win);
    }

    void setSkipSwitcher(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_skip_switcher(win, set); }}, ref_win);
    }

    bool skipsCloseAnimation() const override
    {
        return std::visit(
            overload{[](auto&& win) {
                if constexpr (requires(decltype(win) win) { win->skip_close_animation; }) {
                    return win->skip_close_animation;
                }
                return false;
            }},
            ref_win);
    }

    void setSkipCloseAnimation(bool set) override
    {
        std::visit(overload{[=](auto&& win) {
                       if constexpr (requires(decltype(win) win) { win->skip_close_animation; }) {
                           win::set_skip_close_animation(*win, set);
                       }
                   }},
                   ref_win);
    }

    bool isActive() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->active; }}, ref_win);
    }

    bool isDemandingAttention() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->demands_attention; }},
                          ref_win);
    }

    void demandAttention(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win::set_demands_attention(win, set); }}, ref_win);
    }

    bool wantsInput() const override
    {
        return std::visit(overload{[](auto&& win) { return win->wantsInput(); }}, ref_win);
    }

    bool applicationMenuActive() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->appmenu.active; }},
                          ref_win);
    }

    bool unresponsive() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->unresponsive; }}, ref_win);
    }

    bool isTransient() const override
    {
        return std::visit(overload{[](auto&& win) -> bool { return win->transient->lead(); }},
                          ref_win);
    }

    window* transientFor() const override
    {
        return std::visit(overload{[](auto&& win) -> window* {
                              auto parent = win->transient->lead();
                              if (!parent) {
                                  return nullptr;
                              }

                              assert(parent->control);
                              return parent->control->scripting.get();
                          }},
                          ref_win);
    }

    bool isModal() const override
    {
        return std::visit(overload{[](auto&& win) { return win->transient->modal(); }}, ref_win);
    }

    bool decorationHasAlpha() const override
    {
        return std::visit(overload{[](auto&& win) { return win::decoration_has_alpha(win); }},
                          ref_win);
    }

    bool hasNoBorder() const override
    {
        return std::visit(overload{[](auto&& win) { return win->noBorder(); }}, ref_win);
    }

    void setNoBorder(bool set) override
    {
        std::visit(overload{[=](auto&& win) { win->setNoBorder(set); }}, ref_win);
    }

    QString colorScheme() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->palette.color_scheme; }},
                          ref_win);
    }

    QByteArray desktopFileName() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->desktop_file_name; }},
                          ref_win);
    }

    bool hasApplicationMenu() const override
    {
        return std::visit(overload{[](auto&& win) { return win->control->has_application_menu(); }},
                          ref_win);
    }

    bool providesContextHelp() const override
    {
        return std::visit(overload{[](auto&& win) { return win->providesContextHelp(); }}, ref_win);
    }

    bool isDeleted() const override
    {
        return std::visit(overload{[](auto&& win) { return static_cast<bool>(win->remnant); }},
                          ref_win);
    }

    int stackingOrder() const override
    {
        return std::visit(
            overload{[](auto&& win) { return win::index_of_stacking_order(win->space, win); }},
            ref_win);
    }

    void closeWindow() override
    {
        return std::visit(overload{[](auto&& win) { return win->closeWindow(); }}, ref_win);
    }

    RefWin client() const
    {
        return ref_win;
    }

private:
    RefWin ref_win;
};

}

Q_DECLARE_METATYPE(QList<KWin::scripting::window*>)
