/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor.h"

#include <QObject>

#include <NETWM>
#include <xcb/xcb.h>

namespace Wrapland
{
namespace Server
{
class Surface;
}
}

namespace KWin
{
class Toplevel;

namespace scripting
{
class space;

class window : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qulonglong frameId READ frameId)
    Q_PROPERTY(qulonglong windowId READ windowId CONSTANT)

    Q_PROPERTY(QByteArray resourceName READ resourceName NOTIFY windowClassChanged)
    Q_PROPERTY(QByteArray resourceClass READ resourceClass NOTIFY windowClassChanged)

    Q_PROPERTY(QString caption READ caption NOTIFY captionChanged)
    Q_PROPERTY(QIcon icon READ icon NOTIFY iconChanged)
    Q_PROPERTY(QRect iconGeometry READ iconGeometry)

    Q_PROPERTY(QUuid internalId READ internalId CONSTANT)
    Q_PROPERTY(int pid READ pid CONSTANT)

    Q_PROPERTY(QRect bufferGeometry READ bufferGeometry NOTIFY geometryChanged)
    Q_PROPERTY(QRect frameGeometry READ frameGeometry WRITE setFrameGeometry NOTIFY geometryChanged)

    /// @deprecated. Use frameGeometry instead.
    Q_PROPERTY(QRect geometry READ frameGeometry WRITE setFrameGeometry NOTIFY geometryChanged)

    Q_PROPERTY(QPoint pos READ pos)
    Q_PROPERTY(QRect rect READ rect)
    Q_PROPERTY(QRect visibleRect READ visibleRect)

    Q_PROPERTY(QSize size READ size)
    Q_PROPERTY(QSize minSize READ minSize)
    Q_PROPERTY(QSize maxSize READ maxSize)

    Q_PROPERTY(QPoint clientPos READ clientPos)
    Q_PROPERTY(QSize clientSize READ clientSize)

    Q_PROPERTY(int x READ x)
    Q_PROPERTY(int y READ y)
    Q_PROPERTY(int width READ width)
    Q_PROPERTY(int height READ height)

    Q_PROPERTY(bool move READ isMove NOTIFY moveResizedChanged)
    Q_PROPERTY(bool resize READ isResize NOTIFY moveResizedChanged)

    Q_PROPERTY(bool alpha READ hasAlpha NOTIFY hasAlphaChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

    Q_PROPERTY(bool fullScreen READ isFullScreen WRITE setFullScreen NOTIFY fullScreenChanged)

    Q_PROPERTY(int screen READ screen NOTIFY screenChanged)

    Q_PROPERTY(int desktop READ desktop WRITE setDesktop NOTIFY desktopChanged)
    Q_PROPERTY(QVector<uint> x11DesktopIds READ x11DesktopIds NOTIFY x11DesktopIdsChanged)
    Q_PROPERTY(bool onAllDesktops READ isOnAllDesktops WRITE setOnAllDesktops NOTIFY desktopChanged)

    Q_PROPERTY(QStringList activities READ activities NOTIFY activitiesChanged)

    Q_PROPERTY(QByteArray windowRole READ windowRole NOTIFY windowRoleChanged)
    Q_PROPERTY(int windowType READ windowType)

    Q_PROPERTY(bool desktopWindow READ isDesktop)
    Q_PROPERTY(bool dock READ isDock)
    Q_PROPERTY(bool toolbar READ isToolbar)
    Q_PROPERTY(bool menu READ isMenu)
    Q_PROPERTY(bool normalWindow READ isNormalWindow)
    Q_PROPERTY(bool dialog READ isDialog)
    Q_PROPERTY(bool splash READ isSplash)
    Q_PROPERTY(bool utility READ isUtility)
    Q_PROPERTY(bool dropdownMenu READ isDropdownMenu)
    Q_PROPERTY(bool popupMenu READ isPopupMenu)
    Q_PROPERTY(bool tooltip READ isTooltip)
    Q_PROPERTY(bool notification READ isNotification)
    Q_PROPERTY(bool criticalNotification READ isCriticalNotification)
    Q_PROPERTY(bool onScreenDisplay READ isOnScreenDisplay)
    Q_PROPERTY(bool comboBox READ isComboBox)
    Q_PROPERTY(bool dndIcon READ isDNDIcon)
    Q_PROPERTY(bool popupWindow READ isPopupWindow)
    Q_PROPERTY(bool specialWindow READ isSpecialWindow)

    Q_PROPERTY(bool closeable READ isCloseable)
    Q_PROPERTY(bool moveable READ isMovable)
    Q_PROPERTY(bool moveableAcrossScreens READ isMovableAcrossScreens)
    Q_PROPERTY(bool resizeable READ isResizable)
    Q_PROPERTY(bool minimizable READ isMinimizable)
    Q_PROPERTY(bool maximizable READ isMaximizable)
    Q_PROPERTY(bool fullScreenable READ isFullScreenable)
    Q_PROPERTY(bool shadeable READ isShadeable)

    Q_PROPERTY(bool outline READ isOutline)
    Q_PROPERTY(bool shaped READ isShape NOTIFY shapedChanged)
    Q_PROPERTY(bool shade READ isShade WRITE setShade NOTIFY shadeChanged)

    Q_PROPERTY(bool keepAbove READ keepAbove WRITE setKeepAbove NOTIFY keepAboveChanged)
    Q_PROPERTY(bool keepBelow READ keepBelow WRITE setKeepBelow NOTIFY keepBelowChanged)
    Q_PROPERTY(bool minimized READ isMinimized WRITE setMinimized NOTIFY minimizedChanged)

    Q_PROPERTY(bool skipTaskbar READ skipTaskbar WRITE setSkipTaskbar NOTIFY skipTaskbarChanged)
    Q_PROPERTY(bool skipPager READ skipPager WRITE setSkipPager NOTIFY skipPagerChanged)
    Q_PROPERTY(bool skipSwitcher READ skipSwitcher WRITE setSkipSwitcher NOTIFY skipSwitcherChanged)
    Q_PROPERTY(bool skipsCloseAnimation READ skipsCloseAnimation WRITE setSkipCloseAnimation NOTIFY
                   skipCloseAnimationChanged)

    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool demandsAttention READ isDemandingAttention WRITE demandAttention NOTIFY
                   demandsAttentionChanged)
    Q_PROPERTY(bool wantsInput READ wantsInput)
    Q_PROPERTY(
        bool applicationMenuActive READ applicationMenuActive NOTIFY applicationMenuActiveChanged)
    Q_PROPERTY(bool unresponsive READ unresponsive NOTIFY unresponsiveChanged)

