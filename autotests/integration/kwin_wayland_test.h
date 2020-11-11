/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_WAYLAND_TEST_H
#define KWIN_WAYLAND_TEST_H

#include "../../main.h"

// Qt
#include <QtTest>

// Wrapland
#include <Wrapland/Client/xdgshell.h>

namespace Wrapland
{
namespace Client
{
class AppMenuManager;
class ConnectionThread;
class Compositor;
class IdleInhibitManager;
class PlasmaShell;
class PlasmaWindowManagement;
class PointerConstraints;
class Seat;
class ShadowManager;
class ShmPool;
class SubCompositor;
class SubSurface;
class Surface;
class XdgDecorationManager;
}
}

namespace KWin
{
namespace Xwl
{
class Xwayland;
}

class Toplevel;
class XdgShellClient;

class WaylandTestApplication : public ApplicationWaylandAbstract
{
    Q_OBJECT
public:
    WaylandTestApplication(OperationMode mode, int &argc, char **argv);
    ~WaylandTestApplication() override;

    void continueStartupWithCompositor() override;

protected:
    void performStartup() override;

private:
    void createBackend();
    void continueStartupWithScene();
    void finalizeStartup();

    Xwl::Xwayland *m_xwayland = nullptr;
};

namespace Test
{

enum class AdditionalWaylandInterface {
    Seat = 1 << 0,
    XdgDecoration = 1 << 1,
    PlasmaShell = 1 << 2,
    WindowManagement = 1 << 3,
    PointerConstraints = 1 << 4,
    IdleInhibition = 1 << 5,
    AppMenu = 1 << 6,
    ShadowManager = 1 << 7,
};
Q_DECLARE_FLAGS(AdditionalWaylandInterfaces, AdditionalWaylandInterface)
/**
 * Creates a Wayland Connection in a dedicated thread and creates various
 * client side objects which can be used to create windows.
 * @see destroyWaylandConnection
 */
void setupWaylandConnection(AdditionalWaylandInterfaces flags = AdditionalWaylandInterfaces());

/**
 * Destroys the Wayland Connection created with @link{setupWaylandConnection}.
 * This can be called from cleanup in order to ensure that no Wayland Connection
 * leaks into the next test method.
 * @see setupWaylandConnection
 */
void destroyWaylandConnection();

Wrapland::Client::ConnectionThread *waylandConnection();
Wrapland::Client::Compositor *waylandCompositor();
Wrapland::Client::SubCompositor *waylandSubCompositor();
Wrapland::Client::ShadowManager *waylandShadowManager();
Wrapland::Client::ShmPool *waylandShmPool();
Wrapland::Client::Seat *waylandSeat();
Wrapland::Client::PlasmaShell *waylandPlasmaShell();
Wrapland::Client::PlasmaWindowManagement *waylandWindowManagement();
Wrapland::Client::PointerConstraints *waylandPointerConstraints();
Wrapland::Client::IdleInhibitManager *waylandIdleInhibitManager();
Wrapland::Client::AppMenuManager *waylandAppMenuManager();
Wrapland::Client::XdgDecorationManager *xdgDecorationManager();

bool waitForWaylandPointer();
bool waitForWaylandTouch();
bool waitForWaylandKeyboard();

void flushWaylandConnection();

Wrapland::Client::Surface *createSurface(QObject *parent = nullptr);
Wrapland::Client::SubSurface *createSubSurface(Wrapland::Client::Surface *surface,
                                               Wrapland::Client::Surface *parentSurface, QObject *parent = nullptr);
enum class XdgShellSurfaceType {
    XdgShellStable
};

enum class CreationSetup {
    CreateOnly,
    CreateAndConfigure, /// commit and wait for the configure event, making this surface ready to commit buffers
};

Wrapland::Client::XdgShellSurface *createXdgShellSurface(XdgShellSurfaceType type,
                                                         Wrapland::Client::Surface *surface,
                                                         QObject *parent = nullptr,
                                                         CreationSetup creationSetup = CreationSetup::CreateAndConfigure);

Wrapland::Client::XdgShellSurface *createXdgShellStableSurface(Wrapland::Client::Surface *surface,
                                                               QObject *parent = nullptr,
                                                               CreationSetup = CreationSetup::CreateAndConfigure);
Wrapland::Client::XdgShellPopup *createXdgShellStablePopup(Wrapland::Client::Surface *surface,
                                                           Wrapland::Client::XdgShellSurface *parentSurface,
                                                           const Wrapland::Client::XdgPositioner &positioner,
                                                           QObject *parent = nullptr,
                                                           CreationSetup = CreationSetup::CreateAndConfigure);


/**
 * Commits the XdgShellSurface to the given surface, and waits for the configure event from the compositor
 */
void initXdgShellSurface(Wrapland::Client::Surface *surface, Wrapland::Client::XdgShellSurface *shellSurface);
void initXdgShellPopup(Wrapland::Client::Surface *surface, Wrapland::Client::XdgShellPopup *popup);



/**
 * Creates a shared memory buffer of @p size in @p color and attaches it to the @p surface.
 * The @p surface gets damaged and committed, thus it's rendered.
 */
void render(Wrapland::Client::Surface *surface, const QSize &size, const QColor &color, const QImage::Format &format = QImage::Format_ARGB32_Premultiplied);

/**
 * Creates a shared memory buffer using the supplied image @p img and attaches it to the @p surface
 */
void render(Wrapland::Client::Surface *surface, const QImage &img);

/**
 * Waits till a new XdgShellClient is shown and returns the created XdgShellClient.
 * If no XdgShellClient gets shown during @p timeout @c null is returned.
 */
XdgShellClient *waitForWaylandWindowShown(int timeout = 5000);

/**
 * Combination of @link{render} and @link{waitForWaylandWindowShown}.
 */
XdgShellClient *renderAndWaitForShown(Wrapland::Client::Surface *surface, const QSize &size, const QColor &color, const QImage::Format &format = QImage::Format_ARGB32, int timeout = 5000);

/**
 * Waits for the @p client to be destroyed.
 */
bool waitForWindowDestroyed(KWin::Toplevel* window);

/**
 * Locks the screen and waits till the screen is locked.
 * @returns @c true if the screen could be locked, @c false otherwise
 */
void lockScreen();

/**
 * Unlocks the screen and waits till the screen is unlocked.
 * @returns @c true if the screen could be unlocked, @c false otherwise
 */
void unlockScreen();
}

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Test::AdditionalWaylandInterfaces)
Q_DECLARE_METATYPE(KWin::Test::XdgShellSurfaceType)

