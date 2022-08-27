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

effects_window_impl::effects_window_impl(Toplevel* toplevel)
    : EffectWindow(toplevel->qobject.get())
    , toplevel(toplevel)
    , sw(nullptr)
{
    // Deleted windows are not managed. So, when windowClosed signal is
    // emitted, effects can't distinguish managed windows from unmanaged
    // windows(e.g. combo box popups, popup menus, etc). Save value of the
    // managed property during construction of EffectWindow. At that time,
    // parent can be Client, XdgShellClient, or Unmanaged. So, later on, when
    // an instance of Deleted becomes parent of the EffectWindow, effects
    // can still figure out whether it is/was a managed window.
    managed = toplevel->isClient();

    waylandClient = toplevel->is_wayland_window();
    x11Client = dynamic_cast<win::x11::window*>(toplevel) != nullptr || toplevel->xcb_window;
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
    return sceneWindow()->isPaintingEnabled();
}

void effects_window_impl::enablePainting(int reason)
{
    sceneWindow()->enablePainting(static_cast<render::window_paint_disable_type>(reason));
}

void effects_window_impl::disablePainting(int reason)
{
    sceneWindow()->disablePainting(static_cast<render::window_paint_disable_type>(reason));
}

void effects_window_impl::addRepaint(const QRect& r)
{
    toplevel->addRepaint(r);
}

void effects_window_impl::addRepaint(int x, int y, int w, int h)
{
    addRepaint(QRect(x, y, w, h));
}

void effects_window_impl::addRepaintFull()
{
    toplevel->addRepaintFull();
}

void effects_window_impl::addLayerRepaint(const QRect& r)
{
    toplevel->addLayerRepaint(r);
}

void effects_window_impl::addLayerRepaint(int x, int y, int w, int h)
{
    addLayerRepaint(QRect(x, y, w, h));
}

const EffectWindowGroup* effects_window_impl::group() const
{
    if (auto c = dynamic_cast<win::x11::window*>(toplevel); c && c->group()) {
        return c->group()->effect_group;
    }
    return nullptr; // TODO
}

void effects_window_impl::refWindow()
{
    if (toplevel->transient()->annexed) {
        return;
    }
    if (auto& remnant = toplevel->remnant) {
        return remnant->ref();
    }
    abort(); // TODO
}

void effects_window_impl::unrefWindow()
{
    if (toplevel->transient()->annexed) {
        return;
    }
    if (auto& remnant = toplevel->remnant) {
        // delays deletion in case
        return remnant->unref();
    }
    abort(); // TODO
}

QRect effects_window_impl::rect() const
{
    return QRect(QPoint(), toplevel->size());
}

#define TOPLEVEL_HELPER(rettype, prototype, toplevelPrototype)                                     \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        return toplevel->toplevelPrototype();                                                      \
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
        return win::function(toplevel);                                                            \
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
        if (toplevel->control || toplevel->remnant) {                                              \
            return win::propertyname(toplevel);                                                    \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER_WITH_DELETED_WIN(QString, caption, caption, QString())
CLIENT_HELPER_WITH_DELETED_WIN(QVector<uint>, desktops, x11_desktop_ids, QVector<uint>())

#undef CLIENT_HELPER_WITH_DELETED_WIN

#define CLIENT_HELPER_WITH_DELETED_WIN_CTRL(rettype, prototype, propertyname, defaultValue)        \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control) {                                                                   \
            return toplevel->control->propertyname();                                              \
        }                                                                                          \
        if (auto& remnant = toplevel->remnant) {                                                   \
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
    return static_cast<bool>(toplevel->remnant);
}

Wrapland::Server::Surface* effects_window_impl::surface() const
{
    return toplevel->surface;
}

QStringList effects_window_impl::activities() const
{
    // No support for Activities.
    return {};
}

int effects_window_impl::screen() const
{
    if (!toplevel->central_output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(), *toplevel->central_output);
}

QRect effects_window_impl::clientGeometry() const
{
    return win::frame_to_client_rect(toplevel, toplevel->frameGeometry());
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
    return expanded_geometry_recursion(toplevel);
}

