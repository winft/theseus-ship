/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 David Edmundson <davidedmundson@kde.org>
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

#include "abstract_client.h"

#include <Wrapland/Server/xdg_shell.h>

namespace Wrapland
{
namespace Server
{
class ServerSideDecorationPalette;
class Appmenu;
class PlasmaShellSurface;
class XdgDecoration;
}
}

namespace KWin
{

/**
 * @brief The reason for which the server pinged a client surface
 */
enum class PingReason {
    CloseWindow = 0,
    FocusWindow
};

class KWIN_EXPORT XdgShellClient : public AbstractClient
{
    Q_OBJECT

public:
    XdgShellClient(Wrapland::Server::XdgShellToplevel *surface);
    XdgShellClient(Wrapland::Server::XdgShellPopup *surface);
    ~XdgShellClient() override;

    QRect inputGeometry() const override;
    QRect bufferGeometry() const override;
    QStringList activities() const override;
    QPoint clientContentPos() const override;
    QSize clientSize() const override;
    QRect transparentRect() const override;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    void debug(QDebug &stream) const override;
    double opacity() const override;
    void setOpacity(double opacity) override;
    QByteArray windowRole() const override;
    void blockActivityUpdates(bool b = true) override;
    QString captionNormal() const override;
    QString captionSuffix() const override;
    void closeWindow() override;
    AbstractClient *findModal(bool allow_itself = false) override;
    bool isCloseable() const override;
    bool isFullScreenable() const override;
    bool isFullScreen() const override;
    bool isMaximizable() const override;
    bool isMinimizable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isResizable() const override;
    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override;
    void hideClient(bool hide) override;
    win::maximize_mode maximizeMode() const override;
    win::maximize_mode requestedMaximizeMode() const override;
    QRect geometryRestore() const override;
    bool noBorder() const override;
    void setFullScreen(bool set, bool user = true) override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void setOnAllActivities(bool set) override;
    void takeFocus() override;
    bool userCanSetFullScreen() const override;
    bool userCanSetNoBorder() const override;
    bool wantsInput() const override;
    bool dockWantsInput() const override;
    using AbstractClient::resizeWithChecks;
    void resizeWithChecks(int w, int h, win::force_geometry force = win::force_geometry::no) override;
    using AbstractClient::setFrameGeometry;
    void setFrameGeometry(int x, int y, int w, int h, win::force_geometry force = win::force_geometry::no) override;
    bool hasStrut() const override;
    quint32 windowId() const override;
    pid_t pid() const override;
    bool isLockScreen() const override;
    bool isInputMethod() const override;
    bool isInitialPositionSet() const override;
    bool isTransient() const override;
    bool hasTransientPlacementHint() const override;
    QRect transientPlacement(const QRect &bounds) const override;
    QMatrix4x4 inputTransformation() const override;
    void showOnScreenEdge() override;
    bool hasPopupGrab() const override;
    void popupDone() override;
    void updateColorScheme() override;
    bool is_popup_end() const override;
    void killWindow() override;
    bool isLocalhost() const override;
    bool supportsWindowRules() const override;

    void installPlasmaShellSurface(Wrapland::Server::PlasmaShellSurface *surface);
    void installAppMenu(Wrapland::Server::Appmenu *appmenu);
    void installPalette(Wrapland::Server::ServerSideDecorationPalette *palette);
    void installXdgDecoration(Wrapland::Server::XdgDecoration *decoration);

    void placeIn(const QRect &area);

    void setGeometryRestore(const QRect &geo) override;
    void changeMaximize(bool horizontal, bool vertical, bool adjust) override;
    void doResizeSync() override;
    bool belongsToSameApplication(const AbstractClient *other, win::same_client_check checks) const override;
    bool belongsToDesktop() const override;

