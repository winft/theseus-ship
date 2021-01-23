/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "subsurface.h"
#include "xdg_shell.h"

#include "win/deco.h"
#include "win/remnant.h"
#include "win/transient.h"

#include "decorations/window.h"
#include "rules/rules.h"
#include "wayland_server.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/xdg_decoration.h>

#include <KDecoration2/DecoratedClient>

#include <csignal>
#include <sys/types.h>
#include <unistd.h>

namespace KWin::win::wayland
{

namespace WS = Wrapland::Server;

Toplevel* find_toplevel(WS::Surface* surface)
{
    return Workspace::self()->findToplevel(
        [surface](auto win) { return win->surface() == surface; });
}

window::window(WS::Surface* surface)
    : Toplevel()
{
    setSurface(surface);

    connect(surface, &WS::Surface::unmapped, this, &window::unmap);
    connect(surface, &WS::Surface::subsurfaceTreeChanged, this, [this] {
        discard_shape();
        win::wayland::restack_subsurfaces(this);
    });
    setupCompositing(false);
}

NET::WindowType window::windowType([[maybe_unused]] bool direct,
                                   [[maybe_unused]] int supported_types) const
{
    return window_type;
}

QByteArray window::windowRole() const
{
    return QByteArray();
}

pid_t window::pid() const
{
    return surface()->client()->processId();
}

bool window::isLocalhost() const
{
    return true;
}

bool window::isLockScreen() const
{
    return surface()->client() == waylandServer()->screenLockerClientConnection();
}

bool window::isInputMethod() const
{
    return surface()->client() == waylandServer()->inputMethodConnection();
}

void window::updateCaption()
{
    auto const old_suffix = caption.suffix;
    auto const shortcut = shortcut_caption_suffix(this);
    caption.suffix = shortcut;
    if ((!is_special_window(this) || is_toolbar(this))
        && find_client_with_same_caption(static_cast<Toplevel*>(this))) {
        int i = 2;
        do {
            caption.suffix = shortcut + QLatin1String(" <") + QString::number(i) + QLatin1Char('>');
            i++;
        } while (find_client_with_same_caption(static_cast<Toplevel*>(this)));
    }
    if (caption.suffix != old_suffix) {
        Q_EMIT captionChanged();
    }
}

bool window::belongsToSameApplication(Toplevel const* other, win::same_client_check checks) const
{
    if (win::flags(checks & win::same_client_check::allow_cross_process)) {
        if (other->control->desktop_file_name() == control->desktop_file_name()) {
            return true;
        }
    }
    if (auto s = other->surface()) {
        return s->client() == surface()->client();
    }
    return false;
}

bool window::noBorder() const
{
    if (xdg_deco && xdg_deco->requestedMode() != WS::XdgDecoration::Mode::ClientSide) {
        return user_no_border || control->fullscreen();
    }
    return true;
}

void window::setNoBorder(bool set)
{
    if (!userCanSetNoBorder()) {
        return;
    }

    set = control->rules().checkNoBorder(set);
    if (user_no_border == set) {
        return;
    }

    user_no_border = set;
    updateDecoration(true, false);
    updateWindowRules(Rules::NoBorder);
}

void window::closeWindow()
{
    assert(isCloseable());

    if (isCloseable()) {
        toplevel->close();
        ping(ping_reason::close);
    }
}

QSize window::sizeForClientSize(QSize const& wsize,
                                [[maybe_unused]] win::size_mode mode,
                                bool noframe) const
{
    auto size = wsize
        - QSize(client_frame_extents.left() + client_frame_extents.right(),
                client_frame_extents.top() + client_frame_extents.bottom());
    size = size.expandedTo(minSize()).boundedTo(maxSize());

    if (!noframe) {
        size += QSize(win::left_border(this) + win::right_border(this),
                      win::top_border(this) + win::bottom_border(this));
    }
    return size;
}

QSize window::minSize() const
{
    return control->rules().checkMinSize(toplevel->minimumSize());
}

QSize window::maxSize() const
{
    return control->rules().checkMinSize(toplevel->maximumSize());
}

bool window::isCloseable() const
{
    return toplevel && window_type != NET::Desktop && window_type != NET::Dock;
}

bool window::isMaximizable() const
{
    if (!isResizable()) {
        return false;
    }

    return control->rules().checkMaximize(maximize_mode::restore) == maximize_mode::restore
        && control->rules().checkMaximize(maximize_mode::full) == maximize_mode::full;
}

bool window::isMinimizable() const
{
    if (!control) {
        return false;
    }
    if (!control->rules().checkMinimize(true)) {
        return false;
    }
    return (!plasma_shell_surface
            || plasma_shell_surface->role() == WS::PlasmaShellSurface::Role::Normal);
}

bool window::isMovable() const
{
    if (!control) {
        return false;
    }
    if (control->fullscreen()) {
        return false;
    }
    if (control->rules().checkPosition(invalidPoint) != invalidPoint) {
        return false;
    }
    if (plasma_shell_surface) {
        return plasma_shell_surface->role() == WS::PlasmaShellSurface::Role::Normal;
    }
    return true;
}

bool window::isMovableAcrossScreens() const
{
    if (!control) {
        return false;
    }
    if (control->rules().checkPosition(invalidPoint) != invalidPoint) {
        return false;
    }
    if (plasma_shell_surface) {
        return plasma_shell_surface->role() == WS::PlasmaShellSurface::Role::Normal;
    }
    return true;
}

bool window::isResizable() const
{
    if (!control) {
        return false;
    }
    if (control->fullscreen()) {
        return false;
    }
    if (control->rules().checkSize(QSize()).isValid()) {
        return false;
    }
    if (plasma_shell_surface) {
        return plasma_shell_surface->role() == WS::PlasmaShellSurface::Role::Normal;
    }

    assert(toplevel);
    auto const min = minSize();
    auto const max = maxSize();

    return min.width() < max.width() || min.height() < max.height();
}

void window::takeFocus()
{
    assert(control);

    if (control->rules().checkAcceptFocus(wantsInput())) {
        if (toplevel) {
            ping(ping_reason::focus);
        }
        set_active(this, true);
    }

    if (!control->keep_above() && !is_on_screen_display(this) && !belongsToDesktop()) {
        workspace()->setShowingDesktop(false);
    }
}

void window::doSetActive()
{
    assert(control);

    if (!control->active()) {
        return;
    }
    StackingUpdatesBlocker blocker(workspace());
    workspace()->focusToNull();
}

bool window::userCanSetFullScreen() const
{
    return control.get();
}

bool window::userCanSetNoBorder() const
{
    if (!xdg_deco || xdg_deco->requestedMode() == WS::XdgDecoration::Mode::ClientSide) {
        return false;
    }
    return !control->fullscreen() && !shaded(this);
}

bool window::wantsInput() const
{
    assert(control);
    return control->rules().checkAcceptFocus(acceptsFocus());
}

bool window::acceptsFocus() const
{
    assert(control);

    if (waylandServer()->inputMethodConnection() == surface()->client()) {
        return false;
    }

    using PSS = WS::PlasmaShellSurface;

    if (plasma_shell_surface) {
        if (plasma_shell_surface->role() == PSS::Role::OnScreenDisplay
            || plasma_shell_surface->role() == PSS::Role::ToolTip) {
            return false;
        }

        if (plasma_shell_surface->role() == PSS::Role::Notification
            || plasma_shell_surface->role() == PSS::Role::CriticalNotification) {
            return plasma_shell_surface->panelTakesFocus();
        }
    }
    if (!mapped || closing) {
        return false;
    }
    return true;
}

double window::opacity() const
{
    if (transient()->lead() && transient()->annexed) {
        return transient()->lead()->opacity();
    }
    return m_opacity;
}

void window::setOpacity(double opacity)
{
    assert(control);

    opacity = qBound(0.0, opacity, 1.0);
    if (opacity == m_opacity) {
        return;
    }

    auto const old_opacity = m_opacity;
    m_opacity = opacity;

    addRepaintFull();
    Q_EMIT opacityChanged(this, old_opacity);
}

bool window::isShown([[maybe_unused]] bool shaded_is_shown) const
{
    if (!control && !transient()->lead()) {
        return false;
    }

    if (auto lead = transient()->lead()) {
        if (!lead->isShown(false)) {
            return false;
        }
    }
    if (control && control->minimized()) {
        return false;
    }
    return !closing && !hidden && surface()->buffer().get();
}

bool window::isHiddenInternal() const
{
    if (auto lead = transient()->lead()) {
        if (!lead->isHiddenInternal()) {
            return false;
        }
    }
    return hidden || !surface()->buffer();
}

void window::hideClient(bool hide)
{
    if (hidden == hide) {
        return;
    }

    hidden = hide;

    if (hide) {
        addWorkspaceRepaint(visible_rect(this));
        workspace()->clientHidden(this);
        Q_EMIT windowHidden(this);
    } else {
        Q_EMIT windowShown(this);
    }
}

QRect window::bufferGeometry() const
{
    return QRect(frame_to_client_pos(this, pos()) + surface()->offset(), surface()->size());
}

maximize_mode window::maximizeMode() const
{
    return max_mode;
}

static bool changeMaximize_recursion{false};

void window::changeMaximize(bool horizontal, bool vertical, bool adjust)
{
    assert(control);

    if (changeMaximize_recursion) {
        return;
    }

    if (!isResizable()) {
        return;
    }

    auto const clientArea = control->electric_maximizing()
        ? workspace()->clientArea(MaximizeArea, Cursor::pos(), desktop())
        : workspace()->clientArea(MaximizeArea, this);

    auto const old_mode = configured_max_mode;
    auto const old_geom = frameGeometry();

    // 'adjust == true' means to update the size only, e.g. after changing workspace size
    if (!adjust) {
        if (vertical) {
            configured_max_mode = configured_max_mode ^ maximize_mode::vertical;
        }
        if (horizontal) {
            configured_max_mode = configured_max_mode ^ maximize_mode::horizontal;
        }
    }

    configured_max_mode = control->rules().checkMaximize(configured_max_mode);

    if (!adjust && configured_max_mode == old_mode) {
        return;
    }

    StackingUpdatesBlocker stack_blocker(workspace());
    xdg_shell_configure_blocker conf_blocker(this);
    dont_move_resize(this);

    // call into decoration update borders
    if (win::decoration(this) && control->deco().client
        && !(options->borderlessMaximizedWindows() && configured_max_mode == maximize_mode::full)) {
        changeMaximize_recursion = true;
        auto const deco_client = win::decoration(this)->client().toStrongRef();
        if ((configured_max_mode & maximize_mode::vertical)
            != (old_mode & maximize_mode::vertical)) {
            Q_EMIT deco_client->maximizedVerticallyChanged(
                flags(configured_max_mode & maximize_mode::vertical));
        }
        if ((configured_max_mode & maximize_mode::horizontal)
            != (old_mode & maximize_mode::horizontal)) {
            Q_EMIT deco_client->maximizedHorizontallyChanged(
                flags(configured_max_mode & maximize_mode::horizontal));
        }
        if ((configured_max_mode == maximize_mode::full) != (old_mode == maximize_mode::full)) {
            Q_EMIT deco_client->maximizedChanged(flags(configured_max_mode & maximize_mode::full));
        }
        changeMaximize_recursion = false;
    }

    if (options->borderlessMaximizedWindows()) {
        // triggers a maximize change.
        // The next setNoBorder interation will exit since there's no change but the first recursion
        // pullutes the restore geometry
        changeMaximize_recursion = true;
        setNoBorder(control->rules().checkNoBorder(configured_max_mode == maximize_mode::full));
        changeMaximize_recursion = false;
    }

    // Conditional quick tiling exit points
    auto const old_quicktiling = control->quicktiling();
    if (control->quicktiling() != quicktiles::none) {
        if (old_mode == maximize_mode::full
            && !clientArea.contains(restore_geometries.maximize.center())) {
            // Not restoring on the same screen
            // TODO: The following doesn't work for some reason
            // quick_tile_mode = QuickTileNone; // And exit quick tile mode manually
        } else if ((old_mode == maximize_mode::vertical
                    && configured_max_mode == maximize_mode::restore)
                   || (old_mode == maximize_mode::full
                       && configured_max_mode == maximize_mode::horizontal)) {
            // Modifying geometry of a tiled window.
            // Exit quick tile mode without restoring geometry.
            control->set_quicktiling(quicktiles::none);
        }
    }

    if (configured_max_mode == maximize_mode::full) {
        restore_geometries.maximize = old_geom;

        // TODO: Client has more checks
        if (options->electricBorderMaximize()) {
            control->set_quicktiling(quicktiles::maximize);
        } else {
            control->set_quicktiling(quicktiles::none);
        }
        if (control->quicktiling() != old_quicktiling) {
            Q_EMIT quicktiling_changed();
        }
        setFrameGeometry(workspace()->clientArea(MaximizeArea, this));
        workspace()->raise_window(this);
    } else {
        if (configured_max_mode == maximize_mode::restore) {
            control->set_quicktiling(quicktiles::none);
        }
        if (control->quicktiling() != old_quicktiling) {
            Q_EMIT quicktiling_changed();
        }

        setFrameGeometry(restore_geometries.maximize);
    }
}

void window::setFrameGeometry(QRect const& rect, force_geometry force)
{
    assert(control);

    // In global coordinates.
    auto const frame_geo = control->rules().checkGeometry(rect);

    configured_frame_geometry = frame_geo;

    if (control->geometry_update.block) {
        // When goemetry updates are blocked the current geometry is passed to setFrameGeometry
        // thus we need to set it here.
        set_frame_geometry(frame_geo);

        if (control->geometry_update.pending != pending_geometry::forced) {
            if (force == force_geometry::yes) {
                control->geometry_update.pending = pending_geometry::forced;
            } else {
                control->geometry_update.pending = pending_geometry::normal;
            }
            return;
        }
    }

    if (control->geometry_update.pending != win::pending_geometry::none) {
        // To compare the pending geometry with the last set one, reset it to the one before
        // blocking.
        set_frame_geometry(control->geometry_update.original.frame);
    }

    // In surface-local coordinates.
    auto const ref_geo = QRect(QPoint(), (frame_geo - frame_margins(this)).size());

    // The reference geometry might be empty if a positional window rule was applied before the
    // first client commit. In this case we can omit asking the client for a new size.
    if (!ref_geo.size().isEmpty() && ref_geo.size() != shell_surface->window_geometry().size()) {
        // Size did change, client needs to provide a new buffer before we do set the geometry.
        if (!pending_configures.size()
            || (pending_configures.back().frame_geometry - frame_margins(this)).size()
                != ref_geo.size()) {
            configure_geometry(frame_geo);
        }
        return;
    }

    do_set_geometry(frame_geo);
    update_maximize_mode(configured_max_mode);
}

/**
 * Ask client to provide buffer adapted to the new geometry @param rect (in global coordinates).
 */
void window::configure_geometry(QRect const& frame_geo)
{
    configured_frame_geometry = frame_geo;

    // The window geometry relevant to clients is the frame geometry without decorations.
    auto window_geo = frame_geo;

    if (window_geo.isValid()) {
        window_geo = window_geo - frame_margins(this);
    }

    if (pending_configures.size()
        && (pending_configures.back().frame_geometry - frame_margins(this)).size()
            == window_geo.size()) {
        // The latest pending configure event already asks for this geometry.
        return;
    }

    uint64_t serial = 0;

    if (toplevel) {
        serial = toplevel->configure(xdg_surface_states(this), window_geo.size());
    }
    if (popup) {
        auto parent = transient()->lead();
        if (parent) {
            auto const top_lead = lead_of_annexed_transient(this);
            auto const bounds = Workspace::self()->clientArea(
                top_lead->control->fullscreen() ? FullScreenArea : PlacementArea, top_lead);

            serial = popup->configure(popup_placement(this, bounds).translated(-top_lead->pos()));
        }
    }

    if (!window_geo.isValid()) {
        // Without valid geometry there's implicity no positional information worth using.
        return;
    }

    configure_event ce;
    ce.serial = serial;
    ce.frame_geometry = frame_geo;
    ce.max_mode = configured_max_mode;
    pending_configures.push_back(ce);
}

void window::apply_pending_geometry()
{
    if (!toplevel && !popup) {
        return;
    }

    auto position = pos();
    auto max_mode = this->max_mode;

    for (auto it = pending_configures.begin(); it != pending_configures.end(); it++) {
        if (it->serial > acked_configure) {
            // Serial not acked yet that means all future serials are not.
            break;
        }
        if (it->serial == acked_configure) {
            if (frameGeometry().size() != it->frame_geometry.size()) {
                addLayerRepaint(it->frame_geometry);
                position = it->frame_geometry.topLeft();
                max_mode = it->max_mode;
            }

            // Removes all previous pending configures including this one.
            pending_configures.erase(pending_configures.begin(), ++it);
            break;
        }
    }

    auto const ref_geo = shell_surface->window_geometry();
    auto frame_geo = QRect(position, ref_geo.size() + frame_size(this));

    client_frame_extents = shell_surface->window_margins();

    if (popup) {
        auto const toplevel = lead_of_annexed_transient(this);
        auto const screen_bounds = Workspace::self()->clientArea(
            toplevel->control->fullscreen() ? FullScreenArea : PlacementArea, toplevel);

        if (!plasma_shell_surface) {
            // Popups with Plasma shell surfaces have their frame geometry set explicitly.
            auto const old_frame_geo = frameGeometry();
            set_frame_geometry(popup_placement(this, screen_bounds));
            Q_EMIT geometryShapeChanged(this, old_frame_geo);
        }
        return;
    }

    if (is_resize(this)) {
        // Adjust the geometry according to the resize process.
        // We must adjust frame geometry because configure events carry the maximum window geometry
        // size. A client with aspect ratio can attach a buffer with smaller size than the one in
        // a configure event.
        auto& mov_res = control->move_resize();

        switch (mov_res.contact) {
        case win::position::top_left:
            frame_geo.moveRight(mov_res.geometry.right());
            frame_geo.moveBottom(mov_res.geometry.bottom());
            break;
        case win::position::top:
        case win::position::top_right:
            frame_geo.moveLeft(mov_res.geometry.left());
            frame_geo.moveBottom(mov_res.geometry.bottom());
            break;
        case win::position::right:
        case win::position::bottom_right:
        case win::position::bottom:
            frame_geo.moveLeft(mov_res.geometry.left());
            frame_geo.moveTop(mov_res.geometry.top());
            break;
        case win::position::bottom_left:
        case win::position::left:
            frame_geo.moveRight(mov_res.geometry.right());
            frame_geo.moveTop(mov_res.geometry.top());
            break;
        case win::position::center:
            Q_UNREACHABLE();
        }
    }

    do_set_geometry(frame_geo);
    update_maximize_mode(max_mode);
}

void window::do_set_geometry(QRect const& frame_geo)
{
    assert(control);

    if (frameGeometry() == frame_geo) {
        return;
    }

    if (!control) {
        // Special case for popups with plasma shell surface integration.
        assert(plasma_shell_surface);
        auto const old_frame_geo = frameGeometry();
        set_frame_geometry(frame_geo);
        Q_EMIT geometryShapeChanged(this, old_frame_geo);
        return;
    }

    set_frame_geometry(frame_geo);

    if (!restore_geometries.maximize.isValid()) {
        // Happens on first commit with valid size.
        // TODO(romangg): The restore geometry is used in check_workspace_position for some reason.
        //                Can we use normal frame geometry there and make this here unnecessary?
        restore_geometries.maximize = frame_geo;
    }

    updateWindowRules(static_cast<Rules::Types>(Rules::Position | Rules::Size));

    auto const old_frame_geo = control->geometry_update.original.frame;
    add_repaint_during_geometry_updates(this);
    control->update_geometry_before_update_blocking();

    Q_EMIT geometryShapeChanged(this, old_frame_geo);

    // TODO(romangg): Is this necessary here?
    if (hasStrut()) {
        workspace()->updateClientArea();
    }

    if (is_resize(this)) {
        perform_move_resize(this);
    }
}

void window::update_maximize_mode(maximize_mode mode)
{
    if (mode == max_mode) {
        return;
    }

    max_mode = mode;
    updateWindowRules(static_cast<Rules::Types>(Rules::MaximizeHoriz | Rules::MaximizeVert
                                                | Rules::Position | Rules::Size));

    Q_EMIT clientMaximizedStateChanged(this, mode);
    Q_EMIT clientMaximizedStateChanged(this,
                                       flags(mode & win::maximize_mode::horizontal),
                                       flags(mode & win::maximize_mode::vertical));
}

bool window::belongsToDesktop() const
{
    auto const windows = waylandServer()->windows;

    return std::any_of(windows.cbegin(), windows.cend(), [this](auto const& win) {
        if (belongsToSameApplication(win, flags<same_client_check>())) {
            return is_desktop(win);
        }
        return false;
    });
}

layer window::layer_for_dock() const
{
    assert(control);

    if (!plasma_shell_surface) {
        return Toplevel::layer_for_dock();
    }

    using PSS = WS::PlasmaShellSurface;

    switch (plasma_shell_surface->panelBehavior()) {
    case PSS::PanelBehavior::WindowsCanCover:
        return layer::normal;
    case PSS::PanelBehavior::AutoHide:
        return layer::above;
    case PSS::PanelBehavior::WindowsGoBelow:
    case PSS::PanelBehavior::AlwaysVisible:
        return layer::dock;
    default:
        Q_UNREACHABLE();
        break;
    }

    return layer::unknown;
}

bool window::has_pending_repaints() const
{
    return readyForPainting() && Toplevel::has_pending_repaints();
}

void window::map()
{
    if (mapped || !isShown(false)) {
        return;
    }

    mapped = true;

    if (transient()->annexed) {
        discard_quads();
    }

    if (control) {
        if (!isLockScreen()) {
            setup_wayland_plasma_management(this);
        }
        update_screen_edge(this);
    }

    if (!ready_for_painting) {
        setReadyForPainting();
    } else {
        addRepaintFull();
        Q_EMIT windowShown(this);
    }
}

void window::unmap()
{
    assert(!isShown(false));

    if (!mapped) {
        return;
    }

    mapped = false;

    if (transient()->annexed) {
        discard_quads();
    }

    if (control) {
        if (control->move_resize().enabled) {
            leaveMoveResize();
        }
        control->destroy_wayland_management();
    }

    if (Workspace::self()) {
        addWorkspaceRepaint(visible_rect(this));
        if (control) {
            workspace()->clientHidden(this);
        }
    }

    Q_EMIT windowHidden(this);
}

// At the moment only called on xdg-shell surfaces.
void window::handle_commit()
{
    if (!surface()->buffer()) {
        unmap();
        return;
    }

    apply_pending_geometry();

    setDepth((surface()->buffer()->hasAlphaChannel() && !is_desktop(this)) ? 32 : 24);
    map();
}

bool window::isTransient() const
{
    return transient()->lead() != nullptr;
}

bool window::isInitialPositionSet() const
{
    return plasma_shell_surface && plasma_shell_surface->isPositionSet();
}

// Only transient-relations via subsurfaces are checked this way since
// xdg-shell toplevels and popups are required to be mapped after their parents.
void window::checkTransient(Toplevel* window)
{
    if (transient()->lead()) {
        // This already has a parent set, we can only set one once.
        return;
    }
    if (!surface()->subsurface()) {
        // This is not a subsurface.
        return;
    }
    if (surface()->subsurface()->parentSurface() != window->surface()) {
        // This has a parent different to window.
        return;
    }

    // The window is a new parent of this.
    set_subsurface_parent(this, window);

    map();
}

void window::updateDecoration(bool check_workspace_pos, bool force)
{
    if ((!win::decoration(this) && noBorder()) || (win::decoration(this) && !noBorder())) {
        if (!force) {
            return;
        }
    }

    auto const old_geom = frameGeometry();
    auto const old_content_geom = old_geom.adjusted(
        left_border(this), top_border(this), -right_border(this), -bottom_border(this));

    block_geometry_updates(this, true);

    if (force) {
        control->destroy_decoration();
    }

    if (noBorder()) {
        control->destroy_decoration();
    } else {
        // Create decoration.
        control->deco().window = new Decoration::window(this);
        auto decoration
            = Decoration::DecorationBridge::self()->createDecoration(control->deco().window);
        if (decoration) {
            QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);
            connect(decoration, &KDecoration2::Decoration::shadowChanged, this, [this] {
                update_shadow(this);
            });
            connect(decoration, &KDecoration2::Decoration::bordersChanged, this, [this]() {
                geometry_updates_blocker geo_blocker(this);
                xdg_shell_configure_blocker conf_blocker(this);
                auto const old_geom = frameGeometry();
                if (!shaded(this)) {
                    check_workspace_position(this, old_geom);
                }
                Q_EMIT geometryShapeChanged(this, old_geom);
            });
        }

        control->deco().decoration = decoration;
        auto const deco_size
            = QSize(left_border(this) + right_border(this), bottom_border(this) + top_border(this));

        // TODO: ensure the new geometry still fits into the client area (e.g. maximized windows)
        do_set_geometry(QRect(old_geom.topLeft(), size() + deco_size));
        Q_EMIT geometryShapeChanged(this, old_geom);
    }

