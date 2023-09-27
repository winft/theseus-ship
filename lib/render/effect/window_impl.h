/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "render/effect/screen_impl.h"
#include "render/types.h"
#include "render/x11/effect.h"
#include "win/actions.h"
#include "win/damage.h"
#include "win/desktop_get.h"
#include "win/geo.h"
#include "win/meta.h"
#include "win/move.h"
#include "win/scene.h"
#include "win/types.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effect_window_visible_ref.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/window_quad.h>
#include <render/gl/interface/texture.h>

#include <QHash>

namespace KWin::render
{

template<typename Window>
class effects_window_impl : public EffectWindow
{
public:
    using space_t = typename Window::scene_t::space_t;
    using base_t = typename space_t::base_t;

    explicit effects_window_impl(Window& window)
        : window{window}
    {
        // Deleted windows are not managed. So, when windowClosed signal is
        // emitted, effects can't distinguish managed windows from unmanaged
        // windows(e.g. combo box popups, popup menus, etc). Save value of the
        // managed property during construction of EffectWindow. At that time,
        // parent can be Client, XdgShellClient, or Unmanaged. So, later on, when
        // an instance of Deleted becomes parent of the EffectWindow, effects
        // can still figure out whether it is/was a managed window.
        std::visit(overload{[this](typename space_t::x11_window* ref_win) {
                                managed = ref_win->isClient();
                                x11Client = true;
                            },
                            [this](auto&& ref_win) {
                                if constexpr (requires(decltype(ref_win) win) {
                                                  win->is_wayland_window();
                                              }) {
                                    waylandClient = ref_win->is_wayland_window();
                                }
                            }},
                   *window.ref_win);
    }

    ~effects_window_impl() override
    {
        QVariant cachedTextureVariant = data(LanczosCacheRole);
        if (cachedTextureVariant.isValid()) {
            auto cachedTexture = static_cast<GLTexture*>(cachedTextureVariant.value<void*>());
            delete cachedTexture;
        }
    }

    void addRepaint(QRect const& rect) override
    {
        std::visit(overload{[&](auto&& ref_win) { win::add_repaint(*ref_win, rect); }},
                   *window.ref_win);
    }

    void addRepaint(int x, int y, int w, int h) override
    {
        addRepaint(QRect(x, y, w, h));
    }

    void addRepaintFull() override
    {
        std::visit(overload{[](auto&& ref_win) { win::add_full_repaint(*ref_win); }},
                   *window.ref_win);
    }

    void addLayerRepaint(QRect const& rect) override
    {
        std::visit(overload{[&](auto&& ref_win) { win::add_layer_repaint(*ref_win, rect); }},
                   *window.ref_win);
    }

    void addLayerRepaint(int x, int y, int w, int h) override
    {
        addLayerRepaint(QRect(x, y, w, h));
    }

    void refWindow() override
    {
        std::visit(overload{[](auto&& ref_win) {
                       if (ref_win->transient->annexed) {
                           return;
                       }
                       if (auto& remnant = ref_win->remnant) {
                           return remnant->ref();
                       }

                       // TODO
                       abort();
                   }},
                   *window.ref_win);
    }

    void unrefWindow() override
    {
        std::visit(overload{[](auto&& ref_win) {
                       if (ref_win->transient->annexed) {
                           return;
                       }
                       if (auto& remnant = ref_win->remnant) {
                           // delays deletion in case
                           return remnant->unref();
                       }

                       // TODO
                       abort();
                   }},
                   *window.ref_win);
    }

    const EffectWindowGroup* group() const override
    {
        using x11_win = typename space_t::x11_window;
        return std::visit(
            overload{[](x11_win* ref_win) -> EffectWindowGroup* {
                         if (!ref_win->group) {
                             return nullptr;
                         }
                         return ref_win->group->effect_group;
                     },
                     [](auto&& /*ref_win*/) -> EffectWindowGroup* { return nullptr; }},
            *window.ref_win);
    }

