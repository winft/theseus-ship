/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <kwineffects/effect_window.h>

#include <QHash>

namespace KWin
{

namespace win
{

namespace x11
{
class group;
}

}

class Toplevel;

namespace render
{

class window;

class basic_thumbnail_item;
class desktop_thumbnail_item;
class window_thumbnail_item;

class KWIN_EXPORT effects_window_impl : public EffectWindow
{
public:
    explicit effects_window_impl(Toplevel* toplevel);
    ~effects_window_impl() override;

    void enablePainting(int reason) override;
    void disablePainting(int reason) override;
    bool isPaintingEnabled() override;

    void addRepaint(const QRect& r) override;
    void addRepaint(int x, int y, int w, int h) override;
    void addRepaintFull() override;
    void addLayerRepaint(const QRect& r) override;
    void addLayerRepaint(int x, int y, int w, int h) override;

    void refWindow() override;
    void unrefWindow() override;

    const EffectWindowGroup* group() const override;

    bool isDeleted() const override;
    bool isMinimized() const override;
    double opacity() const override;
    bool hasAlpha() const override;

    QStringList activities() const override;
    int desktop() const override;
    QVector<uint> desktops() const override;
    int x() const override;
    int y() const override;
    int width() const override;
    int height() const override;

    QSize basicUnit() const override;
    QRect geometry() const override;
    QRect frameGeometry() const override;
    QRect bufferGeometry() const override;
    QRect clientGeometry() const override;

    QString caption() const override;

    QRect expandedGeometry() const override;
    int screen() const override;
    QPoint pos() const override;
    QSize size() const override;
    QRect rect() const override;

    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isUserMove() const override;
    bool isUserResize() const override;
    QRect iconGeometry() const override;

    bool isDesktop() const override;
    bool isDock() const override;
    bool isToolbar() const override;
    bool isMenu() const override;
    bool isNormalWindow() const override;
    bool isSpecialWindow() const override;
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
    bool skipsCloseAnimation() const override;

    bool acceptsFocus() const override;
    bool keepAbove() const override;
    bool keepBelow() const override;
    bool isModal() const override;
    bool isPopupWindow() const override;
    bool isOutline() const override;
    bool isLockScreen() const override;

    Wrapland::Server::Surface* surface() const override;
    bool isFullScreen() const override;
    bool isUnresponsive() const override;

    QRect contentsRect() const override;
    bool decorationHasAlpha() const override;
    QIcon icon() const override;
    QString windowClass() const override;
    NET::WindowType windowType() const override;
    bool isSkipSwitcher() const override;
    bool isCurrentTab() const override;
    QString windowRole() const override;

    bool isManaged() const override;
    bool isWaylandClient() const override;
    bool isX11Client() const override;

    pid_t pid() const override;

    QRect decorationInnerRect() const override;
    KDecoration2::Decoration* decoration() const override;
    QByteArray readProperty(long atom, long type, int format) const override;
    void deleteProperty(long atom) const override;

    EffectWindow* findModal() override;
    EffectWindow* transientFor() override;
    EffectWindowList mainWindows() const override;

    WindowQuadList buildQuads(bool force = false) const override;

    void minimize() override;
    void unminimize() override;
    void closeWindow() override;

    void referencePreviousWindowPixmap() override;
    void unreferencePreviousWindowPixmap() override;

    QWindow* internalWindow() const override;

    Toplevel const* window() const
    {
        return toplevel;
    }

    Toplevel* window()
    {
        return toplevel;
    }

    void setWindow(Toplevel* w);            // internal
    void setSceneWindow(render::window* w); // internal

    const render::window* sceneWindow() const
    {
        return sw;
    }

    render::window* sceneWindow()
    {
        return sw;
    }

    void elevate(bool elevate);

    void setData(int role, const QVariant& data) override;
    QVariant data(int role) const override;

    void registerThumbnail(basic_thumbnail_item* item);
    QHash<window_thumbnail_item*, QPointer<effects_window_impl>> const& thumbnails() const
    {
        return m_thumbnails;
    }
    QList<desktop_thumbnail_item*> const& desktopThumbnails() const
    {
        return m_desktopThumbnails;
    }

private:
    void thumbnailDestroyed(QObject* object);
    void thumbnailTargetChanged();
    void desktopThumbnailDestroyed(QObject* object);
    void insertThumbnail(window_thumbnail_item* item);

    Toplevel* toplevel;
    render::window* sw; // This one is used only during paint pass.
    QHash<int, QVariant> dataMap;
    QHash<window_thumbnail_item*, QPointer<effects_window_impl>> m_thumbnails;
    QList<desktop_thumbnail_item*> m_desktopThumbnails;
    bool managed = false;
    bool waylandClient;
    bool x11Client;
};

class effect_window_group_impl : public EffectWindowGroup
{
public:
    explicit effect_window_group_impl(win::x11::group* g);
    EffectWindowList members() const override;

private:
    win::x11::group* group;
};

}
}
