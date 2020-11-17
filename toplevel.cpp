/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

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
#include "toplevel.h"

#ifdef KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif
#include "atoms.h"
#include "client_machine.h"
#include "composite.h"
#include "effects.h"
#include "netinfo.h"
#include "screens.h"
#include "shadow.h"
#include "wayland_server.h"
#include "win/remnant.h"
#include "win/transient.h"
#include "win/win.h"
#include "workspace.h"
#include "xcbutils.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/wl_output.h>
#include <Wrapland/Server/surface.h>

#include <QDebug>

namespace KWin
{

Toplevel::Toplevel()
    : Toplevel(new win::transient(this))
{
}

Toplevel::Toplevel(win::transient* transient)
    : info(nullptr)
    , ready_for_painting(false)
    , m_isDamaged(false)
    , m_internalId(QUuid::createUuid())
    , m_client()
    , damage_handle(XCB_NONE)
    , is_shape(false)
    , effect_window(nullptr)
    , m_clientMachine(new ClientMachine(this))
    , m_wmClientLeader(XCB_WINDOW_NONE)
    , m_damageReplyPending(false)
    , m_screen(0)
    , m_skipCloseAnimation(false)
{
    m_transient.reset(transient);

    connect(this, SIGNAL(damaged(KWin::Toplevel*,QRect)), SIGNAL(needsRepaint()));
    connect(screens(), SIGNAL(changed()), SLOT(checkScreen()));
    connect(screens(), SIGNAL(countChanged(int,int)), SLOT(checkScreen()));

    setupCheckScreenConnection();
}

Toplevel::~Toplevel()
{
    Q_ASSERT(damage_handle == XCB_NONE);
    delete info;
    delete m_remnant;
}

QDebug& operator<<(QDebug& stream, const Toplevel* cl)
{
    if (cl == nullptr)
        return stream << "\'NULL\'";
    cl->debug(stream);
    return stream;
}

QRect Toplevel::decorationRect() const
{
    return rect();
}

QRect Toplevel::transparentRect() const
{
    if (m_remnant) {
        return m_remnant->transparent_rect;
    }
    return QRect(clientPos(), clientSize());
}

NET::WindowType Toplevel::windowType([[maybe_unused]] bool direct,int supported_types) const
{
    if (m_remnant) {
        return m_remnant->window_type;
    }
    if (supported_types == 0) {
        supported_types = supported_default_types;
    }

    auto wt = info->windowType(NET::WindowTypes(supported_types));
    if (direct || !control()) {
        return wt;
    }

    auto wt2 = control()->rules().checkType(wt);
    if (wt != wt2) {
        wt = wt2;
        // force hint change
        info->setWindowType(wt);
    }

    // hacks here
    if (wt == NET::Unknown) {
        // this is more or less suggested in NETWM spec
        wt = isTransient() ? NET::Dialog : NET::Normal;
    }
    return wt;
}

void Toplevel::detectShape(xcb_window_t id)
{
    const bool wasShape = is_shape;
    is_shape = Xcb::Extensions::self()->hasShape(id);
    if (wasShape != is_shape) {
        emit shapedChanged();
    }
}

Toplevel* Toplevel::create_remnant(Toplevel* source)
{
    auto win = new Toplevel();
    win->copyToDeleted(source);
    win->m_remnant = new win::remnant(win, source);
    workspace()->addDeleted(win, source);
    return win;
}

// used only by Deleted::copy()
void Toplevel::copyToDeleted(Toplevel* c)
{
    m_internalId = c->internalId();
    m_frameGeometry = c->m_frameGeometry;
    m_visual = c->m_visual;
    bit_depth = c->bit_depth;

    info = c->info;
    if (auto win_info = dynamic_cast<WinInfo*>(info)) {
        win_info->disable();
    }

    m_client.reset(c->m_client, false);
    ready_for_painting = c->ready_for_painting;
    damage_handle = XCB_NONE;
    damage_region = c->damage_region;
    repaints_region = c->repaints_region;
    layer_repaints_region = c->layer_repaints_region;
    is_shape = c->is_shape;
    effect_window = c->effect_window;
    if (effect_window != nullptr)
        effect_window->setWindow(this);
    resource_name = c->resourceName();
    resource_class = c->resourceClass();
    m_clientMachine = c->m_clientMachine;
    m_clientMachine->setParent(this);
    m_wmClientLeader = c->wmClientLeader();
    opaque_region = c->opaqueRegion();
    m_screen = c->m_screen;
    m_skipCloseAnimation = c->m_skipCloseAnimation;
    m_internalFBO = c->m_internalFBO;
    m_internalImage = c->m_internalImage;
    m_desktops = c->desktops();
    m_layer = c->layer();
}

// before being deleted, remove references to everything that's now
// owner by Deleted
void Toplevel::disownDataPassedToDeleted()
{
    info = nullptr;
}

QRect Toplevel::visibleRect() const
{
    // There's no strict order between frame geometry and buffer geometry.
    QRect rect = frameGeometry() | bufferGeometry();

    if (win::shadow(this) && !win::shadow(this)->shadowRegion().isEmpty()) {
        rect |= win::shadow(this)->shadowRegion().boundingRect().translated(pos());
    }

    return rect;
}

Xcb::Property Toplevel::fetchWmClientLeader() const
{
    return Xcb::Property(false, window(), atoms->wm_client_leader, XCB_ATOM_WINDOW, 0, 10000);
}

void Toplevel::readWmClientLeader(Xcb::Property &prop)
{
    m_wmClientLeader = prop.value<xcb_window_t>(window());
}

void Toplevel::getWmClientLeader()
{
    auto prop = fetchWmClientLeader();
    readWmClientLeader(prop);
}

/**
 * Returns sessionId for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::sessionId() const
{
    QByteArray result = Xcb::StringProperty(window(), atoms->sm_client_id);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != window()) {
        result = Xcb::StringProperty(m_wmClientLeader, atoms->sm_client_id);
    }
    return result;
}

/**
 * Returns command property for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::wmCommand()
{
    QByteArray result = Xcb::StringProperty(window(), XCB_ATOM_WM_COMMAND);
    if (result.isEmpty() && m_wmClientLeader && m_wmClientLeader != window()) {
        result = Xcb::StringProperty(m_wmClientLeader, XCB_ATOM_WM_COMMAND);
    }
    result.replace(0, ' ');
    return result;
}

void Toplevel::getWmClientMachine()
{
    m_clientMachine->resolve(window(), wmClientLeader());
}

/**
 * Returns client machine for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::wmClientMachine(bool use_localhost) const
{
    if (!m_clientMachine) {
        // this should never happen
        return QByteArray();
    }
    if (use_localhost && m_clientMachine->isLocal()) {
        // special name for the local machine (localhost)
        return ClientMachine::localhost();
    }
    return m_clientMachine->hostName();
}

/**
 * Returns client leader window for this client.
 * Returns the client window itself if no leader window is defined.
 */
xcb_window_t Toplevel::wmClientLeader() const
{
    if (m_wmClientLeader != XCB_WINDOW_NONE) {
        return m_wmClientLeader;
    }
    return window();
}

void Toplevel::getResourceClass()
{
    setResourceClass(QByteArray(info->windowClassName()).toLower(), QByteArray(info->windowClassClass()).toLower());
}

void Toplevel::setResourceClass(const QByteArray &name, const QByteArray &className)
{
    resource_name  = name;
    resource_class = className;
    emit windowClassChanged();
}

bool Toplevel::resourceMatch(const Toplevel *c1, const Toplevel *c2)
{
    return c1->resourceClass() == c2->resourceClass();
}

double Toplevel::opacity() const
{
    if (m_remnant) {
        return m_remnant->opacity;
    }
    if (info->opacity() == 0xffffffff)
        return 1.0;
    return info->opacity() * 1.0 / 0xffffffff;
}

void Toplevel::setOpacity(double new_opacity)
{
    double old_opacity = opacity();
    new_opacity = qBound(0.0, new_opacity, 1.0);
    if (old_opacity == new_opacity)
        return;
    info->setOpacity(static_cast< unsigned long >(new_opacity * 0xffffffff));
    if (win::compositing()) {
        addRepaintFull();
        emit opacityChanged(this, old_opacity);
    }
}

bool Toplevel::isOutline() const
{
    if (m_remnant) {
        return m_remnant->was_outline;
    }
    return is_outline;
}

bool Toplevel::setupCompositing(bool add_full_damage)
{
    assert(!remnant());

    if (!win::compositing())
        return false;

    if (damage_handle != XCB_NONE)
        return false;

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        assert(!surface());
        damage_handle = xcb_generate_id(connection());
        xcb_damage_create(connection(), damage_handle, frameId(), XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    }

    damage_region = QRegion(0, 0, width(), height());
    effect_window = new EffectWindowImpl(this);

    Compositor::self()->scene()->addToplevel(this);

    if (add_full_damage) {
        // With unmanaged windows there is a race condition between the client painting the window
        // and us setting up damage tracking.  If the client wins we won't get a damage event even
        // though the window has been painted.  To avoid this we mark the whole window as damaged
        // and schedule a repaint immediately after creating the damage object.
        // TODO: move this out of the class.
        addDamageFull();
    }

    return true;
}

void Toplevel::finishCompositing(ReleaseReason releaseReason)
{
    assert(!remnant());

    if (kwinApp()->operationMode() == Application::OperationModeX11 && damage_handle == XCB_NONE)
        return;

    if (effect_window->window() == this) { // otherwise it's already passed to Deleted, don't free data
        discardWindowPixmap();
        delete effect_window;
    }

    if (damage_handle != XCB_NONE &&
            releaseReason != ReleaseReason::Destroyed) {
        xcb_damage_destroy(connection(), damage_handle);
    }

    damage_handle = XCB_NONE;
    damage_region = QRegion();
    repaints_region = QRegion();
    effect_window = nullptr;
}

void Toplevel::discardWindowPixmap()
{
    addDamageFull();
    if (auto scene_window = win::scene_window(this)) {
        scene_window->discardPixmap();
    }
}

void Toplevel::damageNotifyEvent()
{
    m_isDamaged = true;

    // Note: The rect is supposed to specify the damage extents,
    //       but we don't know it at this point. No one who connects
    //       to this signal uses the rect however.
    emit damaged(this, QRect());
}

bool Toplevel::resetAndFetchDamage()
{
    if (!m_isDamaged)
        return false;

    if (damage_handle == XCB_NONE) {
        m_isDamaged = false;
        return true;
    }

    xcb_connection_t *conn = connection();

    // Create a new region and copy the damage region to it,
    // resetting the damaged state.
    xcb_xfixes_region_t region = xcb_generate_id(conn);
    xcb_xfixes_create_region(conn, region, 0, nullptr);
    xcb_damage_subtract(conn, damage_handle, 0, region);

    // Send a fetch-region request and destroy the region
    m_regionCookie = xcb_xfixes_fetch_region_unchecked(conn, region);
    xcb_xfixes_destroy_region(conn, region);

    m_isDamaged = false;
    m_damageReplyPending = true;

    return m_damageReplyPending;
}

void Toplevel::getDamageRegionReply()
{
    if (!m_damageReplyPending)
        return;

    m_damageReplyPending = false;

    // Get the fetch-region reply
    xcb_xfixes_fetch_region_reply_t *reply =
            xcb_xfixes_fetch_region_reply(connection(), m_regionCookie, nullptr);

    if (!reply)
        return;

    // Convert the reply to a QRegion
    int count = xcb_xfixes_fetch_region_rectangles_length(reply);
    QRegion region;

    if (count > 1 && count < 16) {
        xcb_rectangle_t *rects = xcb_xfixes_fetch_region_rectangles(reply);

        QVector<QRect> qrects;
        qrects.reserve(count);

        for (int i = 0; i < count; i++)
            qrects << QRect(rects[i].x, rects[i].y, rects[i].width, rects[i].height);

        region.setRects(qrects.constData(), count);
    } else
        region += QRect(reply->extents.x, reply->extents.y,
                        reply->extents.width, reply->extents.height);

    const QRect bufferRect = bufferGeometry();
    const QRect frameRect = frameGeometry();

    damage_region += region;
    repaints_region += region.translated(bufferRect.topLeft() - frameRect.topLeft());

    free(reply);
}

void Toplevel::addDamageFull()
{
    if (!win::compositing())
        return;

    const QRect bufferRect = bufferGeometry();
    const QRect frameRect = frameGeometry();

    const int offsetX = bufferRect.x() - frameRect.x();
    const int offsetY = bufferRect.y() - frameRect.y();

    const QRect damagedRect = QRect(0, 0, bufferRect.width(), bufferRect.height());

    damage_region = damagedRect;
    repaints_region |= damagedRect.translated(offsetX, offsetY);

    emit damaged(this, damagedRect);
}

void Toplevel::resetDamage()
{
    damage_region = QRegion();
}

void Toplevel::addRepaint(const QRect& r)
{
    if (!win::compositing()) {
        return;
    }
    repaints_region += r;
    emit needsRepaint();
}

void Toplevel::addRepaint(int x, int y, int w, int h)
{
    QRect r(x, y, w, h);
    addRepaint(r);
}

void Toplevel::addRepaint(const QRegion& r)
{
    if (!win::compositing()) {
        return;
    }
    repaints_region += r;
    emit needsRepaint();
}

void Toplevel::addLayerRepaint(const QRect& r)
{
    if (!win::compositing()) {
        return;
    }
    layer_repaints_region += r;
    emit needsRepaint();
}

void Toplevel::addLayerRepaint(int x, int y, int w, int h)
{
    QRect r(x, y, w, h);
    addLayerRepaint(r);
}

void Toplevel::addLayerRepaint(const QRegion& r)
{
    if (!win::compositing())
        return;
    layer_repaints_region += r;
    emit needsRepaint();
}

void Toplevel::addRepaintFull()
{
    repaints_region = visibleRect().translated(-pos());
    emit needsRepaint();
}

bool Toplevel::has_pending_repaints() const
{
    return !repaints().isEmpty();
}

QRegion Toplevel::repaints() const
{
    return repaints_region.translated(pos()) | layer_repaints_region;
}

void Toplevel::resetRepaints()
{
    repaints_region = QRegion();
    layer_repaints_region = QRegion();
}

void Toplevel::addWorkspaceRepaint(int x, int y, int w, int h)
{
    addWorkspaceRepaint(QRect(x, y, w, h));
}

void Toplevel::addWorkspaceRepaint(const QRect& r2)
{
    if (!win::compositing())
        return;
    Compositor::self()->addRepaint(r2);
}

void Toplevel::setReadyForPainting()
{
    if (!ready_for_painting) {
        ready_for_painting = true;
        if (win::compositing()) {
            addRepaintFull();
            emit windowShown(this);
        }
    }
}

void Toplevel::deleteEffectWindow()
{
    delete effect_window;
    effect_window = nullptr;
}

void Toplevel::checkScreen()
{
    if (screens()->count() == 1) {
        if (m_screen != 0) {
            m_screen = 0;
            emit screenChanged();
        }
    } else {
        const int s = screens()->number(frameGeometry().center());
        if (s != m_screen) {
            m_screen = s;
            emit screenChanged();
        }
    }
    qreal newScale = screens()->scale(m_screen);
    if (newScale != m_screenScale) {
        m_screenScale = newScale;
        emit screenScaleChanged();
    }
}

void Toplevel::setupCheckScreenConnection()
{
    connect(this, SIGNAL(geometryShapeChanged(KWin::Toplevel*,QRect)), SLOT(checkScreen()));
    connect(this, SIGNAL(geometryChanged()), SLOT(checkScreen()));
    checkScreen();
}

void Toplevel::removeCheckScreenConnection()
{
    disconnect(this, SIGNAL(geometryShapeChanged(KWin::Toplevel*,QRect)), this, SLOT(checkScreen()));
    disconnect(this, SIGNAL(geometryChanged()), this, SLOT(checkScreen()));
}

int Toplevel::screen() const
{
    return m_screen;
}

qreal Toplevel::screenScale() const
{
    return m_screenScale;
}

qreal Toplevel::bufferScale() const
{
    if (m_remnant) {
        return m_remnant->buffer_scale;
    }
    return surface() ? surface()->scale() : 1;
}

QPoint Toplevel::clientPos() const
{
    if (m_remnant) {
        return m_remnant->contents_rect.topLeft();
    }
    return QPoint(win::left_border(this), win::top_border(this));
}

bool Toplevel::wantsShadowToBeRendered() const
{
    return true;
}

void Toplevel::getWmOpaqueRegion()
{
    const auto rects = info->opaqueRegion();
    QRegion new_opaque_region;
    for (const auto &r : rects) {
        new_opaque_region += QRect(r.pos.x, r.pos.y, r.size.width, r.size.height);
    }

    opaque_region = new_opaque_region;
}

bool Toplevel::isClient() const
{
    return false;
}

bool Toplevel::isDeleted() const
{
    return remnant() != nullptr;
}

bool Toplevel::isOnCurrentActivity() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return true;
    }
    return isOnActivity(Activities::self()->current());
