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
#ifndef KWIN_WAYLAND_OUTPUT_H
#define KWIN_WAYLAND_OUTPUT_H

#include "abstract_wayland_output.h"

#include <Wrapland/Client/xdg_shell.h>

#include <QObject>

namespace Wrapland
{
namespace Client
{
class Surface;

class Shell;
class ShellSurface;

class Pointer;
class LockedPointer;
}
}

namespace KWin
{
namespace Wayland
{
class WaylandBackend;

class WaylandOutput : public AbstractWaylandOutput
{
    Q_OBJECT
public:
    WaylandOutput(Wrapland::Client::Surface *surface, WaylandBackend *backend);
    ~WaylandOutput() override;

    void init(const QPoint &logicalPosition, const QSize &pixelSize);

    virtual void lockPointer(Wrapland::Client::Pointer *pointer, bool lock) {
        Q_UNUSED(pointer)
        Q_UNUSED(lock)
    }

    virtual bool pointerIsLocked() { return false; }

    Wrapland::Client::Surface* surface() const {
        return m_surface;
    }

    void present();

Q_SIGNALS:
    void sizeChanged(const QSize &size);

protected:
    WaylandBackend *backend() {
        return m_backend;
    }

private:
    Wrapland::Client::Surface *m_surface;
    WaylandBackend *m_backend;
};

class XdgShellOutput : public WaylandOutput
{
public:
    XdgShellOutput(Wrapland::Client::Surface *surface,
                   Wrapland::Client::XdgShell *xdgShell,
                   WaylandBackend *backend, int number);
    ~XdgShellOutput() override;

    void lockPointer(Wrapland::Client::Pointer *pointer, bool lock) override;

private:
    void handleConfigure(const QSize &size, Wrapland::Client::XdgShellToplevel::States states, quint32 serial);
    void updateWindowTitle();

    Wrapland::Client::XdgShellToplevel* xdg_shell_toplevel{nullptr};
    int m_number;
    Wrapland::Client::LockedPointer *m_pointerLock = nullptr;
    bool m_hasPointerLock = false;
};

}
}

#endif