    void doSetActive() override;

protected:
    void addDamage(const QRegion &damage) override;
    win::layer layerForDock() const override;
    bool acceptsFocus() const override;
    void doMinimize() override;
    void updateCaption() override;
    void doMove(int x, int y) override;

private Q_SLOTS:
    void handleConfigureAcknowledged(quint32 serial);
    void handleTransientForChanged();
    void handleWindowClassChanged();
    void handleWindowGeometryChanged(const QRect &windowGeometry);
    void handleWindowTitleChanged();
    void handleMoveRequested(Wrapland::Server::Seat *seat, quint32 serial);
    void handleResizeRequested(Wrapland::Server::Seat *seat, quint32 serial, Qt::Edges edges);
    void handleMinimizeRequested();
    void handleMaximizeRequested(bool maximized);
    void handleFullScreenRequested(bool fullScreen, Wrapland::Server::Output *output);
    void handleWindowMenuRequested(Wrapland::Server::Seat *seat, quint32 serial, const QPoint &surfacePos);
    void handleGrabRequested(Wrapland::Server::Seat *seat, quint32 serial);
    void handlePingDelayed(quint32 serial);
    void handlePingTimeout(quint32 serial);
    void handlePongReceived(quint32 serial);
    void handleCommitted();

private:
    /**
     *  Called when the shell is created.
     */
    void init();
    /**
     * Called for the XDG case when the shell surface is committed to the surface.
     * At this point all initial properties should have been set by the client.
     */
    void finishInit();
    void createDecoration(const QRect &oldgeom);
    void destroyClient();
    void createWindowId();
    void updateIcon();
    bool shouldExposeToWindowManagement();
    Wrapland::Server::XdgShellSurface::States xdgSurfaceStates() const;
    void updateShowOnScreenEdge();
    void updateMaximizeMode(win::maximize_mode maximizeMode);
    // called on surface commit and processes all m_pendingConfigureRequests up to m_lastAckedConfigureReqest
    void updatePendingGeometry();
    QPoint popupOffset(const QRect &anchorRect, const Qt::Edges anchorEdge, const Qt::Edges gravity, const QSize popupSize) const;
    void requestGeometry(const QRect &rect);
    void doSetGeometry(const QRect &rect);
    void unmap();
    void markAsMapped();
    QRect determineBufferGeometry() const;
    void ping(PingReason reason);
    static void deleteClient(XdgShellClient *c);

    QRect adjustMoveGeometry(const QRect &rect) const;
    QRect adjustResizeGeometry(const QRect &rect) const;

    Wrapland::Server::XdgShellToplevel *m_xdgShellToplevel;
    Wrapland::Server::XdgShellPopup *m_xdgShellPopup;

    QRect m_bufferGeometry;
    QRect m_windowGeometry;
    bool m_hasWindowGeometry = false;

    // last size we requested or empty if we haven't sent an explicit request to the client
    // if empty the client should choose their own default size
    QSize m_requestedClientSize = QSize(0, 0);

    struct PendingConfigureRequest {
        //note for wl_shell we have no serial, so serialId and m_lastAckedConfigureRequest will always be 0
        //meaning we treat a surface commit as having processed all requests
        quint32 serialId = 0;
        // position to apply after a resize operation has been completed
        QPoint positionAfterResize;
        win::maximize_mode maximizeMode;
    };
    QVector<PendingConfigureRequest> m_pendingConfigureRequests;
    quint32 m_lastAckedConfigureRequest = 0;

    //mode in use by the current buffer
    win::maximize_mode m_maximizeMode = win::maximize_mode::restore;
    //mode we currently want to be, could be pending on client updating, could be not sent yet
    win::maximize_mode m_requestedMaximizeMode = win::maximize_mode::restore;

    QRect m_geomFsRestore; //size and position of the window before it was set to fullscreen
    bool m_closing = false;
    quint32 m_windowId = 0;
    bool m_unmapped = true;
    QRect m_geomMaximizeRestore; // size and position of the window before it was set to maximize
    NET::WindowType m_windowType = NET::Normal;
    QPointer<Wrapland::Server::PlasmaShellSurface> m_plasmaShellSurface;
    QPointer<Wrapland::Server::Appmenu> m_appmenu;
    QPointer<Wrapland::Server::ServerSideDecorationPalette> m_paletteInterface;
    Wrapland::Server::XdgDecoration *m_xdgDecoration = nullptr;
    bool m_userNoBorder = false;
    bool m_fullScreen = false;
    bool m_transient = false;
    bool m_hidden = false;
    bool m_hasPopupGrab = false;
    qreal m_opacity = 1.0;

    class RequestGeometryBlocker { //TODO rename ConfigureBlocker when this class is Xdg only
    public:
        RequestGeometryBlocker(XdgShellClient *client)
            : m_client(client)
        {
            m_client->m_requestGeometryBlockCounter++;
        }
        ~RequestGeometryBlocker()
        {
            m_client->m_requestGeometryBlockCounter--;
            if (m_client->m_requestGeometryBlockCounter == 0) {
                m_client->requestGeometry(m_client->m_blockedRequestGeometry);
            }
        }
    private:
        XdgShellClient *m_client;
    };
    friend class RequestGeometryBlocker;
    int m_requestGeometryBlockCounter = 0;
    QRect m_blockedRequestGeometry;
    QString m_caption;
    QString m_captionSuffix;
    QHash<quint32, PingReason> m_pingSerials;

    bool m_isInitialized = false;

    friend class Workspace;
};

}

Q_DECLARE_METATYPE(KWin::XdgShellClient *)
