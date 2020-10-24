/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
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
#ifndef KWIN_ABSTRACT_CLIENT_H
#define KWIN_ABSTRACT_CLIENT_H

#include "toplevel.h"
#include "options.h"
#include "rules/rules.h"
#include "cursor.h"

#include <memory>

#include <QElapsedTimer>
#include <QPointer>

namespace Wrapland
{
namespace Server
{
class PlasmaWindow;
}
}

namespace KDecoration2
{
class Decoration;
}

namespace KWin
{
class Group;

namespace Decoration
{
class DecoratedClientImpl;
class DecorationPalette;
}

namespace win
{
enum class force_geometry;
enum class maximize_mode;
enum class position;
enum class size_mode;
}

class KWIN_EXPORT AbstractClient : public Toplevel
{
    Q_OBJECT

public:
    ~AbstractClient() override;

    QMargins frameMargins() const override;
    QPoint clientPos() const override;

    /**
     * @returns The caption as set by the AbstractClient without any suffix.
     * @see caption
     * @see captionSuffix
     */
    virtual QString captionNormal() const = 0;
    /**
     * @returns The suffix added to the caption (e.g. shortcut, machine name, etc.)
     * @see caption
     * @see captionNormal
     */
    virtual QString captionSuffix() const = 0;
    virtual bool isCloseable() const = 0;
    // TODO: remove boolean trap
    virtual bool isShown(bool shaded_is_shown) const = 0;
    virtual bool isHiddenInternal() const = 0;
    // TODO: remove boolean trap
    virtual void hideClient(bool hide) = 0;
    virtual bool isFullScreenable() const = 0;
    virtual bool isFullScreen() const = 0;
    // TODO: remove boolean trap
    virtual AbstractClient *findModal(bool allow_itself = false) = 0;
    virtual bool isTransient() const;
    /**
     * @returns Whether there is a hint available to place the AbstractClient on it's parent, default @c false.
     * @see transientPlacementHint
     */
    virtual bool hasTransientPlacementHint() const;
    /**
     * Only valid id hasTransientPlacementHint is true
     * @returns The position the transient wishes to position itself
     */
    virtual QRect transientPlacement(const QRect &bounds) const;
    const AbstractClient* transientFor() const;
    AbstractClient* transientFor();
    /**
     * @returns @c true if c is the transient_for window for this client,
     *  or recursively the transient_for window
     * @todo: remove boolean trap
     */
    virtual bool hasTransient(const AbstractClient* c, bool indirect) const;
    const QList<AbstractClient*>& transients() const; // Is not indirect
    virtual void removeTransient(AbstractClient* cl);
    virtual QList<AbstractClient*> mainClients() const; // Call once before loop , is not indirect

    const QKeySequence &shortcut() const {
        return _shortcut;
    }
    void setShortcut(const QString &cut);
    virtual bool performMouseCommand(Options::MouseCommand, const QPoint &globalPos);

    /**
     * Set the window as being on the attached list of desktops
     * On X11 it will be set to the last entry
     */
    void setDesktops(QVector<VirtualDesktop *> desktops);

    int desktop() const override {
        return m_desktops.isEmpty() ? (int)NET::OnAllDesktops : m_desktops.last()->x11DesktopNumber();
    }
    QVector<VirtualDesktop *> desktops() const override {
        return m_desktops;
    }
    QVector<uint> x11DesktopIds() const;

    virtual void setFullScreen(bool set, bool user = true) = 0;

    virtual void setClientShown(bool shown);

    virtual QRect geometryRestore() const = 0;

    /**
     * The currently applied maximize mode
     */
    virtual win::maximize_mode maximizeMode() const = 0;

    /**
     * The maximise mode requested by the server.
     * For X this always matches maximizeMode, for wayland clients it
     * is asynchronous
     */
    virtual win::maximize_mode requestedMaximizeMode() const;

