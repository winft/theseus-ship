/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "wayland_output.h"
#include "wayland_backend.h"

#include "composite.h"
#include "wayland_server.h"

#include "render/wayland/output.h"

#include <Wrapland/Client/pointerconstraints.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <Wrapland/Server/display.h>

#include <KLocalizedString>

namespace KWin
{
namespace Wayland
{

using namespace Wrapland::Client;

WaylandOutput::WaylandOutput(Surface *surface, WaylandBackend *backend)
    : AbstractWaylandOutput(backend)
    , m_surface(surface)
    , m_backend(backend)
{
    connect(surface, &Surface::frameRendered, this, [this] {
        static_cast<WaylandCompositor*>(Compositor::self())->swapped(this);
    });
}

WaylandOutput::~WaylandOutput()
{
    delete m_surface;
}

void WaylandOutput::init(const QPoint &logicalPosition, const QSize &pixelSize)
{
    Wrapland::Server::Output::Mode mode;
    mode.id = 0;
    mode.size = pixelSize;
    mode.refresh_rate = 60000;  // TODO: can we get refresh rate data from Wayland host?
    initInterfaces("Nested-Wayland", "", "", "", pixelSize, { mode });
    forceGeometry(QRectF(logicalPosition, pixelSize));

    // TODO
#if 0
    setScale(backend()->initialOutputScale());
#endif
}

void WaylandOutput::present()
{
    auto comp = static_cast<WaylandCompositor*>(Compositor::self());
    auto render_output = comp->outputs.at(this).get();

    assert(!render_output->swap_pending);
    render_output->swap_pending = true;
}

XdgShellOutput::XdgShellOutput(Surface *surface, XdgShell *xdgShell, WaylandBackend *backend, int number)
    : WaylandOutput(surface, backend)
    , m_number(number)
{
    xdg_shell_toplevel = xdgShell->create_toplevel(surface, this);
    updateWindowTitle();

    if (auto manager = backend->xdgDecorationManager()) {
        auto decoration = manager->getToplevelDecoration(xdg_shell_toplevel, this);
        connect(decoration, &XdgDecoration::modeChanged, this,
                [decoration] {
                    if (decoration->mode() != XdgDecoration::Mode::ServerSide) {
                        decoration->setMode(XdgDecoration::Mode::ServerSide);
                    }
                });
    }

    connect(xdg_shell_toplevel, &XdgShellToplevel::configureRequested, this, &XdgShellOutput::handleConfigure);
    connect(xdg_shell_toplevel, &XdgShellToplevel::closeRequested, qApp, &QCoreApplication::quit);

    connect(backend, &WaylandBackend::pointerLockSupportedChanged, this, &XdgShellOutput::updateWindowTitle);
    connect(backend, &WaylandBackend::pointerLockChanged, this, [this](bool locked) {
        if (locked) {
            if (!m_hasPointerLock) {
                // some other output has locked the pointer
                // this surface can stop trying to lock the pointer
                lockPointer(nullptr, false);
                // set it true for the other surface
                m_hasPointerLock = true;
            }
        } else {
            // just try unlocking
            lockPointer(nullptr, false);
        }
        updateWindowTitle();
    });

    surface->commit(Surface::CommitFlag::None);
}

XdgShellOutput::~XdgShellOutput()
{
    delete xdg_shell_toplevel;
}

void XdgShellOutput::handleConfigure(const QSize &size, XdgShellToplevel::States states, quint32 serial)
{
    Q_UNUSED(states);
    if (size.width() > 0 && size.height() > 0) {
        forceGeometry(geometry());
        emit sizeChanged(size);
    }
    xdg_shell_toplevel->ackConfigure(serial);
}

void XdgShellOutput::updateWindowTitle()
{
    QString grab;
    if (m_hasPointerLock) {
        grab = i18n("Press right control to ungrab pointer");
    } else if (backend()->pointerConstraints()) {
        grab = i18n("Press right control key to grab pointer");
    }
    const QString title = i18nc("Title of nested KWin Wayland with Wayland socket identifier as argument",
                                "KDE Wayland Compositor #%1 (%2)", m_number, waylandServer()->display()->socketName().c_str());

    if (grab.isEmpty()) {
        xdg_shell_toplevel->setTitle(title);
    } else {
        xdg_shell_toplevel->setTitle(title + QStringLiteral(" — ") + grab);
    }
}

void XdgShellOutput::lockPointer(Pointer *pointer, bool lock)
{
    if (!lock) {
        const bool surfaceWasLocked = m_pointerLock && m_hasPointerLock;
        delete m_pointerLock;
        m_pointerLock = nullptr;
        m_hasPointerLock = false;
        if (surfaceWasLocked) {
            emit backend()->pointerLockChanged(false);
        }
        return;
    }

    Q_ASSERT(!m_pointerLock);
    m_pointerLock = backend()->pointerConstraints()->lockPointer(surface(), pointer, nullptr,
                                                                 PointerConstraints::LifeTime::OneShot,
                                                                 this);
    if (!m_pointerLock->isValid()) {
        delete m_pointerLock;
        m_pointerLock = nullptr;
        return;
    }
    connect(m_pointerLock, &LockedPointer::locked, this,
        [this] {
            m_hasPointerLock = true;
            emit backend()->pointerLockChanged(true);
        }
    );
    connect(m_pointerLock, &LockedPointer::unlocked, this,
        [this] {
            delete m_pointerLock;
            m_pointerLock = nullptr;
            m_hasPointerLock = false;
            emit backend()->pointerLockChanged(false);
        }
    );
}

}
}
