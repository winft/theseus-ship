/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output_helpers.h"
#include "render/thumbnail_item.h"
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

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/window_quad.h>
#include <kwingl/texture.h>

#include <QHash>

namespace KWin::render
{

template<typename Window>
class effects_window_impl : public EffectWindow
{
public:
    using space_t = typename Window::scene_t::space_t;

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
        managed = window.ref_win->isClient();

        waylandClient = window.ref_win->is_wayland_window();
        x11Client = dynamic_cast<typename space_t::x11_window*>(window.ref_win)
            || window.ref_win->xcb_window;
    }

    ~effects_window_impl() override
    {
        QVariant cachedTextureVariant = data(LanczosCacheRole);
        if (cachedTextureVariant.isValid()) {
            auto cachedTexture = static_cast<GLTexture*>(cachedTextureVariant.value<void*>());
            delete cachedTexture;
        }
    }

    void enablePainting(int reason) override
    {
        window.enablePainting(static_cast<window_paint_disable_type>(reason));
    }

    void disablePainting(int reason) override
    {
        window.disablePainting(static_cast<window_paint_disable_type>(reason));
    }

    bool isPaintingEnabled() override
    {
        return window.isPaintingEnabled();
    }

    void addRepaint(const QRect& r) override
    {
        win::add_repaint(*window.ref_win, r);
    }

    void addRepaint(int x, int y, int w, int h) override
    {
        addRepaint(QRect(x, y, w, h));
    }

    void addRepaintFull() override
    {
        win::add_full_repaint(*window.ref_win);
    }

    void addLayerRepaint(const QRect& r) override
    {
        win::add_layer_repaint(*window.ref_win, r);
    }

    void addLayerRepaint(int x, int y, int w, int h) override
    {
        addLayerRepaint(QRect(x, y, w, h));
    }

    void refWindow() override
    {
        if (window.ref_win->transient->annexed) {
            return;
        }
        if (auto& remnant = window.ref_win->remnant) {
            return remnant->ref();
        }
        abort(); // TODO
    }

    void unrefWindow() override
    {
        if (window.ref_win->transient->annexed) {
            return;
        }
        if (auto& remnant = window.ref_win->remnant) {
            // delays deletion in case
            return remnant->unref();
        }
        abort(); // TODO
    }

    const EffectWindowGroup* group() const override
    {
        if (auto x11_win = dynamic_cast<typename space_t::x11_window*>(window.ref_win);
            x11_win && x11_win->group) {
            return x11_win->group->effect_group;
        }
        return nullptr; // TODO
    }

    bool isDeleted() const override
    {
        return static_cast<bool>(window.ref_win->remnant);
    }

    bool isMinimized() const override
    {
        if (window.ref_win->control) {
            return window.ref_win->control->minimized;
        }
        if (auto& remnant = window.ref_win->remnant) {
            return remnant->data.minimized;
        }
        return false;
    }

    double opacity() const override
    {
        return window.ref_win->opacity();
    }

    bool hasAlpha() const override
    {
        return win::has_alpha(*window.ref_win);
    }

    QStringList activities() const override
    {
        // No support for Activities.
        return {};
    }

    int desktop() const override
    {
        return win::get_desktop(*window.ref_win);
    }

    QVector<uint> desktops() const override
    {
        if (window.ref_win->control || window.ref_win->remnant) {
            return win::x11_desktop_ids(window.ref_win);
        }
        return {};
    }

    int x() const override
    {
        return window.ref_win->geo.pos().x();
    }

    int y() const override
    {
        return window.ref_win->geo.pos().y();
    }

    int width() const override
    {
        return window.ref_win->geo.size().width();
    }

    int height() const override
    {
        return window.ref_win->geo.size().height();
    }

    QSize basicUnit() const override
    {
        if (auto client = dynamic_cast<typename space_t::x11_window*>(window.ref_win)) {
            return client->basicUnit();
        }
        return QSize(1, 1);
    }