#else
    return true;
#endif
}

pid_t Toplevel::pid() const
{
    return info->pid();
}

xcb_window_t Toplevel::frameId() const
{
    if (m_remnant) {
        return m_remnant->frame;
    }
    return m_client;
}

void Toplevel::getSkipCloseAnimation()
{
    setSkipCloseAnimation(win::fetch_skip_close_animation(window()).toBool());
}

void Toplevel::debug(QDebug& stream) const
{
    if (remnant()) {
        stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
    } else {
        stream << "\'ID:" << reinterpret_cast<void const*>(this) << window() << "\'";
    }
}

bool Toplevel::skipsCloseAnimation() const
{
    return m_skipCloseAnimation;
}

void Toplevel::setSkipCloseAnimation(bool set)
{
    if (set == m_skipCloseAnimation) {
        return;
    }
    m_skipCloseAnimation = set;
    emit skipCloseAnimationChanged();
}

void Toplevel::setSurface(Wrapland::Server::Surface *surface)
{
    using namespace Wrapland::Server;
    Q_ASSERT(surface);

    if (m_surface) {
        // This can happen with XWayland clients since receiving the surface destroy signal through
        // the Wayland connection is independent of when the corresponding X11 unmap/map events
        // are received.
        disconnect(m_surface, nullptr, this, nullptr);

        disconnect(this, &Toplevel::geometryChanged, this, &Toplevel::updateClientOutputs);
        disconnect(screens(), &Screens::changed, this, &Toplevel::updateClientOutputs);
    } else {
        // Need to setup this connections since setSurface was never called before or
        // the surface had been destroyed before what disconnected them.
        connect(this, &Toplevel::geometryChanged, this, &Toplevel::updateClientOutputs);
        connect(screens(), &Screens::changed, this, &Toplevel::updateClientOutputs);
    }

    m_surface = surface;

    connect(m_surface, &Surface::damaged, this, &Toplevel::addDamage);
    connect(m_surface, &Surface::sizeChanged,
            this, &Toplevel::handleXwaylandSurfaceSizeChange);

    connect(m_surface, &Surface::subsurfaceTreeChanged, this,
        [this] {
            // TODO improve to only update actual visual area
            if (ready_for_painting) {
                addDamageFull();
                m_isDamaged = true;
            }
        }
    );
    connect(m_surface, &Surface::destroyed, this,
        [this] {
            m_surface = nullptr;
            disconnect(this, &Toplevel::geometryChanged, this, &Toplevel::updateClientOutputs);
            disconnect(screens(), &Screens::changed, this, &Toplevel::updateClientOutputs);
        }
    );
    updateClientOutputs();
    emit surfaceChanged();
}