#define WAYLANDTEST_MAIN_HELPER(TestObject, DPI, OperationMode) \
int main(int argc, char *argv[]) \
{ \
    setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true); \
    setenv("QT_QPA_PLATFORM_PLUGIN_PATH", QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toLocal8Bit().constData(), true); \
    setenv("KWIN_FORCE_OWN_QPA", "1", true); \
    qunsetenv("KDE_FULL_SESSION"); \
    qunsetenv("KDE_SESSION_VERSION"); \
    qunsetenv("XDG_SESSION_DESKTOP"); \
    qunsetenv("XDG_CURRENT_DESKTOP"); \
    DPI; \
    KWin::WaylandTestApplication app(OperationMode, argc, argv); \
    app.setAttribute(Qt::AA_Use96Dpi, true); \
    TestObject tc; \
    return QTest::qExec(&tc, argc, argv); \
}

#ifdef NO_XWAYLAND
#define WAYLANDTEST_MAIN(TestObject) WAYLANDTEST_MAIN_HELPER(TestObject, QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps), KWin::Application::OperationModeWaylandOnly)
#else
#define WAYLANDTEST_MAIN(TestObject) WAYLANDTEST_MAIN_HELPER(TestObject, QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps), KWin::Application::OperationModeXwayland)
#endif

#endif