    if (xdg_deco) {
        auto const mode = (win::decoration(this) || user_no_border)
            ? WS::XdgDecoration::Mode::ServerSide
            : WS::XdgDecoration::Mode::ClientSide;
        xdg_deco->configure(mode);
    }

    update_shadow(this);

    if (check_workspace_pos) {
        check_workspace_position(this, old_geom, -2, old_content_geom);
    }

    block_geometry_updates(this, false);
}

void window::updateColorScheme()
{
    assert(control);

    if (palette) {
        set_color_scheme(this, control->rules().checkDecoColor(palette->palette()));
    } else {
        set_color_scheme(this, control->rules().checkDecoColor(QString()));
    }
}

bool window::hasStrut() const
{
    using PSS = WS::PlasmaShellSurface;

    if (!isShown(true)) {
        return false;
    }
    if (!plasma_shell_surface) {
        return false;
    }
    return plasma_shell_surface->role() == PSS::Role::Panel
        && plasma_shell_surface->panelBehavior() == PSS::PanelBehavior::AlwaysVisible;
}

quint32 window::windowId() const
{
    return id;
}

void window::resizeWithChecks(QSize const& size, force_geometry force)
{
    auto const area = workspace()->clientArea(WorkArea, this);
    auto width = size.width();
    auto height = size.height();

    // don't allow growing larger than workarea
    if (width > area.width()) {
        width = area.width();
    }
    if (height > area.height()) {
        height = area.height();
    }

    setFrameGeometry(QRect(pos(), QSize(width, height)), force);
}