    QRect geometry() const override
    {
        return frameGeometry();
    }

    QRect frameGeometry() const override
    {
        return window.ref_win->geo.frame;
    }

    QRect bufferGeometry() const override
    {
        return win::render_geometry(window.ref_win);
    }

    QRect clientGeometry() const override
    {
        return win::frame_to_client_rect(window.ref_win, window.ref_win->geo.frame);
    }

    QString caption() const override
    {
        if (window.ref_win->control || window.ref_win->remnant) {
            return win::caption(window.ref_win);
        }
        return {};
    }

    QRect expandedGeometry() const override
    {
        return expanded_geometry_recursion(window.ref_win);
    }

    int screen() const override
    {
        if (!window.ref_win->topo.central_output) {
            return 0;
        }
        return base::get_output_index(window.ref_win->space.base.outputs,
                                      *window.ref_win->topo.central_output);
    }

    QPoint pos() const override
    {
        return window.ref_win->geo.pos();
    }

    QSize size() const override
    {
        return window.ref_win->geo.size();
    }

    QRect rect() const override
    {
        return QRect({}, window.ref_win->geo.size());
    }

    bool isMovable() const override
    {
        return window.ref_win->control ? window.ref_win->isMovable() : false;
    }

    bool isMovableAcrossScreens() const override
    {
        return window.ref_win->control ? window.ref_win->isMovableAcrossScreens() : false;
    }

    bool isUserMove() const override
    {
        return window.ref_win->control ? win::is_move(window.ref_win) : false;
    }

    bool isUserResize() const override
    {
        return window.ref_win->control ? win::is_resize(window.ref_win) : false;
    }

    QRect iconGeometry() const override
    {
        return window.ref_win->control ? window.ref_win->iconGeometry() : QRect();
    }

    bool isDesktop() const override
    {
        return win::is_desktop(window.ref_win);
    }

    bool isDock() const override
    {
        return win::is_dock(window.ref_win);
    }

    bool isToolbar() const override
    {
        return win::is_toolbar(window.ref_win);
    }

    bool isMenu() const override
    {
        return win::is_menu(window.ref_win);
    }

    bool isNormalWindow() const override
    {
        return win::is_normal(window.ref_win);
    }

    bool isSpecialWindow() const override
    {
        return window.ref_win->control ? win::is_special_window(window.ref_win) : true;
    }

    bool isDialog() const override
    {
        return win::is_dialog(window.ref_win);
    }

    bool isSplash() const override
    {
        return win::is_splash(window.ref_win);
    }

    bool isUtility() const override
    {
        return win::is_utility(window.ref_win);
    }

    bool isDropdownMenu() const override
    {
        return win::is_dropdown_menu(window.ref_win);
    }

    bool isPopupMenu() const override
    {
        return win::is_popup_menu(window.ref_win);
    }

    bool isTooltip() const override
    {
        return win::is_tooltip(window.ref_win);
    }

    bool isNotification() const override
    {
        return win::is_notification(window.ref_win);
    }

    bool isCriticalNotification() const override
    {
        return win::is_critical_notification(window.ref_win);
    }

    bool isAppletPopup() const override
    {
        return win::is_applet_popup(window.ref_win);
    }

    bool isOnScreenDisplay() const override
    {
        return win::is_on_screen_display(window.ref_win);
    }

    bool isComboBox() const override
    {
        return win::is_combo_box(window.ref_win);
    }

    bool isDNDIcon() const override
    {
        return win::is_dnd_icon(window.ref_win);
    }

    bool skipsCloseAnimation() const override
    {
        return window.ref_win->skip_close_animation;
    }

    bool acceptsFocus() const override
    {
        return window.ref_win->control ? window.ref_win->wantsInput() : true;
    }

    bool keepAbove() const override
    {
        if (window.ref_win->control) {
            return window.ref_win->control->keep_above;
        }
        if (auto& remnant = window.ref_win->remnant) {
            return remnant->data.keep_above;
        }
        return false;
    }