    bool isDeleted() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) { return static_cast<bool>(ref_win->remnant); }},
            *window.ref_win);
    }

    bool isHidden() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) { return static_cast<bool>(ref_win->isHiddenInternal()); }},
            *window.ref_win);
    }

    bool isMinimized() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              if (ref_win->control) {
                                  return ref_win->control->minimized;
                              }
                              if (auto& remnant = ref_win->remnant) {
                                  return remnant->data.minimized;
                              }
                              return false;
                          }},
                          *window.ref_win);
    }

    double opacity() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->opacity(); }},
                          *window.ref_win);
    }

    QStringList activities() const override
    {
        // No support for Activities.
        return {};
    }

    QVector<uint> desktops() const override
    {
        return std::visit(overload{[](auto&& ref_win) -> QVector<uint> {
                              if (ref_win->control || ref_win->remnant) {
                                  return win::x11_desktop_ids(*ref_win);
                              }
                              return {};
                          }},
                          *window.ref_win);
    }

    int x() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.pos().x(); }},
                          *window.ref_win);
    }

    int y() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.pos().y(); }},
                          *window.ref_win);
    }

    int width() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.size().width(); }},
                          *window.ref_win);
    }

    int height() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.size().height(); }},
                          *window.ref_win);
    }

    QSize basicUnit() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              if constexpr (requires(decltype(ref_win) win) { win->basicUnit(); }) {
                                  return ref_win->basicUnit();
                              }
                              return QSize(1, 1);
                          }},
                          *window.ref_win);
    }

    QRect frameGeometry() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.frame; }},
                          *window.ref_win);
    }

    QRect bufferGeometry() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::render_geometry(ref_win); }},
                          *window.ref_win);
    }

    QRect clientGeometry() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return win::frame_to_client_rect(ref_win, ref_win->geo.frame);
                          }},
                          *window.ref_win);
    }

    QString caption() const override
    {
        return std::visit(overload{[](auto&& ref_win) -> QString {
                              if (ref_win->control || ref_win->remnant) {
                                  return win::caption(ref_win);
                              }
                              return {};
                          }},
                          *window.ref_win);
    }

    QRect expandedGeometry() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) { return expanded_geometry_recursion(ref_win); }},
            *window.ref_win);
    }

    EffectScreen* screen() const override
    {
        return std::visit(overload{[this](auto&& ref_win) -> EffectScreen* {
                              auto output = ref_win->topo.central_output;
                              if (!output || !window.compositor.effects) {
                                  return nullptr;
                              }
                              return get_effect_screen(*window.compositor.effects, *output);
                          }},
                          *window.ref_win);
    }

    QPoint pos() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.pos(); }},
                          *window.ref_win);
    }

    QSize size() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->geo.size(); }},
                          *window.ref_win);
    }

    QRect rect() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return QRect({}, ref_win->geo.size()); }},
                          *window.ref_win);
    }

    bool isMovable() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? ref_win->isMovable() : false;
                          }},
                          *window.ref_win);
    }

    bool isMovableAcrossScreens() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? ref_win->isMovableAcrossScreens() : false;
                          }},
                          *window.ref_win);
    }

    bool isUserMove() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? win::is_move(ref_win) : false;
                          }},
                          *window.ref_win);
    }

    bool isUserResize() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? win::is_resize(ref_win) : false;
                          }},
                          *window.ref_win);
    }

    QRect iconGeometry() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) {
                if (!ref_win->control) {
                    return QRect();
                }

                if constexpr (requires(decltype(ref_win) win) { win->iconGeometry(); }) {
                    return ref_win->iconGeometry();
                } else {
                    return ref_win->space.get_icon_geometry(ref_win);
                }
            }},
            *window.ref_win);
    }

    bool isDesktop() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_desktop(ref_win); }},
                          *window.ref_win);
    }

    bool isDock() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_dock(ref_win); }},
                          *window.ref_win);
    }

    bool isToolbar() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_toolbar(ref_win); }},
                          *window.ref_win);
    }

    bool isMenu() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_menu(ref_win); }},
                          *window.ref_win);
    }

    bool isNormalWindow() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_normal(ref_win); }},
                          *window.ref_win);
    }

    bool isSpecialWindow() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? win::is_special_window(ref_win) : true;
                          }},
                          *window.ref_win);
    }

    bool isDialog() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_dialog(ref_win); }},
                          *window.ref_win);
    }

    bool isSplash() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_splash(ref_win); }},
                          *window.ref_win);
    }

    bool isUtility() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_utility(ref_win); }},
                          *window.ref_win);
    }

    bool isDropdownMenu() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_dropdown_menu(ref_win); }},
                          *window.ref_win);
    }

    bool isPopupMenu() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_popup_menu(ref_win); }},
                          *window.ref_win);
    }

    bool isTooltip() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_tooltip(ref_win); }},
                          *window.ref_win);
    }

    bool isNotification() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_notification(ref_win); }},
                          *window.ref_win);
    }

    bool isCriticalNotification() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) { return win::is_critical_notification(ref_win); }},
            *window.ref_win);
    }

    bool isAppletPopup() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_applet_popup(ref_win); }},
                          *window.ref_win);
    }

    bool isOnScreenDisplay() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) { return win::is_on_screen_display(ref_win); }},
            *window.ref_win);
    }

    bool isComboBox() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_combo_box(ref_win); }},
                          *window.ref_win);
    }

    bool isDNDIcon() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_dnd_icon(ref_win); }},
                          *window.ref_win);
    }

    bool skipsCloseAnimation() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) {
                if constexpr (requires(decltype(ref_win) win) { win->skip_close_animation; }) {
                    return ref_win->skip_close_animation;
                }

                return false;
            }},
            *window.ref_win);
    }

    bool acceptsFocus() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? ref_win->wantsInput() : true;
                          }},
                          *window.ref_win);
    }

    bool keepAbove() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              if (ref_win->control) {
                                  return ref_win->control->keep_above;
                              }
                              if (auto& remnant = ref_win->remnant) {
                                  return remnant->data.keep_above;
                              }
                              return false;
                          }},
                          *window.ref_win);
    }

    bool keepBelow() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              if (ref_win->control) {
                                  return ref_win->control->keep_below;
                              }
                              if (auto& remnant = ref_win->remnant) {
                                  return remnant->data.keep_below;
                              }
                              return false;
                          }},
                          *window.ref_win);
    }

    bool isModal() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->transient->modal(); }},
                          *window.ref_win);
    }

    bool isPopupWindow() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::is_popup(ref_win); }},
                          *window.ref_win);
    }

    bool isOutline() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              if constexpr (requires(decltype(ref_win) win) { win->is_outline; }) {
                                  return ref_win->is_outline;
                              }
                              return false;
                          }},
                          *window.ref_win);
    }

    bool isLockScreen() const override
    {
        return std::visit(
            overload{[](auto&& ref_win) {
                if constexpr (requires(decltype(ref_win) win) { win->isLockScreen(); }) {
                    return ref_win->isLockScreen();
                } else {
                    return false;
                }
            }},
            *window.ref_win);
    }

    Wrapland::Server::Surface* surface() const override
    {
        return std::visit(overload{[](auto&& win) -> Wrapland::Server::Surface* {
                              if constexpr (requires(decltype(win) win) { win->surface; }) {
                                  return win->surface;
                              }
                              return nullptr;
                          }},
                          *window.ref_win);
    }

    bool isFullScreen() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              if (ref_win->control) {
                                  return ref_win->control->fullscreen;
                              }
                              if (auto& remnant = ref_win->remnant) {
                                  return remnant->data.fullscreen;
                              }
                              return false;
                          }},
                          *window.ref_win);
    }

    bool isUnresponsive() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? ref_win->control->unresponsive : false;
                          }},
                          *window.ref_win);
    }

    QRect contentsRect() const override
    {
        // TODO(romangg): This feels kind of wrong. Why are the frame extents not part of it (i.e.
        // just
        //                using frame_to_client_rect)? But some clients rely on the current version,
        //                for example Latte for its behind-dock blur.
        return std::visit(overload{[](auto&& ref_win) {
                              auto const deco_offset
                                  = QPoint(win::left_border(ref_win), win::top_border(ref_win));
                              auto const client_size
                                  = win::frame_relative_client_rect(ref_win).size();

                              return QRect(deco_offset, client_size);
                          }},
                          *window.ref_win);
    }

    bool decorationHasAlpha() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? win::decoration_has_alpha(ref_win) : false;
                          }},
                          *window.ref_win);
    }

    QIcon icon() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? ref_win->control->icon : QIcon();
                          }},
                          *window.ref_win);
    }

    QString windowClass() const override
    {
        return std::visit(overload{[](auto&& ref_win) -> QString {
                              return ref_win->meta.wm_class.res_name + QLatin1Char(' ')
                                  + ref_win->meta.wm_class.res_class;
                          }},
                          *window.ref_win);
    }

    bool isSkipSwitcher() const override
    {
        return std::visit(overload{[](auto&& ref_win) {
                              return ref_win->control ? ref_win->control->skip_switcher() : false;
                          }},
                          *window.ref_win);
    }

    QString windowRole() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->windowRole(); }},
                          *window.ref_win);
    }

    bool isManaged() const override
    {
        return managed;
    }

    bool isWaylandClient() const override
    {
        return waylandClient;
    }

    bool isX11Client() const override
    {
        return x11Client;
    }

    pid_t pid() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->pid(); }}, *window.ref_win);
    }

    qlonglong windowId() const override
    {
        return std::visit(overload{[](auto&& ref_win) -> qlonglong {
                              if constexpr (requires(decltype(ref_win) win) { win->xcb_windows; }) {
                                  return ref_win->xcb_windows.client;
                              } else {
                                  return XCB_WINDOW_NONE;
                              }
                          }},
                          *window.ref_win);
    }

    QUuid internalId() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return ref_win->meta.internal_id; }},
                          *window.ref_win);
    }

    QRect decorationInnerRect() const override
    {
        return contentsRect();
    }

    KDecoration2::Decoration* decoration() const override
    {
        return std::visit(overload{[](auto&& ref_win) { return win::decoration(ref_win); }},
                          *window.ref_win);
    }

    QByteArray readProperty(long atom, long type, int format) const override
    {
        return std::visit(overload{[&](auto&& win) -> QByteArray {
                              if (!win->space.base.x11_data.connection) {
                                  return {};
                              }
                              if constexpr (requires(decltype(win) win) { win->xcb_windows; }) {
                                  return x11::read_window_property(
                                      win->space.base.x11_data.connection,
                                      win->xcb_windows.client,
                                      atom,
                                      type,
                                      format);
                              }
                              return {};
                          }},
                          *window.ref_win);
    }

    void deleteProperty(long atom) const override
    {
        return std::visit(overload{[&](auto&& ref_win) {
                              if constexpr (requires(decltype(ref_win) win) { win->xcb_windows; }) {
                                  auto& xcb_win = ref_win->xcb_windows.client;
                                  if (xcb_win != XCB_WINDOW_NONE) {
                                      xcb_delete_property(
                                          ref_win->space.base.x11_data.connection, xcb_win, atom);
                                  }
                              }
                          }},
                          *window.ref_win);
    }

    EffectWindow* findModal() override
    {
        return std::visit(overload{[](auto&& ref_win) -> EffectWindow* {
                              if (!ref_win->control) {
                                  return nullptr;
                              }

                              auto modal = win::find_modal(*ref_win);
                              if (!modal) {
                                  return nullptr;
                              }
                              return modal->render->effect.get();
                          }},
                          *window.ref_win);
    }

    EffectWindow* transientFor() override
    {
        return std::visit(overload{[](auto&& ref_win) -> EffectWindow* {
                              if (!ref_win->control) {
                                  return nullptr;
                              }

                              auto transientFor = ref_win->transient->lead();
                              if (transientFor) {
                                  return transientFor->render->effect.get();
                              }

                              return nullptr;
                          }},
                          *window.ref_win);
    }

    EffectWindowList mainWindows() const override
    {
        return std::visit(overload{[](auto&& ref_win) -> EffectWindowList {
                              if (ref_win->control || ref_win->remnant) {
                                  return getMainWindows(ref_win);
                              }
                              return {};
                          }},
                          *window.ref_win);
    }

    WindowQuadList buildQuads(bool force = false) const override
    {
        return window.buildQuads(force);
    }

    void minimize() override
    {
        std::visit(overload{[](auto&& ref_win) {
                       if (ref_win->control) {
                           win::set_minimized(ref_win, true);
                       }
                   }},
                   *window.ref_win);
    }

    void unminimize() override
    {
        std::visit(overload{[](auto&& ref_win) {
                       if (ref_win->control) {
                           win::set_minimized(ref_win, false);
                       }
                   }},
                   *window.ref_win);
    }

    void closeWindow() override
    {
        std::visit(overload{[](auto&& ref_win) {
                       if (ref_win->control) {
                           ref_win->closeWindow();
                       }
                   }},
                   *window.ref_win);
    }

    void referencePreviousWindowPixmap() override
    {
        window.reference_previous_buffer();
    }

    void unreferencePreviousWindowPixmap() override
    {
        window.unreference_previous_buffer();
    }

    void refVisible(EffectWindowVisibleRef const* holder) override
    {
        auto const reason = holder->reason();

        if (reason & PAINT_DISABLED) {
            ++force_visible.hidden;
        }
        if (reason & PAINT_DISABLED_BY_DELETE) {
            ++force_visible.deleted;
        }
        if (reason & PAINT_DISABLED_BY_DESKTOP) {
            ++force_visible.desktop;
        }
        if (reason & PAINT_DISABLED_BY_MINIMIZE) {
            ++force_visible.minimized;
        }
    }

    void unrefVisible(EffectWindowVisibleRef const* holder) override
    {
        auto const reason = holder->reason();

        if (reason & PAINT_DISABLED) {
            assert(force_visible.hidden > 0);
            --force_visible.hidden;
        }
        if (reason & PAINT_DISABLED_BY_DELETE) {
            assert(force_visible.deleted > 0);
            --force_visible.deleted;
        }
        if (reason & PAINT_DISABLED_BY_DESKTOP) {
            assert(force_visible.desktop > 0);
            --force_visible.desktop;
        }
        if (reason & PAINT_DISABLED_BY_MINIMIZE) {
            assert(force_visible.minimized > 0);
            --force_visible.minimized;
        }
    }

    bool is_forced_visible() const
    {
        auto const& fv = force_visible;
        return fv.hidden + fv.deleted + fv.desktop + fv.minimized;
    }

    QWindow* internalWindow() const override
    {
        if constexpr (requires { typename space_t::internal_window_t; }) {
            using int_win = typename space_t::internal_window_t;
            return std::visit(overload{[](int_win* ref_win) { return ref_win->internalWindow(); },
                                       [](auto&& /*ref_win*/) -> QWindow* { return nullptr; }},
                              *window.ref_win);
        }
        return nullptr;
    }

    void elevate(bool elevate)
    {
        effects->setElevatedWindow(this, elevate);
    }

    void setData(int role, const QVariant& data) override
    {
        if (!data.isNull())
            dataMap[role] = data;
        else
            dataMap.remove(role);
        Q_EMIT effects->windowDataChanged(this, role);
    }

    QVariant data(int role) const override
    {
        return dataMap.value(role);
    }

    Window& window;

private:
    template<typename T>
    static EffectWindowList getMainWindows(T* c)
    {
        const auto leads = c->transient->leads();
        EffectWindowList ret;
        ret.reserve(leads.size());
        std::transform(std::cbegin(leads),
                       std::cend(leads),
                       std::back_inserter(ret),
                       [](auto client) { return client->render->effect.get(); });
        return ret;
    }

    template<typename Win>
    static QRect expanded_geometry_recursion(Win* window)
    {
        QRect geo;
        for (auto child : window->transient->children) {
            if (child->transient->annexed) {
                geo |= expanded_geometry_recursion(child);
            }
        }
        return geo |= win::visible_rect(window);
    }

    QHash<int, QVariant> dataMap;
    bool managed = false;
    bool waylandClient{false};
    bool x11Client{false};

    struct {
        int hidden{0};
        int deleted{0};
        int desktop{0};
        int minimized{0};
    } force_visible;
};

}