void window::setFullScreen(bool full, bool user)
{
    full = control->rules().checkFullScreen(full);

    auto const was_fullscreen = control->fullscreen();
    if (was_fullscreen == full) {
        return;
    }

    if (is_special_window(this)) {
        return;
    }
    if (user && !userCanSetFullScreen()) {
        return;
    }

    if (was_fullscreen) {
        // May cause focus leave.
        // TODO: Must always be done when fullscreening to other output allowed.
        workspace()->updateFocusMousePosition(Cursor::pos());
    } else {
        restore_geometries.fullscreen = frameGeometry();
    }
    control->set_fullscreen(full);

    if (full) {
        workspace()->raise_window(this);
    }

    xdg_shell_configure_blocker configure_blocker(this);
    StackingUpdatesBlocker stack_blocker(workspace());
    win::geometry_updates_blocker geometry_blocker(this);
    dont_move_resize(this);

    // Active fullscreens gets a different layer.
    workspace()->updateClientLayer(this);
    updateDecoration(false, false);

    if (full) {
        setFrameGeometry(workspace()->clientArea(FullScreenArea, this));
    } else {
        if (restore_geometries.fullscreen.isValid()) {
            auto const current_screen = screen();
            setFrameGeometry(
                QRect(restore_geometries.fullscreen.topLeft(),
                      adjusted_size(this, restore_geometries.fullscreen.size(), size_mode::any)));
            if (current_screen != screen()) {
                workspace()->sendClientToScreen(this, current_screen);
            }
        } else {
            // May happen when fullscreen from the start, let the client set the size.
            setFrameGeometry(
                QRect(workspace()->clientArea(PlacementArea, this).topLeft(), QSize(0, 0)));
        }
    }

    updateWindowRules(static_cast<Rules::Types>(Rules::Fullscreen | Rules::Position | Rules::Size));
    Q_EMIT fullScreenChanged();
}