void Toplevel::handleXwaylandSurfaceSizeChange()
{
    discardWindowPixmap();
    Q_EMIT geometryShapeChanged(this, frameGeometry());
}

void Toplevel::updateClientOutputs()
{
    std::vector<Wrapland::Server::Output*> clientOutputs;
    const auto outputs = waylandServer()->display()->outputs();
    for (auto output : outputs) {
        if (frameGeometry().intersects(output->output()->geometry().toRect())) {
            clientOutputs.push_back(output->output());
        }
    }
    surface()->setOutputs(clientOutputs);
}

void Toplevel::addDamage(const QRegion &damage)
{
    repaints_region += damage.translated(bufferGeometry().topLeft() - frameGeometry().topLeft());
    m_isDamaged = true;
    damage_region += damage;
    for (const QRect &r : damage) {
        emit damaged(this, r);
    }
}

QByteArray Toplevel::windowRole() const
{
    if (m_remnant) {
        return m_remnant->window_role;
    }
    return QByteArray(info->windowRole());
}

void Toplevel::setDepth(int depth)
{
    if (bit_depth == depth) {
        return;
    }
    const bool oldAlpha = hasAlpha();
    bit_depth = depth;
    if (oldAlpha != hasAlpha()) {
        emit hasAlphaChanged();
    }
}