    virtual bool noBorder() const = 0;
    virtual void setNoBorder(bool set) = 0;
    virtual void blockActivityUpdates(bool b = true) = 0;
    QPalette palette() const;
    const Decoration::DecorationPalette *decorationPalette() const;
    /**
     * Returns whether the window is resizable or has a fixed size.
     */
    virtual bool isResizable() const = 0;
    /**
     * Returns whether the window is moveable or has a fixed position.
     */
    virtual bool isMovable() const = 0;
    /**
     * Returns whether the window can be moved to another screen.
     */
    virtual bool isMovableAcrossScreens() const = 0;
    /**
     * @c true only for @c ShadeNormal
     */
    bool isShade() const {
        return shadeMode() == ShadeNormal;
    }
    /**
     * Default implementation returns @c ShadeNone
     */
    virtual ShadeMode shadeMode() const; // Prefer isShade()
    /**
     * Default implementation does nothing
     */
    virtual void setShade(ShadeMode mode);
    /**
     * Whether the Client can be shaded. Default implementation returns @c false.
     */
    virtual bool isShadeable() const;
    /**
     * Returns whether the window is maximizable or not.
     */
    virtual bool isMaximizable() const = 0;
    virtual bool isMinimizable() const = 0;
    virtual QRect iconGeometry() const;
    virtual bool userCanSetFullScreen() const = 0;
    virtual bool userCanSetNoBorder() const = 0;
    virtual void checkNoBorder();
    virtual void setOnActivities(QStringList newActivitiesList);
    virtual void setOnAllActivities(bool set) = 0;
    const WindowRules* rules() const {
        return &m_rules;
    }
    void removeRule(Rules* r);
    void setupWindowRules(bool ignore_temporary);
    void evaluateWindowRules();
    virtual void applyWindowRules();
    virtual void takeFocus() = 0;
    virtual bool wantsInput() const = 0;
    /**
     * Whether a dock window wants input.
     *
     * By default KWin doesn't pass focus to a dock window unless a force activate
     * request is provided.
     *
     * This method allows to have dock windows take focus also through flags set on
     * the window.
     *
     * The default implementation returns @c false.
     */
    virtual bool dockWantsInput() const;
    virtual xcb_timestamp_t userTime() const;
    virtual void updateWindowRules(Rules::Types selection);

    /**
     * Ends move resize when all pointer buttons are up again.
     */
    void endMoveResize();
    void keyPressEvent(uint key_code);

    // TODO: still needed? remove?
    win::position titlebarPosition() const;

    QuickTileMode quickTileMode() const {
        return QuickTileMode(m_quickTileMode);
    }
    void set_QuickTileMode_win(QuickTileMode mode);

    win::layer layer() const override;

    virtual void move(int x, int y, win::force_geometry force = win::force_geometry::no);
    void move(const QPoint &p, win::force_geometry force = win::force_geometry::no);
    virtual void resizeWithChecks(int w, int h, win::force_geometry force = win::force_geometry::no) = 0;
    void resizeWithChecks(const QSize& s, win::force_geometry force = win::force_geometry::no);
    virtual QSize minSize() const;
    virtual QSize maxSize() const;

    virtual void setFrameGeometry(int x, int y, int w, int h, win::force_geometry force = win::force_geometry::no) = 0;
    void setFrameGeometry(const QRect &rect, win::force_geometry force = win::force_geometry::no);

    /**
     * Calculates the appropriate frame size for the given client size @p wsize.
     *
     * @p wsize is adapted according to the window's size hints (minimum, maximum and incremental size changes).
     *
     * Default implementation returns the passed in @p wsize.
     */
    virtual QSize sizeForClientSize(const QSize &wsize,
                                    win::size_mode mode = win::size_mode::any,
                                    bool noframe = false) const;

    /**
     * Calculates the matching client position for the given frame position @p point.
     */
    virtual QPoint framePosToClientPos(const QPoint &point) const;
    /**
     * Calculates the matching frame position for the given client position @p point.
     */
    virtual QPoint clientPosToFramePos(const QPoint &point) const;
    /**
     * Calculates the matching client size for the given frame size @p size.
     *
     * Notice that size constraints won't be applied.
     *
     * Default implementation returns the frame size with frame margins being excluded.
     */
    virtual QSize frameSizeToClientSize(const QSize &size) const;
    /**
     * Calculates the matching frame size for the given client size @p size.
     *
     * Notice that size constraints won't be applied.
     *
     * Default implementation returns the client size with frame margins being included.
     */
    virtual QSize clientSizeToFrameSize(const QSize &size) const;

