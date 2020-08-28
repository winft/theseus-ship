/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>
Copyright 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_WAYLAND_BACKEND_H
#define KWIN_WAYLAND_BACKEND_H
// KWin
#include "platform.h"
#include <config-kwin.h>
#include <kwinglobals.h>
// Qt
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QSize>

class QTemporaryFile;
struct wl_buffer;
struct wl_display;
struct wl_event_queue;
struct wl_seat;

namespace Wrapland
{
namespace Client
{
class Buffer;
class ShmPool;
class Compositor;
class ConnectionThread;
class EventQueue;
class Keyboard;
class Pointer;
class PointerConstraints;
class PointerGestures;
class PointerSwipeGesture;
class PointerPinchGesture;
class Registry;
class RelativePointer;
class RelativePointerManager;
class Seat;
class SubCompositor;
class SubSurface;
class Surface;
class Touch;
class XdgDecorationManager;
class XdgShell;
}
}

namespace KWin
{
class WaylandCursorTheme;

namespace Wayland
{

class WaylandBackend;
class WaylandSeat;
class WaylandOutput;

class WaylandCursor : public QObject
{
    Q_OBJECT
public:
    explicit WaylandCursor(WaylandBackend *backend);
    ~WaylandCursor() override;

    virtual void init();
    virtual void move(const QPointF &globalPosition) {
        Q_UNUSED(globalPosition)
    }

    void installImage();

protected:
    void resetSurface();
    virtual void doInstallImage(wl_buffer *image, const QSize &size);
    void drawSurface(wl_buffer *image, const QSize &size);

    Wrapland::Client::Surface *surface() const {
        return m_surface;
    }
    WaylandBackend *backend() const {
        return m_backend;
    }

private:
    WaylandBackend *m_backend;
    Wrapland::Client::Pointer *m_pointer;
    Wrapland::Client::Surface *m_surface = nullptr;
};

class WaylandSubSurfaceCursor : public WaylandCursor
{
    Q_OBJECT
public:
    explicit WaylandSubSurfaceCursor(WaylandBackend *backend);
    ~WaylandSubSurfaceCursor() override;

    void init() override;

    void move(const QPointF &globalPosition) override;

private:
    void changeOutput(WaylandOutput *output);
    void doInstallImage(wl_buffer *image, const QSize &size) override;
    void createSubSurface();

    QPointF absoluteToRelativePosition(const QPointF &position);
    WaylandOutput *m_output = nullptr;
    Wrapland::Client::SubSurface *m_subSurface = nullptr;
};

class WaylandSeat : public QObject
{
    Q_OBJECT
public:
    WaylandSeat(wl_seat *seat, WaylandBackend *backend);
    ~WaylandSeat() override;

    Wrapland::Client::Pointer *pointer() const {
        return m_pointer;
    }

    void installGesturesInterface(Wrapland::Client::PointerGestures *gesturesInterface) {
        m_gesturesInterface = gesturesInterface;
        setupPointerGestures();
    }

private:
    void destroyPointer();
    void destroyKeyboard();
    void destroyTouch();
    void setupPointerGestures();

    Wrapland::Client::Seat *m_seat;
    Wrapland::Client::Pointer *m_pointer;
    Wrapland::Client::Keyboard *m_keyboard;
    Wrapland::Client::Touch *m_touch;
    Wrapland::Client::PointerGestures *m_gesturesInterface = nullptr;
    Wrapland::Client::PointerPinchGesture *m_pinchGesture = nullptr;
    Wrapland::Client::PointerSwipeGesture *m_swipeGesture = nullptr;

    uint32_t m_enteredSerial;

    WaylandBackend *m_backend;
};

/**
* @brief Class encapsulating all Wayland data structures needed by the Egl backend.
*
* It creates the connection to the Wayland Compositor, sets up the registry and creates
* the Wayland output surfaces and its shell mappings.
*/
class KWIN_EXPORT WaylandBackend : public Platform
{
    Q_OBJECT
    Q_INTERFACES(KWin::Platform)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Platform" FILE "wayland.json")
public:
    explicit WaylandBackend(QObject *parent = nullptr);
    ~WaylandBackend() override;
    void init() override;
    wl_display *display();
    Wrapland::Client::Compositor *compositor();
    Wrapland::Client::SubCompositor *subCompositor();
    Wrapland::Client::ShmPool *shmPool();

    OpenGLBackend *createOpenGLBackend() override;
    QPainterBackend *createQPainterBackend() override;

    void flush();

    WaylandSeat *seat() const {
        return m_seat;
    }
    Wrapland::Client::PointerConstraints *pointerConstraints() const {
        return m_pointerConstraints;
    }
    Wrapland::Client::XdgDecorationManager *xdgDecorationManager() const {
        return m_xdgDecorationManager;
    }

    void pointerMotionRelativeToOutput(const QPointF &position, quint32 time);

    bool supportsPointerLock();
    void togglePointerLock();
    bool pointerIsLocked();

    QVector<CompositingType> supportedCompositors() const override;

    void checkBufferSwap();

    WaylandOutput* getOutputAt(const QPointF globalPosition);
    Outputs outputs() const override;
    Outputs enabledOutputs() const override;
    QVector<WaylandOutput*> waylandOutputs() const {
        return m_outputs;
    }

Q_SIGNALS:
    void outputAdded(WaylandOutput *output);
    void outputRemoved(WaylandOutput *output);

    void connectionFailed();

    void pointerLockSupportedChanged();
    void pointerLockChanged(bool locked);

private:
    void initConnection();
    void createOutputs();

    void updateScreenSize(WaylandOutput *output);
    void relativeMotionHandler(const QSizeF &delta, const QSizeF &deltaNonAccelerated, quint64 timestamp);

    wl_display *m_display;
    Wrapland::Client::EventQueue *m_eventQueue;
    Wrapland::Client::Registry *m_registry;
    Wrapland::Client::Compositor *m_compositor;
    Wrapland::Client::SubCompositor *m_subCompositor;
    Wrapland::Client::XdgShell *m_xdgShell = nullptr;
    Wrapland::Client::XdgDecorationManager *m_xdgDecorationManager = nullptr;
    Wrapland::Client::ShmPool *m_shm;
    Wrapland::Client::ConnectionThread *m_connectionThreadObject;

    WaylandSeat *m_seat = nullptr;
    Wrapland::Client::RelativePointer *m_relativePointer = nullptr;
    Wrapland::Client::RelativePointerManager *m_relativePointerManager = nullptr;
    Wrapland::Client::PointerConstraints *m_pointerConstraints = nullptr;

    QThread *m_connectionThread;
    QVector<WaylandOutput*> m_outputs;

    WaylandCursor *m_waylandCursor = nullptr;

    bool m_pointerLockRequested = false;
};

inline
wl_display *WaylandBackend::display()
{
    return m_display;
}

inline
Wrapland::Client::Compositor *WaylandBackend::compositor()
{
    return m_compositor;
}

inline
Wrapland::Client::SubCompositor *WaylandBackend::subCompositor()
{
    return m_subCompositor;
}

inline
Wrapland::Client::ShmPool* WaylandBackend::shmPool()
{
    return m_shm;
}

} // namespace Wayland
} // namespace KWin

#endif //  KWIN_WAYLAND_BACKEND_H
