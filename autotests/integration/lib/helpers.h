/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

// Must be included first because of collision with Xlib defines.
#include <QtTest>

#include "types.h"

#include "base/output.h"
#include "base/wayland/platform.h"
#include "base/wayland/server.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/xdg_shell.h>

struct wl_signal;
struct wlr_input_device;
struct wlr_keyboard;

namespace Wrapland::Client
{
class SubSurface;
class Surface;
}

namespace KWin::detail::test
{

class client;

using space = win::wayland::space<render::wayland::platform<base::wayland::platform>,
                                  input::wayland::platform<base::wayland::platform>>;
using wayland_window = win::wayland::window<space>;

struct KWIN_EXPORT output {
    explicit output(QRect const& geometry);
    output(QRect const& geometry, double scale);

    // Geometry in logical space.
    QRect geometry;
    double scale;
};

KWIN_EXPORT input::wayland::cursor<space::input_t>* cursor();

/**
 * Creates a Wayland Connection in a dedicated thread and creates various
 * client side objects which can be used to create windows.
 * @see destroy_wayland_connection
 */
KWIN_EXPORT void setup_wayland_connection(global_selection globals = {});

/**
 * Destroys the Wayland Connection created with @link{setup_wayland_connection}.
 * This can be called from cleanup in order to ensure that no Wayland Connection
 * leaks into the next test method.
 * @see setup_wayland_connection
 */
KWIN_EXPORT void destroy_wayland_connection();

KWIN_EXPORT base::output* get_output(size_t index);
KWIN_EXPORT void set_current_output(int index);

KWIN_EXPORT void test_outputs_default();
KWIN_EXPORT void test_outputs_geometries(std::vector<QRect> const& geometries);

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
                       Wrapland::Client::xdg_shell_positioner_data positioner_data,
                       CreationSetup = CreationSetup::CreateAndConfigure);
KWIN_EXPORT std::unique_ptr<Wrapland::Client::XdgShellPopup>
create_xdg_shell_popup(client const& clt,
                       std::unique_ptr<Wrapland::Client::Surface> const& surface,
                       std::unique_ptr<Wrapland::Client::XdgShellToplevel> const& parent_toplevel,
                       Wrapland::Client::xdg_shell_positioner_data positioner_data,
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
KWIN_EXPORT wayland_window*
render_and_wait_for_shown(std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          QSize const& size,
                          QColor const& color,
                          QImage::Format const& format = QImage::Format_ARGB32_Premultiplied,
                          int timeout = 5000);
KWIN_EXPORT wayland_window*
render_and_wait_for_shown(client const& clt,
                          std::unique_ptr<Wrapland::Client::Surface> const& surface,
                          QSize const& size,
                          QColor const& color,
                          QImage::Format const& format = QImage::Format_ARGB32_Premultiplied,
                          int timeout = 5000);

/**
 * Waits for the @p client to be destroyed.
 */
template<typename Window>
bool wait_for_destroyed(Window* window)
{
    QSignalSpy destroyedSpy(window->qobject.get(), &QObject::destroyed);
    if (!destroyedSpy.isValid()) {
        return false;
    }
    return destroyedSpy.wait();
}

template<typename Window, typename Variant>
Window* get_window(Variant window)
{
    auto helper_get = [](auto&& win) -> Window* {
        if (!std::holds_alternative<Window*>(win)) {
            return nullptr;
        }
        return std::get<Window*>(win);
    };

    if constexpr (requires(Variant win) { win.has_value(); }) {
        if (!window) {
            return nullptr;
        }
        return helper_get(*window);
    } else {
        return helper_get(window);
    }
}

template<typename Window>
auto get_wayland_window(Window window)
{
    if constexpr (requires(Window win) { win.has_value(); }) {
        return get_window<typename std::remove_pointer_t<
            std::variant_alternative_t<0, typename Window::value_type>>::space_t::wayland_window>(
            window);
    } else {
        return get_window<typename std::remove_pointer_t<
            std::variant_alternative_t<0, Window>>::space_t::wayland_window>(window);
    }
}

template<typename Window>
auto get_x11_window(Window window)
{
    if constexpr (requires(Window win) { win.has_value(); }) {
        return get_window<typename std::remove_pointer_t<
            std::variant_alternative_t<0, typename Window::value_type>>::space_t::x11_window>(
            window);
    } else {
        return get_window<typename std::remove_pointer_t<
            std::variant_alternative_t<0, Window>>::space_t::x11_window>(window);
    }
}

template<typename Window>
auto get_internal_window(Window window)
{
    if constexpr (requires(Window win) { win.has_value(); }) {
        return get_window<typename std::remove_pointer_t<
            std::variant_alternative_t<0, typename Window::value_type>>::space_t::
                              internal_window_t>(window);
    } else {
        return get_window<typename std::remove_pointer_t<
            std::variant_alternative_t<0, Window>>::space_t::internal_window_t>(window);
    }
}

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

KWIN_EXPORT void wlr_signal_emit_safe(wl_signal* signal, void* data);

KWIN_EXPORT void pointer_motion_absolute(QPointF const& position, uint32_t time);

KWIN_EXPORT void pointer_button_pressed(uint32_t button, uint32_t time);
KWIN_EXPORT void pointer_button_released(uint32_t button, uint32_t time);

KWIN_EXPORT void pointer_axis_horizontal(double delta, uint32_t time, int32_t discrete_delta);
KWIN_EXPORT void pointer_axis_vertical(double delta, uint32_t time, int32_t discrete_delta);

KWIN_EXPORT void keyboard_key_pressed(uint32_t key, uint32_t time);
KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time);

KWIN_EXPORT void keyboard_key_pressed(uint32_t key, uint32_t time, wlr_keyboard* keyboard);
KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time, wlr_keyboard* keyboard);

KWIN_EXPORT void touch_down(int32_t id, QPointF const& position, uint32_t time);
KWIN_EXPORT void touch_up(int32_t id, uint32_t time);
KWIN_EXPORT void touch_motion(int32_t id, QPointF const& position, uint32_t time);
KWIN_EXPORT void touch_cancel();

KWIN_EXPORT void swipe_begin(uint32_t fingers, uint32_t time);
KWIN_EXPORT void swipe_update(uint32_t fingers, double dx, double dy, uint32_t time);
KWIN_EXPORT void swipe_end(uint32_t time);
KWIN_EXPORT void swipe_cancel(uint32_t time);

KWIN_EXPORT void pinch_begin(uint32_t fingers, uint32_t time);
KWIN_EXPORT void
pinch_update(uint32_t fingers, double dx, double dy, double scale, double rotation, uint32_t time);
KWIN_EXPORT void pinch_end(uint32_t time);
KWIN_EXPORT void pinch_cancel(uint32_t time);

KWIN_EXPORT void hold_begin(uint32_t fingers, uint32_t time);
KWIN_EXPORT void hold_end(uint32_t time);
KWIN_EXPORT void hold_cancel(uint32_t time);

KWIN_EXPORT void prepare_app_env(std::string const& qpa_plugin_path);
KWIN_EXPORT void prepare_sys_env(std::string const& socket_name);
KWIN_EXPORT std::string create_socket_name(std::string base);

}