QRegion Toplevel::inputShape() const
{
    if (m_surface) {
        return m_surface->input();
    } else {
        // TODO: maybe also for X11?
        return QRegion();
    }
}

QMatrix4x4 Toplevel::inputTransformation() const
{
    QMatrix4x4 m;
    m.translate(-x(), -y());
    return m;
}

quint32 Toplevel::windowId() const
{
    return window();
}

void Toplevel::set_frame_geometry(QRect const& rect)
{
    m_frameGeometry = rect;
}

QRect Toplevel::bufferGeometry() const
{
    if (m_remnant) {
        return m_remnant->buffer_geometry;
    }
    return frameGeometry();
}

QRect Toplevel::inputGeometry() const
{
    if (auto const& ctrl = control()) {
        if (auto const& deco = ctrl->deco(); deco.enabled()) {
            return frameGeometry() + deco.decoration->resizeOnlyBorders();
        }
    }

    return frameGeometry();
}

QSize Toplevel::clientSize() const
{
    if (m_remnant) {
        return m_remnant->contents_rect.size();
    }
    return size();
}


QPoint Toplevel::clientContentPos() const
{
    if (m_remnant) {
        return m_remnant->content_pos;
    }
    return QPoint(0, 0);
}

bool Toplevel::isLocalhost() const
{
    if (!m_clientMachine) {
        return true;
    }
    return m_clientMachine->isLocal();
}

