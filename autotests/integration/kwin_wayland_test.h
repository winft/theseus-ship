/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../../main.h"

#include "input/backend/wlroots/platform.h"
#include "platform/wlroots.h"
#include "render/backend/wlroots/backend.h"

#include <Wrapland/Client/xdg_shell.h>

#include <QtTest>
#include <memory>
#include <vector>

namespace Wrapland
{
namespace Client
{
class AppMenuManager;
class ConnectionThread;
class Compositor;
class IdleInhibitManager;
class LayerShellV1;
class Output;
class PlasmaShell;
class PlasmaWindowManagement;
class PointerConstraints;
class Registry;
class Seat;
class ShadowManager;
class ShmPool;
class SubCompositor;
class SubSurface;
class Surface;
class XdgActivationV1;
class XdgDecorationManager;
}
}

namespace KWin
{
namespace win::wayland
{
class window;
}
namespace Xwl
{
class Xwayland;
}

class Toplevel;

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
    XdgActivation = 1 << 8,
};
Q_DECLARE_FLAGS(AdditionalWaylandInterfaces, AdditionalWaylandInterface)

class KWIN_EXPORT client
{
public:
    Wrapland::Client::ConnectionThread* connection{nullptr};
    std::unique_ptr<QThread> thread;
    std::unique_ptr<Wrapland::Client::EventQueue> queue;
    std::unique_ptr<Wrapland::Client::Registry> registry;

    struct {
        std::unique_ptr<Wrapland::Client::Compositor> compositor;
        std::unique_ptr<Wrapland::Client::LayerShellV1> layer_shell;
        std::unique_ptr<Wrapland::Client::SubCompositor> subcompositor;
        std::unique_ptr<Wrapland::Client::ShadowManager> shadow_manager;
        std::unique_ptr<Wrapland::Client::XdgShell> xdg_shell;
        std::unique_ptr<Wrapland::Client::ShmPool> shm;
        std::unique_ptr<Wrapland::Client::Seat> seat;
        std::unique_ptr<Wrapland::Client::PlasmaShell> plasma_shell;
        std::unique_ptr<Wrapland::Client::PlasmaWindowManagement> window_management;
        std::unique_ptr<Wrapland::Client::PointerConstraints> pointer_constraints;
        std::vector<std::unique_ptr<Wrapland::Client::Output>> outputs;
        std::unique_ptr<Wrapland::Client::IdleInhibitManager> idle_inhibit;
        std::unique_ptr<Wrapland::Client::AppMenuManager> app_menu;
        std::unique_ptr<Wrapland::Client::XdgActivationV1> xdg_activation;
        std::unique_ptr<Wrapland::Client::XdgDecorationManager> xdg_decoration;
    } interfaces;

    client() = default;
    explicit client(AdditionalWaylandInterfaces flags);
    client(client const&) = delete;
    client& operator=(client const&) = delete;
    client(client&& other) noexcept;
    client& operator=(client&& other) noexcept;
    ~client();

private:
    QMetaObject::Connection output_announced;
    std::vector<QMetaObject::Connection> output_removals;

    void connect_outputs();
    QMetaObject::Connection output_removal_connection(Wrapland::Client::Output* output);
    void cleanup();
};

}

class KWIN_EXPORT WaylandTestApplication : public ApplicationWaylandAbstract
{
    Q_OBJECT
public:
    wlr_input_device* pointer{nullptr};
    wlr_input_device* keyboard{nullptr};
    wlr_input_device* touch{nullptr};

    std::vector<Test::client> clients;

    WaylandTestApplication(OperationMode mode, int& argc, char** argv);
    ~WaylandTestApplication() override;

    void continueStartupWithCompositor() override;

protected:
    void performStartup() override;

private:
    void createBackend();
    void init_wlroots_backend();
    void continueStartupWithScene();
    void finalizeStartup();

    std::unique_ptr<platform_base::wlroots> base;
    std::unique_ptr<input::platform> input;
    std::unique_ptr<render::backend::wlroots::backend> render;
    Xwl::Xwayland* m_xwayland = nullptr;
};

