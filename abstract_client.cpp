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
#include "abstract_client.h"

#include "cursor.h"
#include "focuschain.h"
#include "outline.h"
#include "screens.h"
#include "screenedge.h"
#include "useractions.h"
#include "win/control.h"
#include "win/setup.h"
#include "win/win.h"
#include "workspace.h"

#include "wayland_server.h"

#include <QDir>
#include <QMouseEvent>
#include <QStyleHints>

namespace KWin
{

AbstractClient::AbstractClient()
    : Toplevel()
{
    win::setup_connections(this);
}

AbstractClient::~AbstractClient()
{
}

bool AbstractClient::isTransient() const
{
    return false;
}

void AbstractClient::setClientShown(bool shown)
{
    Q_UNUSED(shown)
}

win::maximize_mode AbstractClient::requestedMaximizeMode() const
{
    return maximizeMode();
}

xcb_timestamp_t AbstractClient::userTime() const
{
    return XCB_TIME_CURRENT_TIME;
}

void AbstractClient::doSetActive()
{
}

win::layer AbstractClient::layer() const
{
    if (m_layer == win::layer::unknown) {
        const_cast< AbstractClient* >(this)->m_layer = win::belong_to_layer(this);
    }
    return m_layer;
}

void AbstractClient::invalidateLayer()
{
    m_layer = win::layer::unknown;
}

bool AbstractClient::belongsToDesktop() const
{
    return false;
}

win::layer AbstractClient::layerForDock() const
{
    // slight hack for the 'allow window to cover panel' Kicker setting
    // don't move keepbelow docks below normal window, but only to the same
    // layer, so that both may be raised to cover the other
    if (control()->keep_below()) {
        return win::layer::normal;
    }
    if (control()->keep_above()) {
        // slight hack for the autohiding panels
        return win::layer::above;
    }
    return win::layer::dock;
}

void AbstractClient::doSetKeepAbove()
{
}

void AbstractClient::doSetKeepBelow()
{
}

void AbstractClient::doSetDesktop(int desktop, int was_desk)
{
    Q_UNUSED(desktop)
    Q_UNUSED(was_desk)
}

bool AbstractClient::isShadeable() const
{
    return false;
}

void AbstractClient::setShade(win::shade mode)
{
    Q_UNUSED(mode)
}

win::shade AbstractClient::shadeMode() const
{
    return win::shade::none;
}

win::position AbstractClient::titlebarPosition() const
{
    return win::position::top;
}

void AbstractClient::doMinimize()
{
}

QSize AbstractClient::maxSize() const
{
    return control()->rules().checkMaxSize(QSize(INT_MAX, INT_MAX));
}

QSize AbstractClient::minSize() const
{
    return control()->rules().checkMinSize(QSize(0, 0));
}

void AbstractClient::move(int x, int y, win::force_geometry force)
{
    // resuming geometry updates is handled only in setGeometry()
    Q_ASSERT(control()->pending_geometry_update() == win::pending_geometry::none
             || control()->geometry_updates_blocked());
    QPoint p(x, y);
    if (!control()->geometry_updates_blocked() && p != control()->rules().checkPosition(p)) {
        qCDebug(KWIN_CORE) << "forced position fail:" << p << ":" << control()->rules().checkPosition(p);
    }
    if (force == win::force_geometry::no && m_frameGeometry.topLeft() == p)
        return;
    auto old_frame_geometry = m_frameGeometry;
    m_frameGeometry.moveTopLeft(p);
    if (control()->geometry_updates_blocked()) {
        if (control()->pending_geometry_update() == win::pending_geometry::forced)
            {} // maximum, nothing needed
        else if (force == win::force_geometry::yes)
            control()->set_pending_geometry_update(win::pending_geometry::forced);
        else
            control()->set_pending_geometry_update(win::pending_geometry::normal);
        return;
    }
    doMove(x, y);
    updateWindowRules(Rules::Position);
    screens()->setCurrent(this);
    workspace()->updateStackingOrder();
    // client itself is not damaged
    win::add_repaint_during_geometry_updates(this);
    control()->update_geometry_before_update_blocking();
    emit geometryChanged();
    Q_EMIT frameGeometryChanged(this, old_frame_geometry);
}

bool AbstractClient::hasStrut() const
{
    return false;
}

bool AbstractClient::performMouseCommand(Options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(this, cmd, globalPos);
}

bool AbstractClient::hasTransientPlacementHint() const
{
    return false;
}

QRect AbstractClient::transientPlacement(const QRect &bounds) const
{
    Q_UNUSED(bounds);
    Q_UNREACHABLE();
    return QRect();
}

QList< AbstractClient* > AbstractClient::mainClients() const
{
    if (auto t = dynamic_cast<const AbstractClient *>(control()->transient_lead())) {
        return QList<AbstractClient*>{const_cast< AbstractClient* >(t)};
    }
    return QList<AbstractClient*>();
}

void AbstractClient::setModal(bool m)
{
    // Qt-3.2 can have even modal normal windows :(
    if (m_modal == m)
        return;
    m_modal = m;
    emit modalChanged();
    // Changing modality for a mapped window is weird (?)
    // _NET_WM_STATE_MODAL should possibly rather be _NET_WM_WINDOW_TYPE_MODAL_DIALOG
}

bool AbstractClient::isModal() const
{
    return m_modal;
}

QSize AbstractClient::sizeForClientSize(const QSize &wsize,
                                        [[maybe_unused]] win::size_mode mode,
                                        [[maybe_unused]] bool noframe) const
{
    return wsize + QSize(win::left_border(this) + win::right_border(this),
                         win::top_border(this) + win::bottom_border(this));
}

void AbstractClient::doMove(int, int)
{
}

void AbstractClient::updateCursor()
{
    auto& mov_res = control()->move_resize();
    auto m = mov_res.contact;
    if (!isResizable() || win::shaded(this))
        m = win::position::center;
    CursorShape c = Qt::ArrowCursor;
    switch(m) {
    case win::position::top_left:
        c = KWin::ExtendedCursor::SizeNorthWest;
        break;
    case win::position::bottom_right:
        c = KWin::ExtendedCursor::SizeSouthEast;
        break;
    case win::position::bottom_left:
        c = KWin::ExtendedCursor::SizeSouthWest;
        break;
    case win::position::top_right:
        c = KWin::ExtendedCursor::SizeNorthEast;
        break;
    case win::position::top:
        c = KWin::ExtendedCursor::SizeNorth;
        break;
    case win::position::bottom:
        c = KWin::ExtendedCursor::SizeSouth;
        break;
    case win::position::left:
        c = KWin::ExtendedCursor::SizeWest;
        break;
    case win::position::right:
        c = KWin::ExtendedCursor::SizeEast;
        break;
    default:
        if (mov_res.enabled) {
            c = Qt::SizeAllCursor;
        } else {
            c = Qt::ArrowCursor;
        }
        break;
    }
    if (c == mov_res.cursor)
        return;
    mov_res.cursor = c;
    emit moveResizeCursorChanged(c);
}

void AbstractClient::leaveMoveResize()
{
    workspace()->setMoveResizeClient(nullptr);
    control()->move_resize().enabled = false;
    if (ScreenEdges::self()->isDesktopSwitchingMovingClients())
        ScreenEdges::self()->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    if (control()->electric_maximizing()) {
        outline()->hide();
        win::elevate(this, false);
    }
}

bool AbstractClient::doStartMoveResize()
{
    return true;
}

void AbstractClient::positionGeometryTip()
{
}

void AbstractClient::doPerformMoveResize()
{
}

bool AbstractClient::isWaitingForMoveResizeSync() const
{
    return false;
}

void AbstractClient::doResizeSync()
{
}

void AbstractClient::keyPressEvent(uint key_code)
{
    win::key_press_event(this, key_code);
}

QSize AbstractClient::resizeIncrements() const
{
    return QSize(1, 1);
}

void AbstractClient::layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const
{
    win::layout_decoration_rects(this, left, top, right, bottom);
}

bool AbstractClient::processDecorationButtonPress(QMouseEvent *event, bool ignoreMenu)
{
    Options::MouseCommand com = Options::MouseNothing;
    bool active = control()->active();
    if (!wantsInput())    // we cannot be active, use it anyway
        active = true;

    // check whether it is a double click
    if (event->button() == Qt::LeftButton && win::titlebar_positioned_under_mouse(this)) {
        auto& deco = control()->deco();
        if (deco.double_click_timer.isValid()) {
            auto const interval = deco.double_click_timer.elapsed();
            deco.double_click_timer.invalidate();
            if (interval > QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                // expired -> new first click and pot. init
                deco.double_click_timer.start();
            } else {
                Workspace::self()->performWindowOperation(this, options->operationTitlebarDblClick());
                win::dont_move_resize(this);
                return false;
            }
        }
         else {
            deco.double_click_timer.start(); // new first click and pot. init, could be invalidated by release - see below
        }
    }

    if (event->button() == Qt::LeftButton)
        com = active ? options->commandActiveTitlebar1() : options->commandInactiveTitlebar1();
    else if (event->button() == Qt::MiddleButton)
        com = active ? options->commandActiveTitlebar2() : options->commandInactiveTitlebar2();
    else if (event->button() == Qt::RightButton)
        com = active ? options->commandActiveTitlebar3() : options->commandInactiveTitlebar3();
    if (event->button() == Qt::LeftButton
            && com != Options::MouseOperationsMenu // actions where it's not possible to get the matching
            && com != Options::MouseMinimize)  // mouse release event
    {
        auto& mov_res = control()->move_resize();

        mov_res.contact = win::mouse_position(this);
        mov_res.button_down = true;
        mov_res.offset = event->pos();
        mov_res.inverted_offset = rect().bottomRight() - mov_res.offset;
        mov_res.unrestricted = false;
        win::start_delayed_move_resize(this);
        updateCursor();
    }
    // In the new API the decoration may process the menu action to display an inactive tab's menu.
    // If the event is unhandled then the core will create one for the active window in the group.
    if (!ignoreMenu || com != Options::MouseOperationsMenu)
        performMouseCommand(com, event->globalPos());
    return !( // Return events that should be passed to the decoration in the new API
               com == Options::MouseRaise ||
               com == Options::MouseOperationsMenu ||
               com == Options::MouseActivateAndRaise ||
               com == Options::MouseActivate ||
               com == Options::MouseActivateRaiseAndPassClick ||
               com == Options::MouseActivateAndPassClick ||
               com == Options::MouseNothing);
}

bool AbstractClient::providesContextHelp() const
{
    return false;
}

void AbstractClient::showContextHelp()
{
}

QRect AbstractClient::iconGeometry() const
{
    auto management = control()->wayland_management();
    if (!management || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    int minDistance = INT_MAX;
    AbstractClient *candidatePanel = nullptr;
    QRect candidateGeom;

    for (auto i = management->minimizedGeometries().constBegin(),
         end = management->minimizedGeometries().constEnd(); i != end; ++i) {
        AbstractClient *client = waylandServer()->findAbstractClient(i.key());
        if (!client) {
            continue;
        }
        const int distance = QPoint(client->pos() - pos()).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            candidatePanel = client;
            candidateGeom = i.value();
        }
    }
    if (!candidatePanel) {
        return QRect();
    }
    return candidateGeom.translated(candidatePanel->pos());
}

QRect AbstractClient::inputGeometry() const
{
    if (auto& deco = control()->deco(); deco.enabled()) {
        return Toplevel::inputGeometry() + deco.decoration->resizeOnlyBorders();
    }
    return Toplevel::inputGeometry();
}

bool AbstractClient::dockWantsInput() const
{
    return false;
}

// We need to keep this function for now because of inheritance of child classes (InternalClient).
// TODO: remove when our inheritance hierarchy is flattened.
AbstractClient *AbstractClient::findClientWithSameCaption() const
{
    return win::find_client_with_same_caption(this);
}

void AbstractClient::setOnActivities(QStringList newActivitiesList)
{
    Q_UNUSED(newActivitiesList)
}

void AbstractClient::checkNoBorder()
{
    setNoBorder(false);
}

bool AbstractClient::groupTransient() const
{
    return false;
}

const Group *AbstractClient::group() const
{
    return nullptr;
}

Group *AbstractClient::group()
{
    return nullptr;
}

bool AbstractClient::isInternal() const
{
    return false;
}

bool AbstractClient::supportsWindowRules() const
{
    return true;
}

QMargins AbstractClient::frameMargins() const
{
    return QMargins(win::left_border(this), win::top_border(this),
                    win::right_border(this), win::bottom_border(this));
}

QPoint AbstractClient::framePosToClientPos(const QPoint &point) const
{
    return point + QPoint(win::left_border(this), win::top_border(this));
}

QPoint AbstractClient::clientPosToFramePos(const QPoint &point) const
{
    return point - QPoint(win::left_border(this), win::top_border(this));
}

QSize AbstractClient::frameSizeToClientSize(const QSize &size) const
{
    const int width = size.width() - win::left_border(this) - win::right_border(this);
    const int height = size.height() - win::top_border(this) - win::bottom_border(this);
    return QSize(width, height);
}

QSize AbstractClient::clientSizeToFrameSize(const QSize &size) const
{
    const int width = size.width() + win::left_border(this) + win::right_border(this);
    const int height = size.height() + win::top_border(this) + win::bottom_border(this);
    return QSize(width, height);
}

QSize AbstractClient::basicUnit() const
{
    return QSize(1, 1);
}

void AbstractClient::setBlockingCompositing([[maybe_unused]] bool block)
{
}

bool AbstractClient::isBlockingCompositing()
{
    return false;
}

QPoint AbstractClient::clientPos() const
{
    return QPoint(win::left_border(this), win::top_border(this));
}

}