QMargins Toplevel::bufferMargins() const
{
    if (m_remnant) {
        return m_remnant->buffer_margins;
    }
    return QMargins();
}

QMargins Toplevel::frameMargins() const
{
    if (m_remnant) {
        return m_remnant->frame_margins;
    }

    if (control()) {
        return QMargins(win::left_border(this), win::top_border(this),
                        win::right_border(this), win::bottom_border(this));
    } else {
        return QMargins();
    }
}

bool Toplevel::is_popup_end() const
{
    if (m_remnant) {
        return m_remnant->was_popup_window;
    }
    return false;
}

int Toplevel::desktop() const
{
    // TODO: for remnant special case?
    return m_desktops.isEmpty() ? (int)NET::OnAllDesktops : m_desktops.last()->x11DesktopNumber();
}

QVector<VirtualDesktop *> Toplevel::desktops() const
{
    return m_desktops;
}

void Toplevel::set_desktops(QVector<VirtualDesktop*> const& desktops)
{
    m_desktops = desktops;
}

bool Toplevel::isOnAllActivities() const
{
    return win::on_all_activities(this);
}

bool Toplevel::isOnActivity(const QString &activity) const
{
    return win::on_activity(this, activity);
}

bool Toplevel::isOnAllDesktops() const
{
    return win::on_all_desktops(this);
}