    Q_PROPERTY(bool transient READ isTransient NOTIFY transientChanged)
    Q_PROPERTY(window* transientFor READ transientFor NOTIFY transientChanged)
    Q_PROPERTY(bool modal READ isModal NOTIFY modalChanged)

    Q_PROPERTY(bool decorationHasAlpha READ decorationHasAlpha)
    Q_PROPERTY(bool noBorder READ hasNoBorder WRITE setNoBorder)
    Q_PROPERTY(QString colorScheme READ colorScheme NOTIFY colorSchemeChanged)

    Q_PROPERTY(QByteArray desktopFileName READ desktopFileName NOTIFY desktopFileNameChanged)
    Q_PROPERTY(bool hasApplicationMenu READ hasApplicationMenu NOTIFY hasApplicationMenuChanged)
    Q_PROPERTY(bool providesContextHelp READ providesContextHelp CONSTANT)

    // TODO: Should this not also hold true for Wayland windows? The name is misleading.
    //       Wayland windows (with xdg-toplevel role) are also "managed" by the compositor.
    Q_PROPERTY(bool managed READ isClient CONSTANT)
    Q_PROPERTY(quint32 surfaceId READ surfaceId NOTIFY surfaceIdChanged)
    Q_PROPERTY(Wrapland::Server::Surface* surface READ surface)
    Q_PROPERTY(bool deleted READ isDeleted CONSTANT)

    /**
     * X11 only properties
     */
    Q_PROPERTY(QSize basicUnit READ basicUnit)
    Q_PROPERTY(bool blocksCompositing READ isBlockingCompositing WRITE setBlockingCompositing NOTIFY
                   blockingCompositingChanged)

public:
    window(Toplevel* client, space* workspace);

    xcb_window_t frameId() const;
    quint32 windowId() const;

    QByteArray resourceName() const;
    QByteArray resourceClass() const;

    QString caption() const;
    QIcon icon() const;
    QRect iconGeometry() const;

    QUuid internalId() const;
    pid_t pid() const;

    QRect bufferGeometry() const;
    QRect frameGeometry() const;
    void setFrameGeometry(QRect const& geo);

    QPoint pos() const;
    QRect rect() const;
    QRect visibleRect() const;

    QSize size() const;
    QSize minSize() const;
    QSize maxSize() const;

    QPoint clientPos() const;
    QSize clientSize() const;

    int x() const;
    int y() const;
    int width() const;
    int height() const;

    bool isMove() const;
    bool isResize() const;

    bool hasAlpha() const;
    qreal opacity() const;
    void setOpacity(qreal opacity);

    bool isFullScreen() const;
    void setFullScreen(bool set);

    int screen() const;

    int desktop() const;
    void setDesktop(int desktop);
    QVector<uint> x11DesktopIds() const;
    bool isOnAllDesktops() const;
    void setOnAllDesktops(bool set);

    QStringList activities() const;

    QByteArray windowRole() const;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const;