    /**
     * Cursor shape for move/resize mode.
     */
    CursorShape cursor() const {
        return m_moveResize.cursor;
    }

    virtual bool hasStrut() const;

    void setModal(bool modal);
    bool isModal() const;

    // decoration related
    KDecoration2::Decoration *decoration() {
        return m_decoration.decoration;
    }
    const KDecoration2::Decoration *decoration() const {
        return m_decoration.decoration;
    }
    bool isDecorated() const {
        return m_decoration.decoration != nullptr;
    }
    QPointer<Decoration::DecoratedClientImpl> decoratedClient() const;
    void setDecoratedClient(QPointer<Decoration::DecoratedClientImpl> client);
    virtual void layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const;
    bool processDecorationButtonPress(QMouseEvent *event, bool ignoreMenu = false);

    /**
     * TODO: fix boolean traps
     */
    virtual void updateDecoration(bool check_workspace_pos, bool force = false) = 0;

    /**
     * Returns whether the window provides context help or not. If it does,
     * you should show a help menu item or a help button like '?' and call
     * contextHelp() if this is invoked.
     *
     * Default implementation returns @c false.
     * @see showContextHelp;
     */
    virtual bool providesContextHelp() const;

    /**
     * Invokes context help on the window. Only works if the window
     * actually provides context help.
     *
     * Default implementation does nothing.
     *
     * @see providesContextHelp()
     */
    virtual void showContextHelp();

    QRect inputGeometry() const override;

    /**
     * Restores the AbstractClient after it had been hidden due to show on screen edge functionality.
     * The AbstractClient also gets raised (e.g. Panel mode windows can cover) and the AbstractClient
     * gets informed in a window specific way that it is shown and raised again.
     */
    virtual void showOnScreenEdge() = 0;

    QByteArray desktopFileName() const {
        return m_desktopFileName;
    }

    /**
     * Tries to terminate the process of this AbstractClient.
     *
     * Implementing subclasses can perform a windowing system solution for terminating.
     */
    virtual void killWindow() = 0;

    bool hasApplicationMenu() const;
    bool applicationMenuActive() const {
        return m_applicationMenuActive;
    }
    void setApplicationMenuActive(bool applicationMenuActive);

    QString applicationMenuServiceName() const {
        return m_applicationMenuServiceName;
    }
    QString applicationMenuObjectPath() const {
        return m_applicationMenuObjectPath;
    }
    QString colorScheme() const {
        return m_colorScheme;
    }

    bool unresponsive() const;

    virtual bool isInitialPositionSet() const {
        return false;
    }

    /**
     * Default implementation returns @c null.
     * Mostly intended for X11 clients, from EWMH:
     * @verbatim
     * If the WM_TRANSIENT_FOR property is set to None or Root window, the window should be
     * treated as a transient for all other windows in the same group. It has been noted that this
     * is a slight ICCCM violation, but as this behavior is pretty standard for many toolkits and
     * window managers, and is extremely unlikely to break anything, it seems reasonable to document
     * it as standard.
     * @endverbatim
     */
    virtual bool groupTransient() const;
    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual const Group *group() const;
    /**
     * Default implementation returns @c null.
     *
     * Mostly for X11 clients, holds the client group
     */
    virtual Group *group();

    /**
     * Returns whether this is an internal client.
     *
     * Internal clients are created by KWin and used for special purpose windows,
     * like the task switcher, etc.
     *
     * Default implementation returns @c false.
     */
    virtual bool isInternal() const;

    /**
     * Returns whether window rules can be applied to this client.
     *
     * Default implementation returns @c true.
     */
    virtual bool supportsWindowRules() const;

    virtual QSize basicUnit() const;

    virtual void setBlockingCompositing(bool block);
    virtual bool isBlockingCompositing();

    // TODOX: BELOW WAS PROTECTED!
    /**
     * Whether a sync request is still pending.
     * Default implementation returns @c false.
     */
    virtual bool isWaitingForMoveResizeSync() const;

    win::position moveResizePointerMode() const;

    /**
     * @returns whether the Client is currently in move resize mode
     */
    bool isMoveResize() const {
        return m_moveResize.enabled;
    }
    QPoint moveOffset() const {
        return m_moveResize.offset;
    }