bool Toplevel::isOnDesktop(int d) const
{
    return win::on_desktop(this, d);
}

bool Toplevel::isOnCurrentDesktop() const
{
    return win::on_current_desktop(this);
}

QStringList Toplevel::activities() const
{
    if (m_remnant) {
        return m_remnant->activities;
    }
    return QStringList();
}

win::layer Toplevel::layer() const
{
    if (m_layer == win::layer::unknown) {
        const_cast<Toplevel*>(this)->m_layer = win::belong_to_layer(this);
    }
    return m_layer;
}

void Toplevel::set_layer(win::layer layer)
{
    m_layer = layer;;
}

win::layer Toplevel::layer_for_dock() const
{
    assert(control());

    // Slight hack for the 'allow window to cover panel' Kicker setting.
    // Don't move keepbelow docks below normal window, but only to the same
    // layer, so that both may be raised to cover the other.
    if (control()->keep_below()) {
        return win::layer::normal;
    }
    if (control()->keep_above()) {
        // slight hack for the autohiding panels
        return win::layer::above;
    }
    return win::layer::dock;
}

bool Toplevel::isInternal() const
{
    return false;
}

bool Toplevel::belongsToDesktop() const
{
    return false;
}

void Toplevel::checkTransient([[maybe_unused]] xcb_window_t window)
{
}

win::remnant* Toplevel::remnant() const
{
    return m_remnant;
}

win::transient* Toplevel::transient() const
{
    return m_transient.get();
}

QString Toplevel::captionNormal() const
{
    return QString();
}

QString Toplevel::captionSuffix() const
{
    return QString();
}

bool Toplevel::isCloseable() const
{
    return false;
}

bool Toplevel::isShown([[maybe_unused]] bool shaded_is_shown) const
{
    return false;
}

bool Toplevel::isHiddenInternal() const
{
    return false;
}

