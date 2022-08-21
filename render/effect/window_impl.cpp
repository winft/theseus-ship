/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_impl.h"

#include "main.h"
#include "render/thumbnail_item.h"
#include "render/x11/effect.h"
#include "win/internal_window.h"
#include "win/meta.h"
#include "win/space.h"
#include "win/x11/window.h"

#include <kwingl/texture.h>

namespace KWin::render
{

static void deleteWindowProperty(xcb_window_t win, long int atom)
{
    if (win == XCB_WINDOW_NONE) {
        return;
    }
    xcb_delete_property(kwinApp()->x11Connection(), win, atom);
}

effects_window_impl::effects_window_impl(render::window& window)
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
    x11Client = dynamic_cast<win::x11::window*>(window.ref_win) || window.ref_win->xcb_window;
}

effects_window_impl::~effects_window_impl()
{
    QVariant cachedTextureVariant = data(LanczosCacheRole);
    if (cachedTextureVariant.isValid()) {
        auto cachedTexture = static_cast<GLTexture*>(cachedTextureVariant.value<void*>());
        delete cachedTexture;
    }
}

bool effects_window_impl::isPaintingEnabled()
{
    return window.isPaintingEnabled();
}

void effects_window_impl::enablePainting(int reason)
{
    window.enablePainting(static_cast<render::window_paint_disable_type>(reason));
}

void effects_window_impl::disablePainting(int reason)
{
    window.disablePainting(static_cast<render::window_paint_disable_type>(reason));
}

void effects_window_impl::addRepaint(const QRect& r)
{
    window.ref_win->addRepaint(r);
}

void effects_window_impl::addRepaint(int x, int y, int w, int h)
{
    addRepaint(QRect(x, y, w, h));
}

void effects_window_impl::addRepaintFull()
{
    window.ref_win->addRepaintFull();
}

void effects_window_impl::addLayerRepaint(const QRect& r)
{
    window.ref_win->addLayerRepaint(r);
}

void effects_window_impl::addLayerRepaint(int x, int y, int w, int h)
{
    addLayerRepaint(QRect(x, y, w, h));
}

const EffectWindowGroup* effects_window_impl::group() const
{
    if (auto c = dynamic_cast<win::x11::window*>(window.ref_win); c && c->group()) {
        return c->group()->effect_group;
    }
    return nullptr; // TODO
}

void effects_window_impl::refWindow()
{
    if (window.ref_win->transient()->annexed) {
        return;
    }
    if (auto& remnant = window.ref_win->remnant) {
        return remnant->ref();
    }
    abort(); // TODO
}

void effects_window_impl::unrefWindow()
{
    if (window.ref_win->transient()->annexed) {
        return;
    }
    if (auto& remnant = window.ref_win->remnant) {
        // delays deletion in case
        return remnant->unref();
    }
    abort(); // TODO
}

QRect effects_window_impl::rect() const
{
    return QRect(QPoint(), window.ref_win->size());
}

#define TOPLEVEL_HELPER(rettype, prototype, toplevelPrototype)                                     \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        return window.ref_win->toplevelPrototype();                                                \
    }

TOPLEVEL_HELPER(double, opacity, opacity)
TOPLEVEL_HELPER(bool, hasAlpha, hasAlpha)
TOPLEVEL_HELPER(int, x, pos().x)
TOPLEVEL_HELPER(int, y, pos().y)
TOPLEVEL_HELPER(int, width, size().width)
TOPLEVEL_HELPER(int, height, size().height)
TOPLEVEL_HELPER(QPoint, pos, pos)
TOPLEVEL_HELPER(QSize, size, size)
TOPLEVEL_HELPER(QRect, geometry, frameGeometry)
TOPLEVEL_HELPER(QRect, frameGeometry, frameGeometry)
TOPLEVEL_HELPER(int, desktop, desktop)
TOPLEVEL_HELPER(QString, windowRole, windowRole)
TOPLEVEL_HELPER(bool, skipsCloseAnimation, skipsCloseAnimation)
TOPLEVEL_HELPER(bool, isOutline, isOutline)
TOPLEVEL_HELPER(bool, isLockScreen, isLockScreen)
TOPLEVEL_HELPER(pid_t, pid, pid)
TOPLEVEL_HELPER(bool, isModal, transient()->modal)

