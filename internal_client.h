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

namespace KWin
{
class internal_control;

class KWIN_EXPORT InternalClient : public Toplevel
{
    Q_OBJECT

public:
    explicit InternalClient(QWindow *window);
    ~InternalClient() override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    QRect bufferGeometry() const override;
    QStringList activities() const override;
    void blockActivityUpdates(bool b = true) override;
    qreal bufferScale() const override;
    void debug(QDebug &stream) const override;
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
    bool noBorder() const override;
    bool userCanSetNoBorder() const override;
    bool wantsInput() const override;
    bool isInternal() const override;
    bool isLockScreen() const override;
    bool isInputMethod() const override;
    bool isOutline() const override;
    quint32 windowId() const override;
    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override;
    void hideClient(bool hide) override;
    void resizeWithChecks(QSize const& size, win::force_geometry force = win::force_geometry::no) override;
    void setFrameGeometry(QRect const& rect, win::force_geometry force = win::force_geometry::no) override;
    bool supportsWindowRules() const override;
    void setOnAllActivities(bool set) override;
    void takeFocus() override;
    bool userCanSetFullScreen() const override;
    void setFullScreen(bool set, bool user = true) override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void updateColorScheme() override;
    void showOnScreenEdge() override;

    void destroyClient();
    void present(const QSharedPointer<QOpenGLFramebufferObject> fbo);
    void present(const QImage &image, const QRegion &damage);
    QWindow *internalWindow() const;

    void changeMaximize(bool horizontal, bool vertical, bool adjust) override;
    bool has_pending_repaints() const override;

protected:
    bool acceptsFocus() const override;
    bool belongsToSameApplication(Toplevel const* other, win::same_client_check checks) const override;
    void doResizeSync() override;
    void updateCaption() override;

private:
    void createDecoration(const QRect &rect);
    void requestGeometry(const QRect &rect);
    void commitGeometry(const QRect &rect);
    void setCaption(QString const& cap);
    void markAsMapped();
    void syncGeometryToInternalWindow();
    void updateInternalWindowGeometry();

    QWindow *m_internalWindow = nullptr;
    QSize m_clientSize = QSize(0, 0);
    double m_opacity = 1.0;
    NET::WindowType m_windowType = NET::Normal;
    quint32 m_windowId = 0;
    Qt::WindowFlags m_internalWindowFlags = Qt::WindowFlags();
    bool m_userNoBorder = false;

    Q_DISABLE_COPY(InternalClient)

    friend class internal_control;
};

}
