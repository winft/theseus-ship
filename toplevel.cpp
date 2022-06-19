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

#include "base/output.h"
#include "base/output_helpers.h"
#include "base/platform.h"
#include "render/compositor.h"
#include "render/effects.h"
#include "shadow.h"

#include "win/input.h"
#include "win/remnant.h"
#include "win/scene.h"
#include "win/space.h"
#include "win/space_helpers.h"
#include "win/transient.h"

#include <QDebug>

namespace KWin
{

Toplevel::Toplevel(win::space& space)
    : Toplevel(new win::transient(this), space)
{
}

Toplevel::Toplevel(win::transient* transient, win::space& space)
    : space{space}
    , internal_id{QUuid::createUuid()}
    , m_damageReplyPending(false)
    , m_skipCloseAnimation(false)
{
    m_transient.reset(transient);

    connect(this, &Toplevel::frame_geometry_changed, this, [this](auto win, auto const& old_geo) {
        if (win::render_geometry(win).size() == win::frame_to_render_rect(win, old_geo).size()) {
            // Size unchanged. No need to update.
            return;
        }
        discard_shape();
        Q_EMIT visible_geometry_changed();
    });

    connect(this, &Toplevel::damaged, this, &Toplevel::needsRepaint);

    auto& base = kwinApp()->get_base();
    QObject::connect(
        &kwinApp()->get_base(), &base::platform::topology_changed, this, &Toplevel::checkScreen);
    QObject::connect(&base, &base::platform::output_added, this, &Toplevel::handle_output_added);
    QObject::connect(
        &base, &base::platform::output_removed, this, &Toplevel::handle_output_removed);

    setupCheckScreenConnection();
}

Toplevel::~Toplevel()
{
    delete client_machine;
    delete info;
}

QDebug& operator<<(QDebug& stream, const Toplevel* cl)
{
    if (cl == nullptr)
        return stream << "\'NULL\'";
    cl->debug(stream);
    return stream;
}

NET::WindowType Toplevel::windowType([[maybe_unused]] bool direct,int supported_types) const
{
    if (remnant) {
        return remnant->window_type;
    }
    if (supported_types == 0) {
        supported_types = supported_default_types;
    }

    auto wt = info->windowType(NET::WindowTypes(supported_types));
    if (direct || !control) {
        return wt;
    }

    auto wt2 = control->rules().checkType(wt);
    if (wt != wt2) {
        wt = wt2;
        // force hint change
        info->setWindowType(wt);
    }

    // hacks here
    if (wt == NET::Unknown) {
        // this is more or less suggested in NETWM spec
        wt = transient()->lead() ? NET::Dialog : NET::Normal;
    }
    return wt;
}

// used only by Deleted::copy()
void Toplevel::copyToDeleted(Toplevel* c)
{
    internal_id = c->internal_id;
    m_frameGeometry = c->m_frameGeometry;
    xcb_visual = c->xcb_visual;
    bit_depth = c->bit_depth;

    info = c->info;
    if (auto win_info = dynamic_cast<win::x11::win_info*>(info)) {
        win_info->disable();
    }

    xcb_window.reset(c->xcb_window, false);
    ready_for_painting = c->ready_for_painting;
    damage_handle = XCB_NONE;
    damage_region = c->damage_region;
    repaints_region = c->repaints_region;
    layer_repaints_region = c->layer_repaints_region;
    is_shape = c->is_shape;

    render = std::move(c->render);
    if (render) {
        render->effect->setWindow(this);
    }

    resource_name = c->resource_name;
    resource_class = c->resource_class;

    client_machine = c->client_machine;
    m_wmClientLeader = c->wmClientLeader();

    opaque_region = c->opaque_region;
    central_output = c->central_output;
    m_skipCloseAnimation = c->m_skipCloseAnimation;
    m_desktops = c->desktops();
    m_layer = c->layer();
    has_in_content_deco = c->has_in_content_deco;
    client_frame_extents = c->client_frame_extents;
}

// before being deleted, remove references to everything that's now
// owner by Deleted
void Toplevel::disownDataPassedToDeleted()
{
    client_machine = nullptr;
    info = nullptr;
}

/**
 * Returns client machine for this client,
 * taken either from its window or from the leader window.
 */
QByteArray Toplevel::wmClientMachine(bool use_localhost) const
{
    if (!client_machine) {
        return QByteArray();
    }
    if (use_localhost && client_machine->is_local()) {
        // special name for the local machine (localhost)
        return win::x11::client_machine::localhost();
    }
    return client_machine->hostname();
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
    return xcb_window;
}

void Toplevel::setResourceClass(const QByteArray &name, const QByteArray &className)
{
    resource_name  = name;
    resource_class = className;
    Q_EMIT windowClassChanged();
}

double Toplevel::opacity() const
{
    if (remnant) {
        return remnant->opacity;
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
    if (space.compositing()) {
        addRepaintFull();
        Q_EMIT opacityChanged(this, old_opacity);
    }
}

bool Toplevel::isOutline() const
{
    if (remnant) {
        return remnant->was_outline;
    }
    return is_outline;
}

bool Toplevel::setupCompositing()
{
    // Should never be called, always through the child classes instead.
    assert(false);
    return false;
}

void Toplevel::add_scene_window_addon()
{
}

void Toplevel::finishCompositing()
{
    assert(!remnant);

    if (render) {
        discard_buffer();
        render.reset();
    }

    damage_region = QRegion();
    repaints_region = QRegion();
}

void Toplevel::discard_buffer()
{
    addDamageFull();
    if (render) {
        render->discard_buffer();
    }
}

void Toplevel::damageNotifyEvent()
{
    m_isDamaged = true;

    // Note: The region is supposed to specify the damage extents,
    //       but we don't know it at this point. No one who connects
    //       to this signal uses the rect however.
    Q_EMIT damaged(this, {});
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
    if (!m_damageReplyPending) {
        return;
    }

    m_damageReplyPending = false;

    // Get the fetch-region reply
    auto reply = xcb_xfixes_fetch_region_reply(connection(), m_regionCookie, nullptr);
    if (!reply) {
        return;
    }

    // Convert the reply to a QRegion. The region is relative to the content geometry.
    auto count = xcb_xfixes_fetch_region_rectangles_length(reply);
    QRegion region;

    if (count > 1 && count < 16) {
        auto rects = xcb_xfixes_fetch_region_rectangles(reply);

        QVector<QRect> qrects;
        qrects.reserve(count);

        for (int i = 0; i < count; i++) {
            qrects << QRect(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        }
        region.setRects(qrects.constData(), count);
    } else {
        region += QRect(reply->extents.x, reply->extents.y,
                        reply->extents.width, reply->extents.height);
    }

    region.translate(-QPoint(client_frame_extents.left(), client_frame_extents.top()));
    repaints_region |= region;

    if (has_in_content_deco) {
        region.translate(-QPoint(win::left_border(this), win::top_border(this)));
    }
    damage_region |= region;

    free(reply);
}

void Toplevel::addDamageFull()
{
    if (!space.compositing()) {
        return;
    }

    auto const render_geo = win::frame_to_render_rect(this, frameGeometry());

    auto const damage = QRect(QPoint(), render_geo.size());
    damage_region = damage;

    auto repaint = damage;
    if (has_in_content_deco) {
        repaint.translate(-QPoint(win::left_border(this), win::top_border(this)));
    }
    repaints_region |= repaint;
    add_repaint_outputs(render_geo);

    Q_EMIT damaged(this, damage_region);
}

void Toplevel::resetDamage()
{
    damage_region = QRegion();
}

void Toplevel::addRepaint(QRegion const& region)
{
    if (!space.compositing()) {
        return;
    }
    repaints_region += region;
    add_repaint_outputs(region.translated(pos()));
    Q_EMIT needsRepaint();
}

void Toplevel::addLayerRepaint(QRegion const& region)
{
    if (!space.compositing()) {
        return;
    }
    layer_repaints_region += region;
    add_repaint_outputs(region);
    Q_EMIT needsRepaint();
}

void Toplevel::addRepaintFull()
{
    auto const region = win::visible_rect(this);
    repaints_region = region.translated(-pos());
    for (auto child : transient()->children) {
        if (child->transient()->annexed) {
            child->addRepaintFull();
        }
    }
    add_repaint_outputs(region);
    Q_EMIT needsRepaint();
}

bool Toplevel::has_pending_repaints() const
{
    return !repaints().isEmpty();
}

QRegion Toplevel::repaints() const
{
    return repaints_region.translated(pos()) | layer_repaints_region;
}

void Toplevel::resetRepaints(base::output* output)
{
    auto reset_all = [this] {
        repaints_region = QRegion();
        layer_repaints_region = QRegion();
    };

    if (!output) {
        assert(!repaint_outputs.size());
        reset_all();
        return;
    }

    remove_all(repaint_outputs, output);

    if (!repaint_outputs.size()) {
        reset_all();
        return;
    }

    auto reset_region = QRegion(output->geometry());

    for (auto out : repaint_outputs) {
        reset_region = reset_region.subtracted(out->geometry());
    }

    repaints_region.translate(pos());
    repaints_region = repaints_region.subtracted(reset_region);
    repaints_region.translate(-pos());

    layer_repaints_region = layer_repaints_region.subtracted(reset_region);
}

void Toplevel::add_repaint_outputs(QRegion const& region)
{
    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        // On X11 we do not paint per output.
        return;
    }
    for (auto& out : kwinApp()->get_base().get_outputs()) {
        if (contains(repaint_outputs, out)) {
            continue;
        }
        if (region.intersected(out->geometry()).isEmpty()) {
            continue;
        }
        repaint_outputs.push_back(out);
    }
}

void Toplevel::setReadyForPainting()
{
    if (!ready_for_painting) {
        ready_for_painting = true;
        if (space.compositing()) {
            addRepaintFull();
            Q_EMIT windowShown(this);
        }
    }
}

void Toplevel::checkScreen()
{
    auto const& outputs = kwinApp()->get_base().get_outputs();
    auto output = base::get_nearest_output(outputs, frameGeometry().center());
    if (central_output != output) {
        auto old_out = central_output;
        central_output = output;
        Q_EMIT central_output_changed(old_out, output);
    }
}

void Toplevel::setupCheckScreenConnection()
{
    connect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::checkScreen);
    checkScreen();
}

void Toplevel::removeCheckScreenConnection()
{
    disconnect(this, &Toplevel::frame_geometry_changed, this, &Toplevel::checkScreen);
}

void Toplevel::handle_output_added(base::output* output)
{
    if (!central_output) {
        central_output = output;
        Q_EMIT central_output_changed(nullptr, output);
        return;
    }

    checkScreen();
}

void Toplevel::handle_output_removed(base::output* output)
{
    if (central_output != output) {
        return;
    }
    auto const& outputs = kwinApp()->get_base().get_outputs();
    central_output = base::get_nearest_output(outputs, frameGeometry().center());
    Q_EMIT central_output_changed(output, central_output);
}

qreal Toplevel::bufferScale() const
{
    return remnant ? remnant->buffer_scale : 1.;
}

bool Toplevel::wantsShadowToBeRendered() const
{
    return true;
}

bool Toplevel::is_wayland_window() const
{
    return false;
}

bool Toplevel::isClient() const
{
    return false;
}

pid_t Toplevel::pid() const
{
    return info->pid();
}

xcb_window_t Toplevel::frameId() const
{
    if (remnant) {
        return remnant->frame;
    }
    return xcb_window;
}

void Toplevel::debug(QDebug& stream) const
{
    if (remnant) {
        stream << "\'REMNANT:" << reinterpret_cast<void const*>(this) << "\'";
    } else {
        stream << "\'ID:" << reinterpret_cast<void const*>(this) << xcb_window << "\'";
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
    Q_EMIT skipCloseAnimationChanged();
}

// TODO(romangg): * This function is only called on Wayland and the damage translation is not the
//                  usual way. Unify that.
//                * Should we return early on the added damage being empty?
void Toplevel::addDamage(const QRegion &damage)
{
    auto const render_region = win::render_geometry(this);
    repaints_region += damage.translated(render_region.topLeft() - pos());
    add_repaint_outputs(render_region);

    m_isDamaged = true;
    damage_region += damage;
    Q_EMIT damaged(this, damage);
}

QByteArray Toplevel::windowRole() const
{
    if (remnant) {
        return remnant->window_role;
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
        Q_EMIT hasAlphaChanged();
    }
}

QMatrix4x4 Toplevel::input_transform() const
{
    QMatrix4x4 transform;

    auto const render_pos = win::frame_to_render_pos(this, pos());
    transform.translate(-render_pos.x(), -render_pos.y());

    return transform;
}

void Toplevel::set_frame_geometry(QRect const& rect)
{
    m_frameGeometry = rect;
}

void Toplevel::discard_shape()
{
    m_render_shape_valid = false;
    discard_quads();
}

void Toplevel::discard_quads()
{
    if (render) {
        render->invalidateQuadsCache();
        addRepaintFull();
    }
    if (transient()->annexed) {
        for (auto lead : transient()->leads()) {
            lead->discard_quads();
        }
    }
}

QRegion Toplevel::render_region() const
{
    if (remnant) {
        return remnant->render_region;
    }

    auto const render_geo = win::render_geometry(this);

    if (is_shape) {
        if (m_render_shape_valid) {
            return m_render_shape;
        }
        m_render_shape_valid = true;
        m_render_shape = QRegion();

        auto cookie
            = xcb_shape_get_rectangles_unchecked(connection(), frameId(), XCB_SHAPE_SK_BOUNDING);
        unique_cptr<xcb_shape_get_rectangles_reply_t> reply(
            xcb_shape_get_rectangles_reply(connection(), cookie, nullptr));
        if (!reply) {
            return QRegion();
        }

        auto const rects = xcb_shape_get_rectangles_rectangles(reply.get());
        auto const rect_count = xcb_shape_get_rectangles_rectangles_length(reply.get());
        for (int i = 0; i < rect_count; ++i) {
            m_render_shape += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        }

        // make sure the shape is sane (X is async, maybe even XShape is broken)
        m_render_shape &= QRegion(0, 0, render_geo.width(), render_geo.height());
        return m_render_shape;
    }

    return QRegion(0, 0, render_geo.width(), render_geo.height());
}

bool Toplevel::isLocalhost() const
{
    if (!client_machine) {
        return true;
    }
    return client_machine->is_local();
}

bool Toplevel::is_popup_end() const
{
    if (remnant) {
        return remnant->was_popup_window;
    }
    return false;
}

int Toplevel::desktop() const
{
    // TODO: for remnant special case?
    return m_desktops.isEmpty() ? static_cast<int>(NET::OnAllDesktops) : m_desktops.last()->x11DesktopNumber();
}

QVector<win::virtual_desktop*> Toplevel::desktops() const
{
    return m_desktops;
}

void Toplevel::set_desktops(QVector<win::virtual_desktop*> const& desktops)
{
    m_desktops = desktops;
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

win::layer Toplevel::layer() const
{
    if (transient()->lead() && transient()->annexed) {
        return transient()->lead()->layer();
    }
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
    assert(control);

    // Slight hack for the 'allow window to cover panel' Kicker setting.
    // Don't move keepbelow docks below normal window, but only to the same
    // layer, so that both may be raised to cover the other.
    if (control->keep_below()) {
        return win::layer::normal;
    }
    if (control->keep_above()) {
        // slight hack for the autohiding panels
        return win::layer::above;
    }
    return win::layer::dock;
}

bool Toplevel::isInternal() const
{
    return false;
}

win::transient* Toplevel::transient() const
{
    return m_transient.get();
}

win::maximize_mode Toplevel::maximizeMode() const
{
    return win::maximize_mode::restore;
}

bool Toplevel::wantsInput() const
{
    return false;
}

bool Toplevel::dockWantsInput() const
{
    return false;
}

void Toplevel::checkNoBorder()
{
    setNoBorder(false);
}

xcb_timestamp_t Toplevel::userTime() const
{
    return XCB_TIME_CURRENT_TIME;
}

QSize Toplevel::maxSize() const
{
    return control->rules().checkMaxSize(QSize(INT_MAX, INT_MAX));
}

QSize Toplevel::minSize() const
{
    return control->rules().checkMinSize(QSize(0, 0));
}

void Toplevel::layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const
{
    if (remnant) {
        return remnant->layout_decoration_rects(left, top, right, bottom);
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

win::x11::group const* Toplevel::group() const
{
    return nullptr;
}

win::x11::group* Toplevel::group()
{
    return nullptr;
}

bool Toplevel::supportsWindowRules() const
{
    return control != nullptr;
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
    space.setMoveResizeClient(nullptr);
    control->move_resize().enabled = false;
    if (space.edges->desktop_switching.when_moving_client) {
        space.edges->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    }
    if (control->electric_maximizing()) {
        space.outline->hide();
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

void Toplevel::update_maximized([[maybe_unused]] win::maximize_mode mode)
{
}

bool Toplevel::performMouseCommand(base::options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(*this, cmd, globalPos);
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
    return space.get_icon_geometry(this);
}

void Toplevel::setShortcutInternal()
{
    updateCaption();
    space.clientShortcutUpdated(this);
}

}

