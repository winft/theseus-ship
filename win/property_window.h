/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor.h"
#include "kwin_export.h"

#include <QObject>

#include <NETWM>
#include <xcb/xcb.h>

namespace Wrapland::Server
{
class Surface;
}

namespace KWin::win
{

class window_qobject;

/**
 * Provides a common interface for interacting with windows via Qt's property system.
 */
class KWIN_EXPORT property_window : public QObject
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
    Q_PROPERTY(bool appletPopup READ isAppletPopup)
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

    Q_PROPERTY(bool outline READ isOutline)
    Q_PROPERTY(bool shaped READ isShape NOTIFY shapedChanged)

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
    Q_PROPERTY(property_window* transientFor READ transientFor NOTIFY transientChanged)
    Q_PROPERTY(bool modal READ isModal NOTIFY modalChanged)

    Q_PROPERTY(bool decorationHasAlpha READ decorationHasAlpha)
    Q_PROPERTY(bool noBorder READ hasNoBorder WRITE setNoBorder)
    Q_PROPERTY(QString colorScheme READ colorScheme NOTIFY colorSchemeChanged)

    Q_PROPERTY(QByteArray desktopFileName READ desktopFileName NOTIFY desktopFileNameChanged)
    Q_PROPERTY(bool hasApplicationMenu READ hasApplicationMenu NOTIFY hasApplicationMenuChanged)
    Q_PROPERTY(bool providesContextHelp READ providesContextHelp CONSTANT)

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
    property_window(window_qobject& qtwin);

    virtual xcb_window_t frameId() const = 0;
    virtual quint32 windowId() const = 0;

    virtual QByteArray resourceName() const = 0;
    virtual QByteArray resourceClass() const = 0;

    virtual QString caption() const = 0;
    virtual QIcon icon() const = 0;
    virtual QRect iconGeometry() const = 0;

    virtual QUuid internalId() const = 0;
    virtual pid_t pid() const = 0;

    virtual QRect bufferGeometry() const = 0;
    virtual QRect frameGeometry() const = 0;
    virtual void setFrameGeometry(QRect const& geo) = 0;

    virtual QPoint pos() const = 0;
    virtual QRect rect() const = 0;
    virtual QRect visibleRect() const = 0;

    virtual QSize size() const = 0;
    virtual QSize minSize() const = 0;
    virtual QSize maxSize() const = 0;

    virtual QPoint clientPos() const = 0;
    virtual QSize clientSize() const = 0;

    virtual int x() const = 0;
    virtual int y() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;

    virtual bool isMove() const = 0;
    virtual bool isResize() const = 0;

    virtual bool hasAlpha() const = 0;
    virtual qreal opacity() const = 0;
    virtual void setOpacity(qreal opacity) = 0;

    virtual bool isFullScreen() const = 0;
    virtual void setFullScreen(bool set) = 0;

    virtual int screen() const = 0;

    virtual int desktop() const = 0;
    virtual void setDesktop(int desktop) = 0;
    virtual QVector<uint> x11DesktopIds() const = 0;
    virtual bool isOnAllDesktops() const = 0;
    virtual void setOnAllDesktops(bool set) = 0;

    virtual QByteArray windowRole() const = 0;
    virtual NET::WindowType windowType(bool direct = false, int supported_types = 0) const = 0;

    virtual bool isDesktop() const = 0;
    virtual bool isDock() const = 0;
    virtual bool isToolbar() const = 0;
    virtual bool isMenu() const = 0;
    virtual bool isNormalWindow() const = 0;
    virtual bool isDialog() const = 0;
    virtual bool isSplash() const = 0;
    virtual bool isUtility() const = 0;
    virtual bool isDropdownMenu() const = 0;
    virtual bool isPopupMenu() const = 0;
    virtual bool isTooltip() const = 0;
    virtual bool isNotification() const = 0;
    virtual bool isCriticalNotification() const = 0;
    virtual bool isAppletPopup() const = 0;
    virtual bool isOnScreenDisplay() const = 0;
    virtual bool isComboBox() const = 0;
    virtual bool isDNDIcon() const = 0;
    virtual bool isPopupWindow() const = 0;
    virtual bool isSpecialWindow() const = 0;

    virtual bool isCloseable() const = 0;
    virtual bool isMovable() const = 0;
    virtual bool isMovableAcrossScreens() const = 0;
    virtual bool isResizable() const = 0;
    virtual bool isMinimizable() const = 0;
    virtual bool isMaximizable() const = 0;
    virtual bool isFullScreenable() const = 0;

    virtual bool isOutline() const = 0;
    virtual bool isShape() const = 0;

    virtual bool keepAbove() const = 0;
    virtual void setKeepAbove(bool set) = 0;
    virtual bool keepBelow() const = 0;
    virtual void setKeepBelow(bool set) = 0;
    virtual bool isMinimized() const = 0;
    virtual void setMinimized(bool set) = 0;

    virtual bool skipTaskbar() const = 0;
    virtual void setSkipTaskbar(bool set) = 0;
    virtual bool skipPager() const = 0;
    virtual void setSkipPager(bool set) = 0;
    virtual bool skipSwitcher() const = 0;
    virtual void setSkipSwitcher(bool set) = 0;
    virtual bool skipsCloseAnimation() const = 0;
    virtual void setSkipCloseAnimation(bool set) = 0;

    virtual bool isActive() const = 0;
    virtual bool isDemandingAttention() const = 0;
    virtual void demandAttention(bool set) = 0;
    virtual bool wantsInput() const = 0;
    virtual bool applicationMenuActive() const = 0;
    virtual bool unresponsive() const = 0;

    virtual bool isTransient() const = 0;
    virtual property_window* transientFor() const = 0;
    virtual bool isModal() const = 0;

    virtual bool decorationHasAlpha() const = 0;
    virtual bool hasNoBorder() const = 0;
    virtual void setNoBorder(bool set) = 0;
    virtual QString colorScheme() const = 0;

    virtual QByteArray desktopFileName() const = 0;
    virtual bool hasApplicationMenu() const = 0;
    virtual bool providesContextHelp() const = 0;

    virtual bool isDeleted() const = 0;
    virtual quint32 surfaceId() const = 0;
    virtual Wrapland::Server::Surface* surface() const = 0;

    virtual QSize basicUnit() const = 0;
    virtual bool isBlockingCompositing() = 0;
    virtual void setBlockingCompositing(bool block) = 0;

    window_qobject* get_window_qobject();

Q_SIGNALS:
    void windowClassChanged();
    void captionChanged();
    void iconChanged();

    void geometryChanged();

    void moveResizedChanged();

    void hasAlphaChanged();
    void opacityChanged();
    void fullScreenChanged();

    void screenChanged();
    void desktopChanged();
    void x11DesktopIdsChanged();
    void windowRoleChanged();

    void shapedChanged();

    void keepAboveChanged();
    void keepBelowChanged();
    void minimizedChanged();

    void skipTaskbarChanged();
    void skipPagerChanged();
    void skipSwitcherChanged();
    void skipCloseAnimationChanged();

    void activeChanged();
    void demandsAttentionChanged();
    void applicationMenuActiveChanged();
    void unresponsiveChanged(bool);
    void transientChanged();
    void modalChanged();

    void colorSchemeChanged();
    void desktopFileNameChanged();
    void hasApplicationMenuChanged();
    void surfaceIdChanged(quint32);

    void blockingCompositingChanged();

private:
    void setup_connections();

    window_qobject& qtwin;
};

}