void window::handle_class_changed()
{
    auto const window_class = QByteArray(toplevel->appId().c_str());
    setResourceClass(resourceName(), window_class);
    if (initialized && supportsWindowRules()) {
        setup_rules(this, true);
        applyWindowRules();
    }
    set_desktop_file_name(this, window_class);
}

void window::handle_title_changed()
{
    auto const old_suffix = caption.suffix;

    caption.normal = QString::fromStdString(toplevel->title()).simplified();
    updateCaption();

    if (caption.suffix == old_suffix) {
        // Don't emit caption change twice it already got emitted by the changing suffix.
        Q_EMIT captionChanged();
    }
}

void window::destroy()
{
    closing = true;

    StackingUpdatesBlocker blocker(workspace());

    auto remnant_window = create_remnant(this);
    Q_EMIT windowClosed(this, remnant_window);

    if (control) {
#ifdef KWIN_BUILD_TABBOX
        auto tabbox = TabBox::TabBox::self();
        if (tabbox->isDisplayed() && tabbox->currentClient() == this) {
            tabbox->nextPrev(true);
        }
#endif
        if (control->move_resize().enabled) {
            leaveMoveResize();
        }

        RuleBook::self()->discardUsed(this, true);

        control->destroy_wayland_management();
        control->destroy_decoration();
    }

    waylandServer()->remove_window(this);
    remnant_window->remnant()->unref();

    delete_self(this);
}