#undef TOPLEVEL_HELPER

#define TOPLEVEL_HELPER_WIN(rettype, prototype, function)                                          \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        return win::function(window.ref_win);                                                      \
    }

TOPLEVEL_HELPER_WIN(bool, isComboBox, is_combo_box)
TOPLEVEL_HELPER_WIN(bool, isCriticalNotification, is_critical_notification)
TOPLEVEL_HELPER_WIN(bool, isDesktop, is_desktop)
TOPLEVEL_HELPER_WIN(bool, isDialog, is_dialog)
TOPLEVEL_HELPER_WIN(bool, isDNDIcon, is_dnd_icon)
TOPLEVEL_HELPER_WIN(bool, isDock, is_dock)
TOPLEVEL_HELPER_WIN(bool, isDropdownMenu, is_dropdown_menu)
TOPLEVEL_HELPER_WIN(bool, isMenu, is_menu)
TOPLEVEL_HELPER_WIN(bool, isNormalWindow, is_normal)
TOPLEVEL_HELPER_WIN(bool, isNotification, is_notification)
TOPLEVEL_HELPER_WIN(bool, isPopupMenu, is_popup_menu)
TOPLEVEL_HELPER_WIN(bool, isPopupWindow, is_popup)
TOPLEVEL_HELPER_WIN(bool, isOnScreenDisplay, is_on_screen_display)
TOPLEVEL_HELPER_WIN(bool, isSplash, is_splash)
TOPLEVEL_HELPER_WIN(bool, isToolbar, is_toolbar)
TOPLEVEL_HELPER_WIN(bool, isUtility, is_utility)
TOPLEVEL_HELPER_WIN(bool, isTooltip, is_tooltip)
TOPLEVEL_HELPER_WIN(QRect, bufferGeometry, render_geometry)

#undef TOPLEVEL_HELPER_WIN

#define CLIENT_HELPER_WITH_DELETED_WIN(rettype, prototype, propertyname, defaultValue)             \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (window.ref_win->control || window.ref_win->remnant) {                                  \
            return win::propertyname(window.ref_win);                                              \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER_WITH_DELETED_WIN(QString, caption, caption, QString())
CLIENT_HELPER_WITH_DELETED_WIN(QVector<uint>, desktops, x11_desktop_ids, QVector<uint>())

#undef CLIENT_HELPER_WITH_DELETED_WIN

#define CLIENT_HELPER_WITH_DELETED_WIN_CTRL(rettype, prototype, propertyname, defaultValue)        \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (window.ref_win->control) {                                                             \
            return window.ref_win->control->propertyname;                                          \
        }                                                                                          \
        if (auto& remnant = window.ref_win->remnant) {                                             \
            return remnant->data.propertyname;                                                     \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, keepAbove, keep_above, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, keepBelow, keep_below, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, isMinimized, minimized, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, isFullScreen, fullscreen, false)

#undef CLIENT_HELPER_WITH_DELETED_WIN_CTRL

bool effects_window_impl::isDeleted() const
{
    return static_cast<bool>(window.ref_win->remnant);
}

Wrapland::Server::Surface* effects_window_impl::surface() const
{
    return window.ref_win->surface;
}

QStringList effects_window_impl::activities() const
{
    // No support for Activities.
    return {};
}

int effects_window_impl::screen() const
{
    if (!window.ref_win->central_output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(),
                                  *window.ref_win->central_output);
}

QRect effects_window_impl::clientGeometry() const
{
    return win::frame_to_client_rect(window.ref_win, window.ref_win->frameGeometry());
}

