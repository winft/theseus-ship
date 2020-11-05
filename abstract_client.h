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
#include "cursor.h"

#include <memory>

#include <QElapsedTimer>
#include <QPointer>

namespace KWin
{

namespace win
{
enum class force_geometry;
enum class maximize_mode;
enum class size_mode;
}

class KWIN_EXPORT AbstractClient : public Toplevel
{
    Q_OBJECT

public:
    ~AbstractClient() override;

    // TODO: remove boolean trap
    virtual AbstractClient *findModal(bool allow_itself = false) = 0;

    virtual QList<AbstractClient*> mainClients() const; // Call once before loop , is not indirect

    virtual bool performMouseCommand(Options::MouseCommand, const QPoint &globalPos);

    virtual QRect iconGeometry() const;

    virtual void applyWindowRules();

    /**
     * Leaves the move resize mode.
     *
     * Inheriting classes must invoke the base implementation which
     * ensures that the internal mode is properly ended.
     */
    virtual void leaveMoveResize();

    virtual bool belongsToSameApplication(const AbstractClient *other, win::same_client_check checks) const = 0;

    virtual void setShortcutInternal();

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
    void clientMaximizedStateChanged(KWin::AbstractClient*, KWin::win::maximize_mode);
    void clientMaximizedStateChanged(KWin::AbstractClient* c, bool h, bool v);
    void transientChanged();
    void modalChanged();
    void quicktiling_changed();
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
    void blockingCompositingChanged(KWin::AbstractClient* client);

protected:
    AbstractClient();
};

}

Q_DECLARE_METATYPE(KWin::AbstractClient*)
Q_DECLARE_METATYPE(QList<KWin::AbstractClient*>)

#endif
