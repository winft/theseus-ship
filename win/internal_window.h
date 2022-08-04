/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 Martin Flöser <mgraesslin@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#pragma once

#include "toplevel.h"

namespace KWin::win
{
class internal_control;

class KWIN_EXPORT internal_window : public Toplevel
{
    Q_OBJECT

public:
    constexpr static bool is_toplevel{false};

    internal_window(win::remnant remnant, win::space& space);
    internal_window(QWindow* window, win::space& space);
    ~internal_window() override;

    bool setupCompositing() override;
    void add_scene_window_addon() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    qreal bufferScale() const override;
    void debug(QDebug& stream) const override;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    double opacity() const override;
    void setOpacity(double opacity) override;
    void killWindow() override;
    bool is_popup_end() const override;
    QByteArray windowRole() const override;
    void closeWindow() override;
    bool isCloseable() const override;
    bool isMaximizable() const override;
    bool isMinimizable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isResizable() const override;
    bool placeable() const;
    bool noBorder() const override;
    bool userCanSetNoBorder() const override;
    bool wantsInput() const override;
    bool isInternal() const override;
    bool isLockScreen() const override;
    bool isOutline() const override;
    bool isShown() const override;
    bool isHiddenInternal() const override;
    void hideClient(bool hide) override;
    void setFrameGeometry(QRect const& rect) override;
    void apply_restore_geometry(QRect const& restore_geo) override;
    void restore_geometry_from_fullscreen() override;
    bool hasStrut() const override;
    bool supportsWindowRules() const override;
    void takeFocus() override;
    bool userCanSetFullScreen() const override;
    void setFullScreen(bool set, bool user = true) override;
    void handle_update_fullscreen(bool full) override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void updateColorScheme() override;
    void showOnScreenEdge() override;
    void checkTransient(Toplevel* window) override;
    bool belongsToDesktop() const override;

    void destroyClient();
    void present(std::shared_ptr<QOpenGLFramebufferObject> const& fbo);
    void present(const QImage& image, const QRegion& damage);
    QWindow* internalWindow() const;

    bool has_pending_repaints() const override;

    struct {
        std::shared_ptr<QOpenGLFramebufferObject> fbo;
        QImage image;
    } buffers;

protected:
    bool acceptsFocus() const override;
    bool belongsToSameApplication(Toplevel const* other,
                                  win::same_client_check checks) const override;
    void doResizeSync() override;
    void updateCaption() override;

private:
    double buffer_scale_internal() const;
    void createDecoration(const QRect& rect);
    void setCaption(QString const& cap);
    void markAsMapped();

    void requestGeometry(const QRect& rect);
    void do_set_geometry(QRect const& frame_geo);
    void updateInternalWindowGeometry();

    QWindow* m_internalWindow = nullptr;
    QRect synced_geo;
    double m_opacity = 1.0;
    NET::WindowType m_windowType = NET::Normal;
    Qt::WindowFlags m_internalWindowFlags = Qt::WindowFlags();
    bool m_userNoBorder = false;

    Q_DISABLE_COPY(internal_window)

    friend class internal_control;
};

}