    void setMoveResizePointerButtonDown(bool down) {
        m_moveResize.buttonDown = down;
    }
    /**
     * Sets an appropriate cursor shape for the logical mouse position.
     */
    void updateCursor();
    QPoint invertedMoveOffset() const {
        return m_moveResize.invertedOffset;
    }
    QRect moveResizeGeometry() const {
        return m_moveResize.geometry;
    }

    QRect initialMoveResizeGeometry() const {
        return m_moveResize.initialGeometry;
    }
    void setMoveResizeGeometry(const QRect &geo) {
        m_moveResize.geometry = geo;
    }

    /**
     * @returns whether the move resize mode is unrestricted.
     */
    bool isUnrestrictedMoveResize() const {
        return m_moveResize.unrestricted;
    }
    bool haveResizeEffect() {
        return m_haveResizeEffect;
    }
    /**
     * Called during handling a resize. Implementing subclasses can use this
     * method to perform windowing system specific syncing.
     *
     * Default implementation does nothing.
     */
    virtual void doResizeSync();

    void blockGeometryUpdates() {
        m_blockGeometryUpdates++;
    }
    void blockGeometryUpdates(bool block);

    virtual void setGeometryRestore(const QRect &geo) = 0;

    void setMoveOffset(const QPoint &offset) {
        m_moveResize.offset = offset;
    }

    void setMoveResizePointerMode(win::position mode);

    void setInvertedMoveOffset(const QPoint &offset) {
        m_moveResize.invertedOffset = offset;
    }

    /**
     * Sets whether move resize mode is unrestricted to @p set.
     */
    void setUnrestrictedMoveResize(bool set) {
        m_moveResize.unrestricted = set;
    }

    /**
     * Whether the window accepts focus.
     * The difference to wantsInput is that the implementation should not check rules and return
     * what the window effectively supports.
     */
    virtual bool acceptsFocus() const = 0;

    virtual void changeMaximize(bool horizontal, bool vertical, bool adjust) = 0;

    // electric border / quick tiling
    QuickTileMode electricBorderMode() const {
        return m_electricMode;
    }
    void setElectricBorderMode(QuickTileMode mode);

    /**
     * Sets whether the Client is in move resize mode to @p enabled.
     */
    void setMoveResize(bool enabled) {
        m_moveResize.enabled = enabled;
    }
    bool isElectricBorderMaximizing() const {
        return m_electricMaximizing;
    }
    void setElectricBorderMaximizing(bool maximizing);

    /**
     * Looks for another AbstractClient with same captionNormal and captionSuffix.
     * If no such AbstractClient exists @c nullptr is returned.
     */
    AbstractClient *findClientWithSameCaption() const;

    void stopDelayedMoveResize();

    /**
     * Called from win::start_move_resize.
     *
     * Implementing classes should return @c false if starting move resize should
     * get aborted. In that case win::start_move_resize will also return @c false.
     *
     * Base implementation returns @c true.
     */
    virtual bool doStartMoveResize();

    void invalidateDecorationDoubleClickTimer();

    void updateQuickTileMode(QuickTileMode newMode) {
        m_quickTileMode = newMode;
    }

    void updateHaveResizeEffect();

    /**
     * Sets the initial move resize geometry to the current geometry.
     */
    void updateInitialMoveResizeGeometry();

    /**
     * Leaves the move resize mode.
     *
     * Inheriting classes must invoke the base implementation which
     * ensures that the internal mode is properly ended.
     */
    virtual void leaveMoveResize();

    int moveResizeStartScreen() const {
        return m_moveResize.startScreen;
    }

    virtual void positionGeometryTip();

    /**
     * Called from win::perform_move_resize() after actually performing the change of geometry.
     * Implementing subclasses can perform windowing system specific handling here.
     *
     * Default implementation does nothing.
     */
    virtual void doPerformMoveResize();

    bool isMoveResizePointerButtonDown() const {
        return m_moveResize.buttonDown;
    }

    virtual win::layer layerForDock() const;
    virtual bool belongsToDesktop() const;

    virtual void destroyDecoration();
    virtual bool belongsToSameApplication(const AbstractClient *other, win::same_client_check checks) const = 0;