QRect expanded_geometry_recursion(Toplevel* window)
{
    QRect geo;
    for (auto child : window->transient()->children) {
        if (child->transient()->annexed) {
            geo |= expanded_geometry_recursion(child);
        }
    }
    return geo |= win::visible_rect(window);
}

QRect effects_window_impl::expandedGeometry() const
{
    return expanded_geometry_recursion(window.ref_win);
}

// legacy from tab groups, can be removed when no effects use this any more.
bool effects_window_impl::isCurrentTab() const
{
    return true;
}

QString effects_window_impl::windowClass() const
{
    return window.ref_win->resource_name + QLatin1Char(' ') + window.ref_win->resource_class;
}

QRect effects_window_impl::contentsRect() const
{
    // TODO(romangg): This feels kind of wrong. Why are the frame extents not part of it (i.e. just
    //                using frame_to_client_rect)? But some clients rely on the current version,
    //                for example Latte for its behind-dock blur.
    auto const deco_offset
        = QPoint(win::left_border(window.ref_win), win::top_border(window.ref_win));
    auto const client_size = win::frame_relative_client_rect(window.ref_win).size();

    return QRect(deco_offset, client_size);
}

NET::WindowType effects_window_impl::windowType() const
{
    return window.ref_win->windowType();
}

#define CLIENT_HELPER(rettype, prototype, propertyname, defaultValue)                              \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (window.ref_win->control) {                                                             \
            return window.ref_win->propertyname();                                                 \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER(bool, isMovable, isMovable, false)
CLIENT_HELPER(bool, isMovableAcrossScreens, isMovableAcrossScreens, false)
CLIENT_HELPER(QRect, iconGeometry, iconGeometry, QRect())
CLIENT_HELPER(bool, acceptsFocus, wantsInput, true) // We don't actually know...

#undef CLIENT_HELPER

#define CLIENT_HELPER_WIN(rettype, prototype, function, default_value)                             \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (window.ref_win->control) {                                                             \
            return win::function(window.ref_win);                                                  \
        }                                                                                          \
        return default_value;                                                                      \
    }

CLIENT_HELPER_WIN(bool, isSpecialWindow, is_special_window, true)
CLIENT_HELPER_WIN(bool, isUserMove, is_move, false)
CLIENT_HELPER_WIN(bool, isUserResize, is_resize, false)
CLIENT_HELPER_WIN(bool, decorationHasAlpha, decoration_has_alpha, false)

#undef CLIENT_HELPER_WIN

#define CLIENT_HELPER_WIN_CONTROL(rettype, prototype, function, default_value)                     \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (window.ref_win->control) {                                                             \
            return window.ref_win->control->function;                                              \
        }                                                                                          \
        return default_value;                                                                      \
    }

CLIENT_HELPER_WIN_CONTROL(bool, isSkipSwitcher, skip_switcher(), false)
CLIENT_HELPER_WIN_CONTROL(QIcon, icon, icon, QIcon())
CLIENT_HELPER_WIN_CONTROL(bool, isUnresponsive, unresponsive, false)

#undef CLIENT_HELPER_WIN_CONTROL

QSize effects_window_impl::basicUnit() const
{
    if (auto client = dynamic_cast<win::x11::window*>(window.ref_win)) {
        return client->basicUnit();
    }
    return QSize(1, 1);
}

QRect effects_window_impl::decorationInnerRect() const
{
    return contentsRect();
}

KDecoration2::Decoration* effects_window_impl::decoration() const
{
    return win::decoration(window.ref_win);
}

QByteArray effects_window_impl::readProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return render::x11::read_window_property(window.ref_win->xcb_window, atom, type, format);
}

void effects_window_impl::deleteProperty(long int atom) const
{
    if (kwinApp()->x11Connection()) {
        deleteWindowProperty(window.ref_win->xcb_window, atom);
    }
}

EffectWindow* effects_window_impl::findModal()
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