void window::delete_self(window* win)
{
    delete win;
}

void window::debug(QDebug& stream) const
{
    std::string type = "role unknown";
    if (control) {
        type = "toplevel";
    } else if (transient()->lead()) {
        type = popup ? "popup" : "subsurface";
    }

    stream.nospace();
    stream << "\'wayland::window"
           << "(" << QString::fromStdString(type) << "):" << surface() << ";" << resourceName()
           << "\'";
}

void window::ping(window::ping_reason reason)
{
    assert(toplevel);

    auto shell = waylandServer()->xdgShell();
    auto const serial = shell->ping(toplevel->client());
    pings.insert({serial, reason});
}
void window::doMinimize()
{
    if (control->minimized()) {
        workspace()->clientHidden(this);
    } else {
        Q_EMIT windowShown(this);
    }
    workspace()->updateMinimizedOfTransients(this);
}

void window::placeIn(QRect const& area)
{
    Placement::self()->place(this, area);
    restore_geometries.maximize = frameGeometry();
}

void window::showOnScreenEdge()
{
    if (!plasma_shell_surface || !mapped) {
        return;
    }

    hideClient(false);
    workspace()->raise_window(this);

    if (plasma_shell_surface->panelBehavior() == WS::PlasmaShellSurface::PanelBehavior::AutoHide) {
        plasma_shell_surface->showAutoHidingPanel();
    }
}

bool window::dockWantsInput() const
{
    if (plasma_shell_surface
        && plasma_shell_surface->role() == WS::PlasmaShellSurface::Role::Panel) {
        return plasma_shell_surface->panelTakesFocus();
    }
    return false;
}

void window::killWindow()
{
    if (!surface()) {
        return;
    }

    auto client = surface()->client();
    if (client->processId() == getpid() || client->processId() == 0) {
        client->destroy();
        return;
    }

    ::kill(client->processId(), SIGTERM);

    // Give it time to terminate. Only if terminate fails try destroying the Wayland connection.
    QTimer::singleShot(5000, client, &WS::Client::destroy);
}

void window::cancel_popup()
{
    assert(popup);
    if (popup) {
        popup->popupDone();
    }
}

bool window::is_popup_end() const
{
    return popup != nullptr;
}

bool window::supportsWindowRules() const
{
    return toplevel && !plasma_shell_surface;
}

void window::doResizeSync()
{
    configure_geometry(control->move_resize().geometry);
}

}