    Wrapland::Server::PlasmaWindow *windowManagementInterface() const {
        return m_windowManagementInterface;
    }
    void setWindowManagementInterface(Wrapland::Server::PlasmaWindow* plasma_window);

    void invalidateLayer();

    /**
     * Called from win::set_active once the active value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetActive();

    /**
     * Called from setKeepAbove once the keepBelow value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepAbove();
    /**
     * Called from setKeepBelow once the keepBelow value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     */
    virtual void doSetKeepBelow();

    /**
     * Called from @ref minimize and @ref unminimize once the minimized value got updated, but before the
     * changed signal is emitted.
     *
     * Default implementation does nothig.
     */
    virtual void doMinimize();

    virtual QSize resizeIncrements() const;

    // TODOX: ABOVE WAS PROTECTED!

    void delayed_electric_maximize();

    QRect visible_rect_before_geometry_update() const;
    void set_visible_rect_before_geometry_update(QRect const& rect);

public Q_SLOTS:
    virtual void closeWindow() = 0;

Q_SIGNALS:
    void fullScreenChanged();
    void skipTaskbarChanged();
    void skipPagerChanged();
    void skipSwitcherChanged();
    void activeChanged();
    void keepAboveChanged(bool);
    void keepBelowChanged(bool);
    /**
     * Emitted whenever the demands attention state changes.
     */
    void demandsAttentionChanged();
    void desktopPresenceChanged(KWin::AbstractClient*, int); // to be forwarded by Workspace
    void desktopChanged();
    void x11DesktopIdsChanged();
    void shadeChanged();
    void minimizedChanged();
    void clientMinimized(KWin::AbstractClient* client, bool animate);
    void clientUnminimized(KWin::AbstractClient* client, bool animate);
    void paletteChanged(const QPalette &p);
    void colorSchemeChanged();
    void captionChanged();
    void clientMaximizedStateChanged(KWin::AbstractClient*, KWin::win::maximize_mode);
    void clientMaximizedStateChanged(KWin::AbstractClient* c, bool h, bool v);
    void transientChanged();
    void modalChanged();
    void quickTileModeChanged();
    void moveResizedChanged();
    void moveResizeCursorChanged(CursorShape);
    void clientStartUserMovedResized(KWin::AbstractClient*);
    void clientStepUserMovedResized(KWin::AbstractClient *, const QRect&);
    void clientFinishUserMovedResized(KWin::AbstractClient*);
    void closeableChanged(bool);
    void minimizeableChanged(bool);
    void shadeableChanged(bool);
    void maximizeableChanged(bool);
    void desktopFileNameChanged();
    void hasApplicationMenuChanged(bool);
    void applicationMenuActiveChanged(bool);
    void unresponsiveChanged(bool);
    void blockingCompositingChanged(KWin::AbstractClient* client);

protected:
    AbstractClient();

    /**
     * Called from setDeskop once the desktop value got updated, but before the changed signal
     * is emitted.
     *
     * Default implementation does nothing.
     * @param desktop The new desktop the Client is on
     * @param was_desk The desktop the Client was on before
     */
    virtual void doSetDesktop(int desktop, int was_desk);

    void destroyWindowManagementInterface();

    void updateColorScheme(QString path);
    virtual void updateColorScheme() = 0;

    void setTransientFor(AbstractClient *transientFor);
    virtual void addTransient(AbstractClient* cl);
    /**
     * Just removes the @p cl from the transients without any further checks.
     */
    void removeTransientFromList(AbstractClient* cl);

    /**
     * Called from move after updating the geometry. Can be reimplemented to perform specific tasks.
     * The base implementation does nothing.
     */
    virtual void doMove(int x, int y);
    void unblockGeometryUpdates();
    bool areGeometryUpdatesBlocked() const;
    enum PendingGeometry_t {
        PendingGeometryNone,
        PendingGeometryNormal,
        PendingGeometryForced
    };
    PendingGeometry_t pendingGeometryUpdate() const;
    void setPendingGeometryUpdate(PendingGeometry_t update);
    QRect bufferGeometryBeforeUpdateBlocking() const;
    QRect frameGeometryBeforeUpdateBlocking() const;
    void updateGeometryBeforeUpdateBlocking();

    void startDelayedMoveResize();

    void resetHaveResizeEffect() {
        m_haveResizeEffect = false;
    }