void Toplevel::hideClient([[maybe_unused]] bool hide)
{
}

void Toplevel::setFullScreen([[maybe_unused]] bool set, [[maybe_unused]] bool user)
{
}

void Toplevel::setClientShown([[maybe_unused]] bool shown)
{
}

QRect Toplevel::geometryRestore() const
{
    return QRect();
}

win::maximize_mode Toplevel::maximizeMode() const
{
    return win::maximize_mode::restore;
}

win::maximize_mode Toplevel::requestedMaximizeMode() const
{
    return maximizeMode();
}

bool Toplevel::noBorder() const
{
    if (m_remnant) {
        return m_remnant->no_border;
    }
    return true;
}

void Toplevel::setNoBorder([[maybe_unused]] bool set)
{
}

void Toplevel::blockActivityUpdates([[maybe_unused]] bool b)
{
}

bool Toplevel::isResizable() const
{
    return false;
}

bool Toplevel::isMovable() const
{
    return false;
}

bool Toplevel::isMovableAcrossScreens() const
{
    return false;
}

bool Toplevel::isShadeable() const
{
    return false;
}

void Toplevel::setShade([[maybe_unused]] win::shade mode)
{
}

win::shade Toplevel::shadeMode() const
{
    return win::shade::none;
}

void Toplevel::takeFocus()
{
}

bool Toplevel::wantsInput() const
{
    return false;
}

bool Toplevel::dockWantsInput() const
{
    return false;
}

bool Toplevel::isMaximizable() const
{
    return false;
}

bool Toplevel::isMinimizable() const
{
    return false;
}

bool Toplevel::userCanSetFullScreen() const
{
    return false;
}

bool Toplevel::userCanSetNoBorder() const
{
    return false;
}

void Toplevel::checkNoBorder()
{
    setNoBorder(false);
}

bool Toplevel::isTransient() const
{
    return transient()->lead();
}

bool Toplevel::hasTransientPlacementHint() const
{
    return false;
}

QRect Toplevel::transientPlacement([[maybe_unused]] QRect const& bounds) const
{
    Q_UNREACHABLE();
    return QRect();
}

void Toplevel::setOnActivities([[maybe_unused]] QStringList newActivitiesList)
{
}

void Toplevel::setOnAllActivities([[maybe_unused]] bool set)
{
}

xcb_timestamp_t Toplevel::userTime() const
{
    return XCB_TIME_CURRENT_TIME;
}

void Toplevel::resizeWithChecks([[maybe_unused]] QSize const& size,
                                [[maybe_unused]] win::force_geometry force)
{
}

QSize Toplevel::maxSize() const
{
    return control()->rules().checkMaxSize(QSize(INT_MAX, INT_MAX));
}

QSize Toplevel::minSize() const
{
    return control()->rules().checkMinSize(QSize(0, 0));
}

void Toplevel::setFrameGeometry([[maybe_unused]] QRect const& rect,
                                [[maybe_unused]] win::force_geometry force)
{
}

QSize Toplevel::sizeForClientSize(QSize const& wsize,
                                  [[maybe_unused]] win::size_mode mode,
                                  [[maybe_unused]] bool noframe) const
{
    return wsize + QSize(win::left_border(this) + win::right_border(this),
                         win::top_border(this) + win::bottom_border(this));
}

QPoint Toplevel::framePosToClientPos(QPoint const& point) const
{
    auto const offset = win::decoration(this)
        ? QPoint(win::left_border(this), win::top_border(this))
        : -QPoint(client_frame_extents.left(), client_frame_extents.top());

    return point + offset;
}

QPoint Toplevel::clientPosToFramePos(QPoint const& point) const
{
    auto const offset = win::decoration(this)
        ? -QPoint(win::left_border(this), win::top_border(this))
        : QPoint(client_frame_extents.left(), client_frame_extents.top());

    return point + offset;
}

QSize Toplevel::frameSizeToClientSize(QSize const& size) const
{
    auto const offset = win::decoration(this)
        ? QSize(-win::left_border(this) - win::right_border(this),
                -win::top_border(this) - win::bottom_border(this))
        : QSize(client_frame_extents.left() + client_frame_extents.right(),
                client_frame_extents.top() + client_frame_extents.bottom());

    return size + offset;
}

