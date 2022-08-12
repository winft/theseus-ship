/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/property_window.h"

namespace KWin
{

class Toplevel;

namespace debug
{

class KWIN_EXPORT console_window : public win::property_window
{
public:
    explicit console_window(Toplevel* ref_win);

    xcb_window_t frameId() const override;
    quint32 windowId() const override;

    QByteArray resourceName() const override;
    QByteArray resourceClass() const override;

    QString caption() const override;
    QIcon icon() const override;
    QRect iconGeometry() const override;

    QUuid internalId() const override;
    pid_t pid() const override;

    QRect bufferGeometry() const override;
    QRect frameGeometry() const override;
    void setFrameGeometry(QRect const& geo) override;

    QPoint pos() const override;
    QRect rect() const override;
    QRect visibleRect() const override;

    QSize size() const override;
    QSize minSize() const override;
    QSize maxSize() const override;

    QPoint clientPos() const override;
    QSize clientSize() const override;

    int x() const override;
    int y() const override;
    int width() const override;
    int height() const override;

    bool isMove() const override;
    bool isResize() const override;

    bool hasAlpha() const override;
    qreal opacity() const override;
    void setOpacity(qreal opacity) override;

    bool isFullScreen() const override;
    void setFullScreen(bool set) override;

    int screen() const override;

    int desktop() const override;
    void setDesktop(int desktop) override;
    QVector<uint> x11DesktopIds() const override;
    bool isOnAllDesktops() const override;
    void setOnAllDesktops(bool set) override;

    QByteArray windowRole() const override;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;

    bool isDesktop() const override;
    bool isDock() const override;
    bool isToolbar() const override;
    bool isMenu() const override;
    bool isNormalWindow() const override;
    bool isDialog() const override;
    bool isSplash() const override;
    bool isUtility() const override;
    bool isDropdownMenu() const override;
    bool isPopupMenu() const override;
    bool isTooltip() const override;
    bool isNotification() const override;
    bool isCriticalNotification() const override;
    bool isOnScreenDisplay() const override;
    bool isComboBox() const override;
    bool isDNDIcon() const override;
    bool isPopupWindow() const override;
    bool isSpecialWindow() const override;

    bool isCloseable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isResizable() const override;
    bool isMinimizable() const override;
    bool isMaximizable() const override;
    bool isFullScreenable() const override;

    bool isOutline() const override;
    bool isShape() const override;

    bool keepAbove() const override;
    void setKeepAbove(bool set) override;
    bool keepBelow() const override;
    void setKeepBelow(bool set) override;
    bool isMinimized() const override;
    void setMinimized(bool set) override;

    bool skipTaskbar() const override;
    void setSkipTaskbar(bool set) override;
    bool skipPager() const override;
    void setSkipPager(bool set) override;
    bool skipSwitcher() const override;
    void setSkipSwitcher(bool set) override;
    bool skipsCloseAnimation() const override;
    void setSkipCloseAnimation(bool set) override;

    bool isActive() const override;
    bool isDemandingAttention() const override;
    void demandAttention(bool set) override;
    bool wantsInput() const override;
    bool applicationMenuActive() const override;
    bool unresponsive() const override;

    bool isTransient() const override;
    console_window* transientFor() const override;
    bool isModal() const override;

    bool decorationHasAlpha() const override;
    bool hasNoBorder() const override;
    void setNoBorder(bool set) override;
    QString colorScheme() const override;

    QByteArray desktopFileName() const override;
    bool hasApplicationMenu() const override;
    bool providesContextHelp() const override;

    bool isDeleted() const override;
    quint32 surfaceId() const override;
    Wrapland::Server::Surface* surface() const override;

    QSize basicUnit() const override;
    bool isBlockingCompositing() override;
    void setBlockingCompositing(bool block) override;

    Toplevel* ref_win;
};

}
}