    bool isDesktop() const;
    bool isDock() const;
    bool isToolbar() const;
    bool isMenu() const;
    bool isNormalWindow() const;
    bool isDialog() const;
    bool isSplash() const;
    bool isUtility() const;
    bool isDropdownMenu() const;
    bool isPopupMenu() const;
    bool isTooltip() const;
    bool isNotification() const;
    bool isCriticalNotification() const;
    bool isOnScreenDisplay() const;
    bool isComboBox() const;
    bool isDNDIcon() const;
    bool isPopupWindow() const;
    bool isSpecialWindow() const;

    bool isCloseable() const;
    bool isMovable() const;
    bool isMovableAcrossScreens() const;
    bool isResizable() const;
    bool isMinimizable() const;
    bool isMaximizable() const;
    bool isFullScreenable() const;
    bool isShadeable() const;

    bool isOutline() const;
    bool isShape() const;
    bool isShade() const;
    void setShade(bool set);

    bool keepAbove() const;
    void setKeepAbove(bool set);
    bool keepBelow() const;
    void setKeepBelow(bool set);
    bool isMinimized() const;
    void setMinimized(bool set);

    bool skipTaskbar() const;
    void setSkipTaskbar(bool set);
    bool skipPager() const;
    void setSkipPager(bool set);
    bool skipSwitcher() const;
    void setSkipSwitcher(bool set);
    bool skipsCloseAnimation() const;
    void setSkipCloseAnimation(bool set);

    bool isActive() const;
    bool isDemandingAttention() const;
    void demandAttention(bool set);
    bool wantsInput() const;
    bool applicationMenuActive() const;
    bool unresponsive() const;

    bool isTransient() const;
    window* transientFor() const;
    bool isModal() const;

    bool decorationHasAlpha() const;
    bool hasNoBorder() const;
    void setNoBorder(bool set);
    QString colorScheme() const;

    QByteArray desktopFileName() const;
    bool hasApplicationMenu() const;
    bool providesContextHelp() const;

    bool isClient() const;
    bool isDeleted() const;
    quint32 surfaceId() const;
    Wrapland::Server::Surface* surface() const;

    QSize basicUnit() const;
    bool isBlockingCompositing();
    void setBlockingCompositing(bool block);

    Toplevel* client() const;

Q_SIGNALS:
    void windowClassChanged();
    void captionChanged();
    void iconChanged();

    void geometryChanged();
    void quickTileModeChanged();

    void moveResizedChanged();
    void moveResizeCursorChanged(input::cursor_shape);
    void clientStartUserMovedResized(KWin::scripting::window* window);
    void clientStepUserMovedResized(KWin::scripting::window* window, const QRect&);
    void clientFinishUserMovedResized(KWin::scripting::window* window);

    void closeableChanged(bool);
    void minimizeableChanged(bool);
    void shadeableChanged(bool);
    void maximizeableChanged(bool);

    void hasAlphaChanged();
    void opacityChanged(KWin::scripting::window* client, qreal old_opacity);
    void fullScreenChanged();

    void screenChanged();
    void desktopChanged();
    void x11DesktopIdsChanged();
    void activitiesChanged(KWin::scripting::window* client);
    void windowRoleChanged();

    void shapedChanged();
    void shadeChanged();

    void keepAboveChanged();
    void keepBelowChanged();
    void minimizedChanged();

    void skipTaskbarChanged();
    void skipPagerChanged();
    void skipSwitcherChanged();
    void skipCloseAnimationChanged();

    void activeChanged();
    void desktopPresenceChanged(KWin::scripting::window* window, int);
    void demandsAttentionChanged();
    void applicationMenuActiveChanged();
    void unresponsiveChanged(bool);
    void transientChanged();
    void modalChanged();

    void paletteChanged(const QPalette& p);
    void colorSchemeChanged();
    void desktopFileNameChanged();
    void hasApplicationMenuChanged();
    void surfaceIdChanged(quint32);

    void blockingCompositingChanged(KWin::scripting::window* window);

    void clientMinimized(KWin::scripting::window* window);
    void clientUnminimized(KWin::scripting::window* window);

    void
    clientMaximizedStateChanged(KWin::scripting::window* window, bool horizontal, bool vertical);

    /// Deprecated
    void clientManaging(KWin::scripting::window* window);

    /// Deprecated
    void clientFullScreenSet(KWin::scripting::window* window, bool fullscreen, bool user);

    // TODO: this signal is never emitted - remove?
    void clientMaximizeSet(KWin::scripting::window* window, bool horizontal, bool vertical);

private:
    Toplevel* m_client;
    space* m_workspace;
};

}
}

Q_DECLARE_METATYPE(KWin::scripting::window*)
Q_DECLARE_METATYPE(QList<KWin::scripting::window*>)
