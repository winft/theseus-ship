/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "toplevel.h"
#include <kwin_export.h>

#include <Wrapland/Server/xdg_shell.h>

#include <memory>
#include <vector>

namespace Wrapland
{
namespace Server
{
class ServerSideDecorationPalette;
class Appmenu;
class input_method_popup_surface_v2;
class LayerSurfaceV1;
class PlasmaShellSurface;
class XdgDecoration;
}
}

namespace KWin::win::wayland
{

class KWIN_EXPORT window : public Toplevel
{
    Q_OBJECT
public:
    constexpr static bool is_toplevel{false};

    bool initialized{false};
    NET::WindowType window_type{NET::Normal};

    bool user_no_border{false};

    bool hidden{false};
    bool mapped{false};
    bool closing{false};

    double m_opacity = 1.0;

    struct configure_event {
        uint32_t serial{0};

        // Geometry to apply after a resize operation has been completed.
        struct {
            QRect frame;
            maximize_mode max_mode{maximize_mode::restore};
            bool fullscreen{false};
        } geometry;
    };
    std::vector<configure_event> pending_configures;

    void handle_commit();
    void do_set_maximize_mode(win::maximize_mode mode);
    void do_set_fullscreen(bool full);

    bool acceptsFocus() const override;
    void updateCaption() override;

    maximize_mode max_mode{maximize_mode::restore};

    struct {
        QRect window;
        maximize_mode max_mode{maximize_mode::restore};
        bool fullscreen{false};
    } synced_geometry;

    Wrapland::Server::XdgShellSurface* shell_surface{nullptr};
    Wrapland::Server::XdgShellToplevel* toplevel{nullptr};
    Wrapland::Server::XdgShellPopup* popup{nullptr};
    Wrapland::Server::LayerSurfaceV1* layer_surface{nullptr};
    Wrapland::Server::input_method_popup_surface_v2* input_method_popup{nullptr};

    Wrapland::Server::XdgDecoration* xdg_deco{nullptr};
    Wrapland::Server::PlasmaShellSurface* plasma_shell_surface{nullptr};
    Wrapland::Server::ServerSideDecorationPalette* palette{nullptr};

    enum class ping_reason {
        close = 0,
        focus,
    };
    std::map<uint32_t, ping_reason> pings;
    uint32_t acked_configure{0};

    bool must_place{false};

    window(Wrapland::Server::Surface* surface);
    ~window() = default;

    qreal bufferScale() const override;
    bool is_wayland_window() const override;
    bool setupCompositing(bool add_full_damage) override;
    void add_scene_window_addon() override;

    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    QByteArray windowRole() const override;

    double opacity() const override;
    void setOpacity(double opacity) override;

    bool isShown() const override;
    bool isHiddenInternal() const override;

    QSize minSize() const override;
    QSize maxSize() const override;

    void configure_geometry(QRect const& rect);
    void apply_pending_geometry();
    void do_set_geometry(QRect const& frame_geo);

    void map();
    void unmap();

    void ping(ping_reason reason);

    // When another window is created, checks if this window is a subsurface for it.
    void checkTransient(Toplevel* window) override;

    void debug(QDebug& stream) const override;

    win::maximize_mode maximizeMode() const override;
    bool noBorder() const override;
    void setFullScreen(bool full, bool user = true) override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void takeFocus() override;
    bool userCanSetFullScreen() const override;
    bool userCanSetNoBorder() const override;
    bool wantsInput() const override;
    bool dockWantsInput() const override;

    bool has_exclusive_keyboard_interactivity() const;

    bool hasStrut() const override;
    pid_t pid() const override;
    bool isLocalhost() const override;
    bool isLockScreen() const override;
    bool isInitialPositionSet() const override;
    void showOnScreenEdge() override;

    void cancel_popup();

    void closeWindow() override;
    bool isCloseable() const override;
    bool isMaximizable() const override;
    bool isMinimizable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isResizable() const override;
    void hideClient(bool hide) override;

    void placeIn(const QRect& area);

    void update_maximized(maximize_mode mode) override;
    void doResizeSync() override;
    bool belongsToSameApplication(Toplevel const* other,
                                  win::same_client_check checks) const override;
    bool belongsToDesktop() const override;

    void doSetActive() override;
    void doMinimize() override;

    void setFrameGeometry(QRect const& rect) override;

    win::layer layer_for_dock() const override;
    bool has_pending_repaints() const override;

    void updateColorScheme() override;
    bool isInputMethod() const override;
    bool is_popup_end() const override;
    void killWindow() override;

    bool supportsWindowRules() const override;

    void handle_class_changed();
    void handle_title_changed();
};

}

Q_DECLARE_METATYPE(KWin::win::wayland::window*)