QSize Toplevel::clientSizeToFrameSize(QSize const& size) const
{
    auto const offset = win::decoration(this)
        ? QSize(win::left_border(this) + win::right_border(this),
                 win::top_border(this) + win::bottom_border(this))
        : QSize(-client_frame_extents.left() - client_frame_extents.right(),
                -client_frame_extents.top() - client_frame_extents.bottom());

    return size + offset;
}

bool Toplevel::hasStrut() const
{
    return false;
}

void Toplevel::updateDecoration([[maybe_unused]] bool check_workspace_pos,
                                [[maybe_unused]] bool force)
{
}

void Toplevel::layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const
{
    if (m_remnant) {
        return m_remnant->layout_decoration_rects(left, top, right, bottom);
    }
    win::layout_decoration_rects(this, left, top, right, bottom);
}

bool Toplevel::providesContextHelp() const
{
    return false;
}

void Toplevel::showContextHelp()
{
}

void Toplevel::showOnScreenEdge()
{
}

void Toplevel::killWindow()
{
}

bool Toplevel::isInitialPositionSet() const
{
    return false;
}

bool Toplevel::groupTransient() const
{
    return false;
}

Group const* Toplevel::group() const
{
    return nullptr;
}

Group* Toplevel::group()
{
    return nullptr;
}

bool Toplevel::supportsWindowRules() const
{
    return control() != nullptr;
}

QSize Toplevel::basicUnit() const
{
    return QSize(1, 1);
}

void Toplevel::setBlockingCompositing([[maybe_unused]] bool block)
{
}

bool Toplevel::isBlockingCompositing()
{
    return false;
}

bool Toplevel::doStartMoveResize()
{
    return true;
}

void Toplevel::doPerformMoveResize()
{
}

void Toplevel::leaveMoveResize()
{
    workspace()->setMoveResizeClient(nullptr);
    control()->move_resize().enabled = false;
    if (ScreenEdges::self()->isDesktopSwitchingMovingClients()) {
        ScreenEdges::self()->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    }
    if (control()->electric_maximizing()) {
        outline()->hide();
        win::elevate(this, false);
    }
}

void Toplevel::doResizeSync()
{
}

void Toplevel::doSetActive()
{
}

void Toplevel::doSetKeepAbove()
{
}

void Toplevel::doSetKeepBelow()
{
}

void Toplevel::doMinimize()
{
}

void Toplevel::doSetDesktop([[maybe_unused]] int desktop, [[maybe_unused]] int was_desk)
{
}

bool Toplevel::isWaitingForMoveResizeSync() const
{
    return false;
}

void Toplevel::positionGeometryTip()
{
}

QSize Toplevel::resizeIncrements() const
{
    return QSize(1, 1);
}

void Toplevel::updateColorScheme()
{
}

void Toplevel::updateCaption()
{
}

void Toplevel::setGeometryRestore([[maybe_unused]] QRect const& geo)
{
}

bool Toplevel::acceptsFocus() const
{
    return false;
}

void Toplevel::changeMaximize([[maybe_unused]] bool horizontal, [[maybe_unused]] bool vertical,
                              [[maybe_unused]] bool adjust)
{
}

void Toplevel::closeWindow()
{
}

bool Toplevel::performMouseCommand(Options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(this, cmd, globalPos);
}

Toplevel* Toplevel::findModal()
{
    return nullptr;
}

bool Toplevel::belongsToSameApplication([[maybe_unused]] Toplevel const* other,
                                        [[maybe_unused]] win::same_client_check checks) const
{
    return false;
}

QRect Toplevel::iconGeometry() const
{
    auto management = control()->wayland_management();
    if (!management || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    int minDistance = INT_MAX;
    Toplevel* candidatePanel = nullptr;
    QRect candidateGeom;

    for (auto i = management->minimizedGeometries().constBegin(),
         end = management->minimizedGeometries().constEnd(); i != end; ++i) {
        auto client = waylandServer()->findToplevel(i.key());
        if (!client) {
            continue;
        }
        const int distance = QPoint(client->pos() - pos()).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            candidatePanel = client;
            candidateGeom = i.value();
        }
    }
    if (!candidatePanel) {
        return QRect();
    }
    return candidateGeom.translated(candidatePanel->pos());
}

void Toplevel::setWindowHandles(xcb_window_t w)
{
    Q_ASSERT(!m_client.isValid() && w != XCB_WINDOW_NONE);
    m_client.reset(w, false);
}

} // namespace