    bool keepBelow() const override
    {
        if (window.ref_win->control) {
            return window.ref_win->control->keep_below;
        }
        if (auto& remnant = window.ref_win->remnant) {
            return remnant->data.keep_below;
        }
        return false;
    }

    bool isModal() const override
    {
        return window.ref_win->transient->modal();
    }

    bool isPopupWindow() const override
    {
        return win::is_popup(window.ref_win);
    }

    bool isOutline() const override
    {
        return window.ref_win->is_outline;
    }

    bool isLockScreen() const override
    {
        return window.ref_win->isLockScreen();
    }

    Wrapland::Server::Surface* surface() const override
    {
        return window.ref_win->surface;
    }

    bool isFullScreen() const override
    {
        if (window.ref_win->control) {
            return window.ref_win->control->fullscreen;
        }
        if (auto& remnant = window.ref_win->remnant) {
            return remnant->data.fullscreen;
        }
        return false;
    }

    bool isUnresponsive() const override
    {
        return window.ref_win->control ? window.ref_win->control->unresponsive : false;
    }

    QRect contentsRect() const override
    {
        // TODO(romangg): This feels kind of wrong. Why are the frame extents not part of it (i.e.
        // just
        //                using frame_to_client_rect)? But some clients rely on the current version,
        //                for example Latte for its behind-dock blur.
        auto const deco_offset
            = QPoint(win::left_border(window.ref_win), win::top_border(window.ref_win));
        auto const client_size = win::frame_relative_client_rect(window.ref_win).size();

        return QRect(deco_offset, client_size);
    }

    bool decorationHasAlpha() const override
    {
        return window.ref_win->control ? win::decoration_has_alpha(window.ref_win) : false;
    }

    QIcon icon() const override
    {
        return window.ref_win->control ? window.ref_win->control->icon : QIcon();
    }

    QString windowClass() const override
    {
        return window.ref_win->meta.wm_class.res_name + QLatin1Char(' ')
            + window.ref_win->meta.wm_class.res_class;
    }

    NET::WindowType windowType() const override
    {
        return window.ref_win->windowType();
    }

    bool isSkipSwitcher() const override
    {
        return window.ref_win->control ? window.ref_win->control->skip_switcher() : false;
    }

    // legacy from tab groups, can be removed when no effects use this any more.
    bool isCurrentTab() const override
    {
        return true;
    }

    QString windowRole() const override
    {
        return window.ref_win->windowRole();
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
        return window.ref_win->pid();
    }

    qlonglong windowId() const override
    {
        return window.ref_win->xcb_window;
    }

    QUuid internalId() const override
    {
        return window.ref_win->meta.internal_id;
    }

    QRect decorationInnerRect() const override
    {
        return contentsRect();
    }

    KDecoration2::Decoration* decoration() const override
    {
        return win::decoration(window.ref_win);
    }

    QByteArray readProperty(long atom, long type, int format) const override
    {
        if (!kwinApp()->x11Connection()) {
            return QByteArray();
        }
        return x11::read_window_property(window.ref_win->xcb_window, atom, type, format);
    }

    void deleteProperty(long atom) const override
    {
        auto deleteWindowProperty = [](xcb_window_t win, long int atom) {
            if (win == XCB_WINDOW_NONE) {
                return;
            }
            xcb_delete_property(kwinApp()->x11Connection(), win, atom);
        };

        if (kwinApp()->x11Connection()) {
            deleteWindowProperty(window.ref_win->xcb_window, atom);
        }
    }

    EffectWindow* findModal() override
    {
        if (!window.ref_win->control) {
            return nullptr;
        }

        auto modal = window.ref_win->findModal();
        if (modal) {
            return modal->render->effect.get();
        }

        return nullptr;
    }