    void setDecoration(KDecoration2::Decoration *decoration) {
        m_decoration.decoration = decoration;
    }
    void startDecorationDoubleClickTimer();

    void setDesktopFileName(QByteArray name);
    QString iconFromDesktopFile() const;

    void updateApplicationMenuServiceName(const QString &serviceName);
    void updateApplicationMenuObjectPath(const QString &objectPath);

    void setUnresponsive(bool unresponsive);

    virtual void setShortcutInternal();
    virtual void updateCaption() = 0;

    void finishWindowRules();
    void discardTemporaryRules();

    bool tabTo(AbstractClient *other, bool behind, bool activate);

private:
    void handlePaletteChange();

    QVector <VirtualDesktop *> m_desktops;

    QString m_colorScheme;
    std::shared_ptr<Decoration::DecorationPalette> m_palette;
    static QHash<QString, std::weak_ptr<Decoration::DecorationPalette>> s_palettes;
    static std::shared_ptr<Decoration::DecorationPalette> s_defaultPalette;

    Wrapland::Server::PlasmaWindow *m_windowManagementInterface = nullptr;

    AbstractClient *m_transientFor = nullptr;
    QList<AbstractClient*> m_transients;
    bool m_modal = false;
    win::layer m_layer = win::layer::unknown;

    // electric border/quick tiling
    QuickTileMode m_electricMode = QuickTileFlag::None;
    bool m_electricMaximizing = false;
    // The quick tile mode of this window.
    int m_quickTileMode = int(QuickTileFlag::None);
    QTimer *m_electricMaximizingDelay = nullptr;

    // geometry
    int m_blockGeometryUpdates = 0; // > 0 = New geometry is remembered, but not actually set
    PendingGeometry_t m_pendingGeometryUpdate = PendingGeometryNone;
    QRect m_visibleRectBeforeGeometryUpdate;
    QRect m_bufferGeometryBeforeUpdateBlocking;
    QRect m_frameGeometryBeforeUpdateBlocking;

    struct {
        bool enabled = false;
        bool unrestricted = false;
        QPoint offset;
        QPoint invertedOffset;
        QRect initialGeometry;
        QRect geometry;
        win::position pointer = win::position::center;
        bool buttonDown = false;
        CursorShape cursor = Qt::ArrowCursor;
        int startScreen = 0;
        QTimer *delayedTimer = nullptr;
    } m_moveResize;

    struct {
        KDecoration2::Decoration *decoration = nullptr;
        QPointer<Decoration::DecoratedClientImpl> client;
        QElapsedTimer doubleClickTimer;
    } m_decoration;
    QByteArray m_desktopFileName;

    bool m_applicationMenuActive = false;
    QString m_applicationMenuServiceName;
    QString m_applicationMenuObjectPath;

    bool m_unresponsive = false;

    QKeySequence _shortcut;

    WindowRules m_rules;

    bool m_haveResizeEffect{false};
};

inline void AbstractClient::move(const QPoint& p, win::force_geometry force)
{
    move(p.x(), p.y(), force);
}

inline void AbstractClient::resizeWithChecks(const QSize& s, win::force_geometry force)
{
    resizeWithChecks(s.width(), s.height(), force);
}

inline void AbstractClient::setFrameGeometry(const QRect &rect, win::force_geometry force)
{
    setFrameGeometry(rect.x(), rect.y(), rect.width(), rect.height(), force);
}

inline const QList<AbstractClient*>& AbstractClient::transients() const
{
    return m_transients;
}

inline bool AbstractClient::areGeometryUpdatesBlocked() const
{
    return m_blockGeometryUpdates != 0;
}

inline void AbstractClient::unblockGeometryUpdates()
{
    m_blockGeometryUpdates--;
}

inline AbstractClient::PendingGeometry_t AbstractClient::pendingGeometryUpdate() const
{
    return m_pendingGeometryUpdate;
}

inline void AbstractClient::setPendingGeometryUpdate(PendingGeometry_t update)
{
    m_pendingGeometryUpdate = update;
}

}

Q_DECLARE_METATYPE(KWin::AbstractClient*)
Q_DECLARE_METATYPE(QList<KWin::AbstractClient*>)

#endif