namespace Test
{

/**
 * Creates a Wayland Connection in a dedicated thread and creates various
 * client side objects which can be used to create windows.
 * @see destroy_wayland_connection
 */
KWIN_EXPORT void setup_wayland_connection(AdditionalWaylandInterfaces flags
                                          = AdditionalWaylandInterfaces());

/**
 * Destroys the Wayland Connection created with @link{setup_wayland_connection}.
 * This can be called from cleanup in order to ensure that no Wayland Connection
 * leaks into the next test method.
 * @see setup_wayland_connection
 */
KWIN_EXPORT void destroy_wayland_connection();

KWIN_EXPORT client& get_client();
KWIN_EXPORT std::vector<client>& get_all_clients();

KWIN_EXPORT bool wait_for_wayland_pointer();
KWIN_EXPORT bool wait_for_wayland_touch();
KWIN_EXPORT bool wait_for_wayland_keyboard();

KWIN_EXPORT void flush_wayland_connection();
KWIN_EXPORT void flush_wayland_connection(client const& clt);

KWIN_EXPORT std::unique_ptr<Wrapland::Client::Surface> create_surface();
KWIN_EXPORT std::unique_ptr<Wrapland::Client::Surface> create_surface(client const& clt);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::SubSurface>
create_subsurface(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                  std::unique_ptr<Wrapland::Client::Surface> const& parentSurface);

enum class CreationSetup {
    CreateOnly,
    CreateAndConfigure, /// commit and wait for the configure event, making this surface ready to
                        /// commit buffers
};

KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellToplevel>
create_xdg_shell_toplevel(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          CreationSetup = CreationSetup::CreateAndConfigure);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellToplevel>
create_xdg_shell_toplevel(client const& clt,
                          std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          CreationSetup = CreationSetup::CreateAndConfigure);

KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellPopup>
create_xdg_shell_popup(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                       std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& parent_toplevel,
                       Wrapland::Client::XdgPositioner const& positioner,
                       CreationSetup = CreationSetup::CreateAndConfigure);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellPopup>
create_xdg_shell_popup(client const& clt,
                       std::unique_ptr<Wrapland::Client::Surface> const& surface,
                       std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& parent_toplevel,
                       Wrapland::Client::XdgPositioner const& positioner,
                       CreationSetup = CreationSetup::CreateAndConfigure);

/**
 * Commits the XdgShellToplevel to the given surface, and waits for the configure event from the
 * compositor
 */
KWIN_EXPORT void
init_xdg_shell_toplevel(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& shell_toplevel);
KWIN_EXPORT void
init_xdg_shell_popup(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                     std::unique_ptr<Wrapland::Client::XdgShellPopup> const& popup);

/**
 * Creates a shared memory buffer of @p size in @p color and attaches it to the @p surface.
 * The @p surface gets damaged and committed, thus it's rendered.
 */
KWIN_EXPORT void render(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QSize& size,
                        const QColor& color,
                        const QImage::Format& format = QImage::Format_ARGB32_Premultiplied);
KWIN_EXPORT void render(client const& clt,
                        std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QSize& size,
                        const QColor& color,
                        const QImage::Format& format = QImage::Format_ARGB32_Premultiplied);

/**
 * Creates a shared memory buffer using the supplied image @p img and attaches it to the @p surface
 */
KWIN_EXPORT void render(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QImage& img);
KWIN_EXPORT void render(client const& clt,
                        std::unique_ptr<Wrapland::Client::Surface> const& surface,
                        const QImage& img);

/**
 * Renders and then waits untill the new window is shown. Returns the created window.
 * If no window gets shown during @p timeout @c null is returned.
 */
KWIN_EXPORT win::wayland::window*
render_and_wait_for_shown(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          QSize const& size,
                          QColor const& color,
                          QImage::Format const& format = QImage::Format_ARGB32_Premultiplied,
                          int timeout = 5000);
KWIN_EXPORT win::wayland::window*
render_and_wait_for_shown(client const& clt,
                          std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          QSize const& size,
                          QColor const& color,
                          QImage::Format const& format = QImage::Format_ARGB32_Premultiplied,
                          int timeout = 5000);

/**
 * Waits for the @p client to be destroyed.
 */
KWIN_EXPORT bool wait_for_destroyed(KWin::Toplevel* window);

/**
 * Locks the screen and waits till the screen is locked.
 * @returns @c true if the screen could be locked, @c false otherwise
 */
KWIN_EXPORT void lock_screen();

/**
 * Unlocks the screen and waits till the screen is unlocked.
 * @returns @c true if the screen could be unlocked, @c false otherwise
 */
KWIN_EXPORT void unlock_screen();

KWIN_EXPORT void pointer_motion_absolute(QPointF const& position, uint32_t time);

KWIN_EXPORT void pointer_button_pressed(uint32_t button, uint32_t time);
KWIN_EXPORT void pointer_button_released(uint32_t button, uint32_t time);

KWIN_EXPORT void pointer_axis_horizontal(double delta, uint32_t time, int32_t discrete_delta);
KWIN_EXPORT void pointer_axis_vertical(double delta, uint32_t time, int32_t discrete_delta);

KWIN_EXPORT void keyboard_key_pressed(uint32_t key, uint32_t time);
KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time);

KWIN_EXPORT void touch_down(int32_t id, QPointF const& position, uint32_t time);
KWIN_EXPORT void touch_up(int32_t id, uint32_t time);
}

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::Test::AdditionalWaylandInterfaces)

#define WAYLANDTEST_MAIN_HELPER(TestObject, DPI, OperationMode)                                    \
    int main(int argc, char* argv[])                                                               \
    {                                                                                              \
        setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);                               \
        setenv(                                                                                    \
            "QT_QPA_PLATFORM_PLUGIN_PATH",                                                         \
            QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toLocal8Bit().constData(),   \
            true);                                                                                 \
        setenv("KWIN_FORCE_OWN_QPA", "1", true);                                                   \
        qunsetenv("KDE_FULL_SESSION");                                                             \
        qunsetenv("KDE_SESSION_VERSION");                                                          \
        qunsetenv("XDG_SESSION_DESKTOP");                                                          \
        qunsetenv("XDG_CURRENT_DESKTOP");                                                          \
        DPI;                                                                                       \
        KWin::WaylandTestApplication app(OperationMode, argc, argv);                               \
        app.setAttribute(Qt::AA_Use96Dpi, true);                                                   \
        TestObject tc;                                                                             \
        return QTest::qExec(&tc, argc, argv);                                                      \
    }

#ifdef NO_XWAYLAND
#define WAYLANDTEST_MAIN(TestObject)                                                               \
    WAYLANDTEST_MAIN_HELPER(TestObject,                                                            \
                            QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps),              \
                            KWin::Application::OperationModeWaylandOnly)
#else
#define WAYLANDTEST_MAIN(TestObject)                                                               \
    WAYLANDTEST_MAIN_HELPER(TestObject,                                                            \
                            QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps),              \
                            KWin::Application::OperationModeXwayland)
#endif
