/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/cursor_shape.h"
#include "kwin_export.h"
#include "types.h"

#include <QObject>
#include <QPalette>
#include <QRegion>

namespace KWin
{

namespace base
{
class output;
}

namespace win
{

class KWIN_EXPORT window_qobject : public QObject
{
    Q_OBJECT
public:
    window_qobject();

Q_SIGNALS:
    void opacityChanged(qreal oldOpacity);
    void damaged(QRegion const& damage);

    void frame_geometry_changed(QRect const& old);
    void visible_geometry_changed();

    void paddingChanged(QRect const& old);
    void closed();
    void windowShown();
    void windowHidden();
    /**
     * Signal emitted when the window's shape state changed. That is if it did not have a shape
     * and received one or if the shape was withdrawn. Think of Chromium enabling/disabling KWin's
     * decoration.
     */
    void shapedChanged();
    /**
     * Emitted whenever the state changes in a way, that the Compositor should
     * schedule a repaint of the scene.
     */
    void needsRepaint();
    /**
     * Emitted whenever the Toplevel's output changes. This can happen either in consequence to
     * an output being removed/added or if the Toplevel's geometry changes.
     */
    void central_output_changed(base::output const* old_out, base::output const* new_out);
    void skipCloseAnimationChanged();
    /**
     * Emitted whenever the window role of the window changes.
     * @since 5.0
     */
    void windowRoleChanged();
    /**
     * Emitted whenever the window class name or resource name of the window changes.
     * @since 5.0
     */
    void windowClassChanged();
    /**
     * Emitted when a Wayland Surface gets associated with this Toplevel.
     * @since 5.3
     */
    void surfaceIdChanged(quint32);
    /**
     * @since 5.4
     */
    void hasAlphaChanged();

    /**
     * Emitted whenever the Surface for this Toplevel changes.
     */
    void surfaceChanged();

    /**
     * Emitted whenever the client's shadow changes.
     * @since 5.15
     */
    void shadowChanged();

    /**
     * Below signals only relevant for toplevels with control.
     */
    void iconChanged();
    void unresponsiveChanged(bool);
    void captionChanged();
    void hasApplicationMenuChanged(bool);
    void applicationMenuChanged();
    void applicationMenuActiveChanged(bool);

    void activeChanged();
    void demandsAttentionChanged();

    // to be forwarded by Workspace
    void desktopPresenceChanged(int);
    void desktopChanged();
    void x11DesktopIdsChanged();

    void minimizedChanged();
    void clientMinimized(bool animate);
    void clientUnminimized(bool animate);
    void maximize_mode_changed(KWin::win::maximize_mode);
    void quicktiling_changed();
    void keepAboveChanged(bool);
    void keepBelowChanged(bool);
    void blockingCompositingChanged(bool);

    void fullScreenChanged();
    void skipTaskbarChanged();
    void skipPagerChanged();
    void skipSwitcherChanged();

    void paletteChanged(QPalette const& p);
    void colorSchemeChanged();
    void transientChanged();
    void modalChanged();
    void moveResizedChanged();
    void moveResizeCursorChanged(input::cursor_shape);
    void clientStartUserMovedResized();
    void clientStepUserMovedResized(QRect const&);
    void clientFinishUserMovedResized();

    void closeableChanged(bool);
    void minimizeableChanged(bool);
    void maximizeableChanged(bool);
    void desktopFileNameChanged();
};

}
}