EffectWindow* effects_window_impl::transientFor()
{
    if (!window.ref_win->control) {
        return nullptr;
    }

    auto transientFor = window.ref_win->transient()->lead();
    if (transientFor) {
        return transientFor->render->effect.get();
    }

    return nullptr;
}

QWindow* effects_window_impl::internalWindow() const
{
    auto client = dynamic_cast<win::internal_window*>(window.ref_win);
    if (!client) {
        return nullptr;
    }
    return client->internalWindow();
}

template<typename T>
EffectWindowList getMainWindows(T* c)
{
    const auto leads = c->transient()->leads();
    EffectWindowList ret;
    ret.reserve(leads.size());
    std::transform(std::cbegin(leads), std::cend(leads), std::back_inserter(ret), [](auto client) {
        return client->render->effect.get();
    });
    return ret;
}

EffectWindowList effects_window_impl::mainWindows() const
{
    if (window.ref_win->control || window.ref_win->remnant) {
        return getMainWindows(window.ref_win);
    }
    return {};
}

WindowQuadList effects_window_impl::buildQuads(bool force) const
{
    return window.buildQuads(force);
}

void effects_window_impl::setData(int role, const QVariant& data)
{
    if (!data.isNull())
        dataMap[role] = data;
    else
        dataMap.remove(role);
    Q_EMIT effects->windowDataChanged(this, role);
}

QVariant effects_window_impl::data(int role) const
{
    return dataMap.value(role);
}

void effects_window_impl::elevate(bool elevate)
{
    effects->setElevatedWindow(this, elevate);
}

void effects_window_impl::registerThumbnail(basic_thumbnail_item* item)
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

void effects_window_impl::thumbnailDestroyed(QObject* object)
{
    // we know it is a window_thumbnail_item
    m_thumbnails.remove(static_cast<window_thumbnail_item*>(object));
}

void effects_window_impl::thumbnailTargetChanged()
{
    if (auto item = qobject_cast<window_thumbnail_item*>(sender())) {
        insertThumbnail(item);
    }
}

void effects_window_impl::insertThumbnail(window_thumbnail_item* item)
{
    EffectWindow* w = effects->findWindow(item->wId());
    if (w) {
        m_thumbnails.insert(item,
                            QPointer<effects_window_impl>(static_cast<effects_window_impl*>(w)));
    } else {
        m_thumbnails.insert(item, QPointer<effects_window_impl>());
    }
}

void effects_window_impl::desktopThumbnailDestroyed(QObject* object)
{
    // we know it is a desktop_thumbnail_item
    m_desktopThumbnails.removeAll(static_cast<desktop_thumbnail_item*>(object));
}

void effects_window_impl::minimize()
{
    if (window.ref_win->control) {
        win::set_minimized(window.ref_win, true);
    }
}

void effects_window_impl::unminimize()
{
    if (window.ref_win->control) {
        win::set_minimized(window.ref_win, false);
    }
}

void effects_window_impl::closeWindow()
{
    if (window.ref_win->control) {
        window.ref_win->closeWindow();
    }
}

void effects_window_impl::referencePreviousWindowPixmap()
{
    window.reference_previous_buffer();
}

void effects_window_impl::unreferencePreviousWindowPixmap()
{
    window.unreference_previous_buffer();
}

bool effects_window_impl::isManaged() const
{
    return managed;
}

bool effects_window_impl::isWaylandClient() const
{
    return waylandClient;
}

bool effects_window_impl::isX11Client() const
{
    return x11Client;
}

//****************************************
// effect_window_group_impl
//****************************************

effect_window_group_impl::effect_window_group_impl(win::x11::group* g)
    : group(g)
{
}

EffectWindowList effect_window_group_impl::members() const
{
    const auto memberList = group->members;
    EffectWindowList ret;
    ret.reserve(memberList.size());
    std::transform(std::cbegin(memberList),
                   std::cend(memberList),
                   std::back_inserter(ret),
                   [](auto toplevel) { return toplevel->render->effect.get(); });
    return ret;
}

}