    EffectWindow* transientFor() override
    {
        if (!window.ref_win->control) {
            return nullptr;
        }

        auto transientFor = window.ref_win->transient->lead();
        if (transientFor) {
            return transientFor->render->effect.get();
        }

        return nullptr;
    }

    EffectWindowList mainWindows() const override
    {
        if (window.ref_win->control || window.ref_win->remnant) {
            return getMainWindows(window.ref_win);
        }
        return {};
    }

    WindowQuadList buildQuads(bool force = false) const override
    {
        return window.buildQuads(force);
    }

    void minimize() override
    {
        if (window.ref_win->control) {
            win::set_minimized(window.ref_win, true);
        }
    }

    void unminimize() override
    {
        if (window.ref_win->control) {
            win::set_minimized(window.ref_win, false);
        }
    }

    void closeWindow() override
    {
        if (window.ref_win->control) {
            window.ref_win->closeWindow();
        }
    }

    void referencePreviousWindowPixmap() override
    {
        window.reference_previous_buffer();
    }

    void unreferencePreviousWindowPixmap() override
    {
        window.unreference_previous_buffer();
    }

    QWindow* internalWindow() const override
    {
        auto client = dynamic_cast<typename space_t::internal_window_t*>(window.ref_win);
        if (!client) {
            return nullptr;
        }
        return client->internalWindow();
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

    void registerThumbnail(basic_thumbnail_item* item)
    {
        if (auto thumb = qobject_cast<window_thumbnail_item*>(item)) {
            insertThumbnail(thumb);
            connect(thumb, &QObject::destroyed, this, &effects_window_impl::thumbnailDestroyed);
            connect(thumb,
                    &window_thumbnail_item::wIdChanged,
                    this,
                    &effects_window_impl::thumbnailTargetChanged);
        } else if (auto desktopThumb = qobject_cast<desktop_thumbnail_item*>(item)) {
            m_desktopThumbnails.append(desktopThumb);
            connect(desktopThumb,
                    &QObject::destroyed,
                    this,
                    &effects_window_impl::desktopThumbnailDestroyed);
        }
    }

    QHash<window_thumbnail_item*, QPointer<effects_window_impl>> const& thumbnails() const
    {
        return m_thumbnails;
    }
    QList<desktop_thumbnail_item*> const& desktopThumbnails() const
    {
        return m_desktopThumbnails;
    }

    Window& window;

private:
    template<typename T>
    EffectWindowList getMainWindows(T* c) const
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

    QRect expanded_geometry_recursion(typename Window::ref_t* window) const
    {
        QRect geo;
        for (auto child : window->transient->children) {
            if (child->transient->annexed) {
                geo |= expanded_geometry_recursion(child);
            }
        }
        return geo |= win::visible_rect(window);
    }

    void thumbnailDestroyed(QObject* object)
    {
        // we know it is a window_thumbnail_item
        m_thumbnails.remove(static_cast<window_thumbnail_item*>(object));
    }

    void thumbnailTargetChanged()
    {
        if (auto item = qobject_cast<window_thumbnail_item*>(sender())) {
            insertThumbnail(item);
        }
    }

    void desktopThumbnailDestroyed(QObject* object)
    {
        // we know it is a desktop_thumbnail_item
        m_desktopThumbnails.removeAll(static_cast<desktop_thumbnail_item*>(object));
    }

    void insertThumbnail(window_thumbnail_item* item)
    {
        auto w = effects->findWindow(item->wId());
        if (w) {
            m_thumbnails.insert(
                item, QPointer<effects_window_impl>(static_cast<effects_window_impl*>(w)));
        } else {
            m_thumbnails.insert(item, QPointer<effects_window_impl>());
        }
    }

    QHash<int, QVariant> dataMap;
    QHash<window_thumbnail_item*, QPointer<effects_window_impl>> m_thumbnails;
    QList<desktop_thumbnail_item*> m_desktopThumbnails;
    bool managed = false;
    bool waylandClient;
    bool x11Client;
};

}
