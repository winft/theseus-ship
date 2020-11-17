/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

#include "subsurface.h"
#include "win/remnant.h"
#include "win/transient.h"

#include "wayland_server.h"

#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/surface.h>

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
    return NET::Unknown;
}

QByteArray window::windowRole() const
{
    return QByteArray();
}

double window::opacity() const
{
    if (transient()->lead() && transient()->annexed) {
        return transient()->lead()->opacity();
    }
    return 1;
}

bool window::isShown([[maybe_unused]] bool shaded_is_shown) const
{
    if (!control() && !transient()->lead()) {
        return false;
    }

    if (auto lead = transient()->lead()) {
        if (!lead->isShown(false)) {
            return false;
        }
    }
    if (control() && control()->minimized()) {
        return false;
    }
    return surface()->buffer().get();
}

bool window::isHiddenInternal() const
{
    if (auto lead = transient()->lead()) {
        if (!lead->isHiddenInternal()) {
            return false;
        }
    }
    return !surface()->buffer();
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

    if (Workspace::self()) {
        addWorkspaceRepaint(win::visible_rect(this));
    }

    Q_EMIT windowHidden(this);
}

void window::handle_commit()
{
    if (!surface()->buffer()) {
        unmap();
        return;
    }
    setDepth(surface()->buffer()->hasAlphaChannel() ? 32 : 24);
    map();
}

bool window::isTransient() const
{
    return transient()->lead() != nullptr;
}

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
    win::wayland::set_subsurface_parent(this, window);

    map();
}

void window::destroy()
{
    StackingUpdatesBlocker blocker(workspace());

    auto remnant_window = create_remnant(this);
    Q_EMIT windowClosed(this, remnant_window);

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
    stream.nospace();
    stream << "\'wayland::window:" << surface() << ";WMCLASS:" << resourceClass() << ":"
           << resourceName() << "\'";
}

}