// legacy from tab groups, can be removed when no effects use this any more.
bool effects_window_impl::isCurrentTab() const
{
    return true;
}

QString effects_window_impl::windowClass() const
{
    return toplevel->resource_name + QLatin1Char(' ') + toplevel->resource_class;
}

QRect effects_window_impl::contentsRect() const
{
    // TODO(romangg): This feels kind of wrong. Why are the frame extents not part of it (i.e. just
    //                using frame_to_client_rect)? But some clients rely on the current version,
    //                for example Latte for its behind-dock blur.
    auto const deco_offset = QPoint(win::left_border(toplevel), win::top_border(toplevel));
    auto const client_size = win::frame_relative_client_rect(toplevel).size();

    return QRect(deco_offset, client_size);
}

NET::WindowType effects_window_impl::windowType() const
{
    return toplevel->windowType();
}

#define CLIENT_HELPER(rettype, prototype, propertyname, defaultValue)                              \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control) {                                                                   \
            return toplevel->propertyname();                                                       \
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
        if (toplevel->control) {                                                                   \
            return win::function(toplevel);                                                        \
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
        if (toplevel->control) {                                                                   \
            return toplevel->control->function();                                                  \
        }                                                                                          \
        return default_value;                                                                      \
    }

CLIENT_HELPER_WIN_CONTROL(bool, isSkipSwitcher, skip_switcher, false)
CLIENT_HELPER_WIN_CONTROL(QIcon, icon, icon, QIcon())
CLIENT_HELPER_WIN_CONTROL(bool, isUnresponsive, unresponsive, false)

#undef CLIENT_HELPER_WIN_CONTROL

QSize effects_window_impl::basicUnit() const
{
    if (auto client = dynamic_cast<win::x11::window*>(toplevel)) {
        return client->basicUnit();
    }
    return QSize(1, 1);
}

void effects_window_impl::setWindow(Toplevel* w)
{
    toplevel = w;
    setParent(w->qobject.get());
}

void effects_window_impl::setSceneWindow(render::window* w)
{
    sw = w;
}

QRect effects_window_impl::decorationInnerRect() const
{
    return contentsRect();
}

KDecoration2::Decoration* effects_window_impl::decoration() const
{
    return win::decoration(toplevel);
}

QByteArray effects_window_impl::readProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return render::x11::read_window_property(window()->xcb_window, atom, type, format);
}

void effects_window_impl::deleteProperty(long int atom) const
{
    if (kwinApp()->x11Connection()) {
        deleteWindowProperty(window()->xcb_window, atom);
    }
}

EffectWindow* effects_window_impl::findModal()
{
    if (!toplevel->control) {
        return nullptr;
    }

    auto modal = toplevel->findModal();
    if (modal) {
        return modal->render->effect.get();
    }

    return nullptr;
}

EffectWindow* effects_window_impl::transientFor()
{
    if (!toplevel->control) {
        return nullptr;
    }

    auto transientFor = toplevel->transient()->lead();
    if (transientFor) {
        return transientFor->render->effect.get();
    }

    return nullptr;
}

QWindow* effects_window_impl::internalWindow() const
{
    auto client = dynamic_cast<win::internal_window*>(toplevel);
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
    if (toplevel->control || toplevel->remnant) {
        return getMainWindows(toplevel);
    }
    return {};
}

WindowQuadList effects_window_impl::buildQuads(bool force) const
{
    return sceneWindow()->buildQuads(force);
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
    if (toplevel->control) {
        win::set_minimized(toplevel, true);
    }
}

void effects_window_impl::unminimize()
{
    if (toplevel->control) {
        win::set_minimized(toplevel, false);
    }
}

void effects_window_impl::closeWindow()
{
    if (toplevel->control) {
        toplevel->closeWindow();
    }
}

void effects_window_impl::referencePreviousWindowPixmap()
{
    if (sw) {
        sw->reference_previous_buffer();
    }
}

void effects_window_impl::unreferencePreviousWindowPixmap()
{
    if (sw) {
        sw->unreference_previous_buffer();
    }
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
