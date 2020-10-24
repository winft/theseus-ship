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

#include "appmenu.h"
#include "decorations/decoratedclient.h"
#include "decorations/decorationpalette.h"
#include "cursor.h"
#include "effects.h"
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
#include <Wrapland/Server/plasma_window.h>

#include <KDesktopFile>

#include <QDir>
#include <QMouseEvent>
#include <QStyleHints>

namespace KWin
{

QHash<QString, std::weak_ptr<Decoration::DecorationPalette>> AbstractClient::s_palettes;
std::shared_ptr<Decoration::DecorationPalette> AbstractClient::s_defaultPalette;

AbstractClient::AbstractClient()
    : Toplevel()
    , m_colorScheme(QStringLiteral("kdeglobals"))
{
    win::setup_connections(this);
}

AbstractClient::~AbstractClient()
{
    Q_ASSERT(m_blockGeometryUpdates == 0);
    Q_ASSERT(m_decoration.decoration == nullptr);
}

void AbstractClient::updateMouseGrab()
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

void AbstractClient::setDesktops(QVector<VirtualDesktop*> desktops)
{
    //on x11 we can have only one desktop at a time
    if (kwinApp()->operationMode() == Application::OperationModeX11 && desktops.size() > 1) {
        desktops = QVector<VirtualDesktop*>({desktops.last()});
    }

    if (desktops == m_desktops) {
        return;
    }

    int was_desk = AbstractClient::desktop();
    const bool wasOnCurrentDesktop = isOnCurrentDesktop() && was_desk >= 0;

    m_desktops = desktops;

    if (windowManagementInterface()) {
        if (m_desktops.isEmpty()) {
            windowManagementInterface()->setOnAllDesktops(true);
        } else {
            windowManagementInterface()->setOnAllDesktops(false);
            auto currentDesktops = windowManagementInterface()->plasmaVirtualDesktops();
            for (auto desktop: m_desktops) {
                if (!currentDesktops.contains(desktop->id())) {
                    windowManagementInterface()->addPlasmaVirtualDesktop(desktop->id());
                } else {
                    currentDesktops.removeOne(desktop->id());
                }
            }
            for (auto desktopId: currentDesktops) {
                windowManagementInterface()->removePlasmaVirtualDesktop(desktopId);
            }
        }
    }
    if (info) {
        info->setDesktop(desktop());
    }

    if ((was_desk == NET::OnAllDesktops) != (desktop() == NET::OnAllDesktops)) {
        // onAllDesktops changed
        workspace()->updateOnAllDesktopsOfTransients(this);
    }

    auto transients_stacking_order = workspace()->ensureStackingOrder(transients());
    for (auto it = transients_stacking_order.constBegin();
            it != transients_stacking_order.constEnd();
            ++it)
        (*it)->setDesktops(desktops);

    if (isModal())  // if a modal dialog is moved, move the mainwindow with it as otherwise
        // the (just moved) modal dialog will confusingly return to the mainwindow with
        // the next desktop change
    {
        foreach (AbstractClient * c2, mainClients())
        c2->setDesktops(desktops);
    }

    doSetDesktop(desktop(), was_desk);

    FocusChain::self()->update(this, FocusChain::MakeFirst);
    updateWindowRules(Rules::Desktop);

    emit desktopChanged();
    if (wasOnCurrentDesktop != isOnCurrentDesktop())
        emit desktopPresenceChanged(this, was_desk);
    emit x11DesktopIdsChanged();
}

void AbstractClient::doSetDesktop(int desktop, int was_desk)
{
    Q_UNUSED(desktop)
    Q_UNUSED(was_desk)
}

QVector<uint> AbstractClient::x11DesktopIds() const
{
    return win::x11_desktop_ids(this);
}

bool AbstractClient::isShadeable() const
{
    return false;
}

void AbstractClient::setShade(ShadeMode mode)
{
    Q_UNUSED(mode)
}

ShadeMode AbstractClient::shadeMode() const
{
    return ShadeNone;
}

win::position AbstractClient::titlebarPosition() const
{
    return win::position::top;
}

win::position AbstractClient::moveResizePointerMode() const
{
    return m_moveResize.pointer;
}

void AbstractClient::setMinimized(bool set)
{
    set ? minimize() : unminimize();
}

void AbstractClient::minimize(bool avoid_animation)
{
    if (!isMinimizable() || isMinimized())
        return;

    if (isShade() && info) // NETWM restriction - KWindowInfo::isMinimized() == Hidden && !Shaded
        info->setState(NET::States(), NET::Shaded);

    m_minimized = true;

    doMinimize();

    updateWindowRules(Rules::Minimize);
    // TODO: merge signal with s_minimized
    addWorkspaceRepaint(visibleRect());
    emit clientMinimized(this, !avoid_animation);
    emit minimizedChanged();
}

void AbstractClient::unminimize(bool avoid_animation)
{
    if (!isMinimized())
        return;

    if (rules()->checkMinimize(false)) {
        return;
    }

    if (isShade() && info) // NETWM restriction - KWindowInfo::isMinimized() == Hidden && !Shaded
        info->setState(NET::Shaded, NET::Shaded);

    m_minimized = false;

    doMinimize();

    updateWindowRules(Rules::Minimize);
    emit clientUnminimized(this, !avoid_animation);
    emit minimizedChanged();
}

void AbstractClient::doMinimize()
{
}

QPalette AbstractClient::palette() const
{
    if (!m_palette) {
        return QPalette();
    }
    return m_palette->palette();
}

const Decoration::DecorationPalette *AbstractClient::decorationPalette() const
{
    return m_palette.get();
}

void AbstractClient::updateColorScheme(QString path)
{
    if (path.isEmpty()) {
        path = QStringLiteral("kdeglobals");
    }

    if (!m_palette || m_colorScheme != path) {
        m_colorScheme = path;

        if (m_palette) {
            disconnect(m_palette.get(), &Decoration::DecorationPalette::changed, this, &AbstractClient::handlePaletteChange);
        }

        auto it = s_palettes.find(m_colorScheme);

        if (it == s_palettes.end() || it->expired()) {
            m_palette = std::make_shared<Decoration::DecorationPalette>(m_colorScheme);
            if (m_palette->isValid()) {
                s_palettes[m_colorScheme] = m_palette;
            } else {
                if (!s_defaultPalette) {
                    s_defaultPalette = std::make_shared<Decoration::DecorationPalette>(QStringLiteral("kdeglobals"));
                    s_palettes[QStringLiteral("kdeglobals")] = s_defaultPalette;
                }

                m_palette = s_defaultPalette;
            }

            if (m_colorScheme == QStringLiteral("kdeglobals")) {
                s_defaultPalette = m_palette;
            }
        } else {
            m_palette = it->lock();
        }

        connect(m_palette.get(), &Decoration::DecorationPalette::changed, this, &AbstractClient::handlePaletteChange);

        emit paletteChanged(palette());
        emit colorSchemeChanged();
    }
}

void AbstractClient::handlePaletteChange()
{
    emit paletteChanged(palette());
}

QSize AbstractClient::maxSize() const
{
    return rules()->checkMaxSize(QSize(INT_MAX, INT_MAX));
}

QSize AbstractClient::minSize() const
{
    return rules()->checkMinSize(QSize(0, 0));
}

void AbstractClient::blockGeometryUpdates(bool block)
{
    if (block) {
        if (m_blockGeometryUpdates == 0)
            m_pendingGeometryUpdate = PendingGeometryNone;
        ++m_blockGeometryUpdates;
    } else {
        if (--m_blockGeometryUpdates == 0) {
            if (m_pendingGeometryUpdate != PendingGeometryNone) {
                if (isShade())
                    setFrameGeometry(QRect(pos(), win::adjusted_size(this)), win::force_geometry::no);
                else
                    setFrameGeometry(frameGeometry(), win::force_geometry::no);
                m_pendingGeometryUpdate = PendingGeometryNone;
            }
        }
    }
}

void AbstractClient::move(int x, int y, win::force_geometry force)
{
    // resuming geometry updates is handled only in setGeometry()
    Q_ASSERT(pendingGeometryUpdate() == PendingGeometryNone || areGeometryUpdatesBlocked());
    QPoint p(x, y);
    if (!areGeometryUpdatesBlocked() && p != rules()->checkPosition(p)) {
        qCDebug(KWIN_CORE) << "forced position fail:" << p << ":" << rules()->checkPosition(p);
    }
    if (force == win::force_geometry::no && m_frameGeometry.topLeft() == p)
        return;
    auto old_frame_geometry = m_frameGeometry;
    m_frameGeometry.moveTopLeft(p);
    if (areGeometryUpdatesBlocked()) {
        if (pendingGeometryUpdate() == PendingGeometryForced)
            {} // maximum, nothing needed
        else if (force == win::force_geometry::yes)
            setPendingGeometryUpdate(PendingGeometryForced);
        else
            setPendingGeometryUpdate(PendingGeometryNormal);
        return;
    }
    doMove(x, y);
    updateWindowRules(Rules::Position);
    screens()->setCurrent(this);
    workspace()->updateStackingOrder();
    // client itself is not damaged
    win::add_repaint_during_geometry_updates(this);
    updateGeometryBeforeUpdateBlocking();
    emit geometryChanged();
    Q_EMIT frameGeometryChanged(this, old_frame_geometry);
}

// When the user pressed mouse on the titlebar, don't activate move immediately,
// since it may be just a click. Activate instead after a delay. Move used to be
// activated only after moving by several pixels, but that looks bad.
void AbstractClient::startDelayedMoveResize()
{
    Q_ASSERT(!m_moveResize.delayedTimer);
    m_moveResize.delayedTimer = new QTimer(this);
    m_moveResize.delayedTimer->setSingleShot(true);
    connect(m_moveResize.delayedTimer, &QTimer::timeout, this,
        [this]() {
            Q_ASSERT(isMoveResizePointerButtonDown());
            if (!win::start_move_resize(this)) {
                setMoveResizePointerButtonDown(false);
            }
            updateCursor();
            stopDelayedMoveResize();
        }
    );
    m_moveResize.delayedTimer->start(QApplication::startDragTime());
}

void AbstractClient::stopDelayedMoveResize()
{
    delete m_moveResize.delayedTimer;
    m_moveResize.delayedTimer = nullptr;
}

bool AbstractClient::hasStrut() const
{
    return false;
}

void AbstractClient::destroyWindowManagementInterface()
{
    if (m_windowManagementInterface) {
        m_windowManagementInterface->unmap();
        m_windowManagementInterface = nullptr;
    }
}

bool AbstractClient::performMouseCommand(Options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(this, cmd, globalPos);
}

void AbstractClient::setTransientFor(AbstractClient *transientFor)
{
    if (transientFor == this) {
        // cannot be transient for one self
        return;
    }
    if (m_transientFor == transientFor) {
        return;
    }
    m_transientFor = transientFor;
    emit transientChanged();
}

const AbstractClient *AbstractClient::transientFor() const
{
    return m_transientFor;
}

AbstractClient *AbstractClient::transientFor()
{
    return m_transientFor;
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

bool AbstractClient::hasTransient(const AbstractClient *c, bool indirect) const
{
    Q_UNUSED(indirect);
    return c->transientFor() == this;
}

QList< AbstractClient* > AbstractClient::mainClients() const
{
    if (const AbstractClient *t = transientFor()) {
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

void AbstractClient::addTransient(AbstractClient *cl)
{
    Q_ASSERT(!m_transients.contains(cl));
    Q_ASSERT(cl != this);
    m_transients.append(cl);
}

void AbstractClient::removeTransient(AbstractClient *cl)
{
    m_transients.removeAll(cl);
    if (cl->transientFor() == this) {
        cl->setTransientFor(nullptr);
    }
}

void AbstractClient::removeTransientFromList(AbstractClient *cl)
{
    m_transients.removeAll(cl);
}

QSize AbstractClient::sizeForClientSize(const QSize &wsize,
                                        [[maybe_unused]] win::size_mode mode,
                                        [[maybe_unused]] bool noframe) const
{
    return wsize + QSize(win::left_border(this) + win::right_border(this),
                         win::top_border(this) + win::bottom_border(this));
}

QRect AbstractClient::bufferGeometryBeforeUpdateBlocking() const
{
    return m_bufferGeometryBeforeUpdateBlocking;
}

QRect AbstractClient::frameGeometryBeforeUpdateBlocking() const
{
    return m_frameGeometryBeforeUpdateBlocking;
}

void AbstractClient::updateGeometryBeforeUpdateBlocking()
{
    m_bufferGeometryBeforeUpdateBlocking = bufferGeometry();
    m_frameGeometryBeforeUpdateBlocking = frameGeometry();
}

void AbstractClient::doMove(int, int)
{
}

void AbstractClient::updateInitialMoveResizeGeometry()
{
    m_moveResize.initialGeometry = frameGeometry();
    m_moveResize.geometry = m_moveResize.initialGeometry;
    m_moveResize.startScreen = screen();
}

void AbstractClient::updateCursor()
{
    auto m = moveResizePointerMode();
    if (!isResizable() || isShade())
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
        if (isMoveResize())
            c = Qt::SizeAllCursor;
        else
            c = Qt::ArrowCursor;
        break;
    }
    if (c == m_moveResize.cursor)
        return;
    m_moveResize.cursor = c;
    emit moveResizeCursorChanged(c);
}

void AbstractClient::leaveMoveResize()
{
    workspace()->setMoveResizeClient(nullptr);
    setMoveResize(false);
    if (ScreenEdges::self()->isDesktopSwitchingMovingClients())
        ScreenEdges::self()->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    if (isElectricBorderMaximizing()) {
        outline()->hide();
        win::elevate(this, false);
    }
}

void AbstractClient::updateHaveResizeEffect()
{
    m_haveResizeEffect = effects && static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::Resize);
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

void AbstractClient::delayed_electric_maximize()
{
    if (!m_electricMaximizingDelay) {
        m_electricMaximizingDelay = new QTimer(this);
        m_electricMaximizingDelay->setInterval(250);
        m_electricMaximizingDelay->setSingleShot(true);
        connect(m_electricMaximizingDelay, &QTimer::timeout, [this]() {
            if (win::is_move(this)) {
                setElectricBorderMaximizing(electricBorderMode() != QuickTileMode(QuickTileFlag::None));
            }
        });
    }
    m_electricMaximizingDelay->start();
}

QRect AbstractClient::visible_rect_before_geometry_update() const
{
    return m_visibleRectBeforeGeometryUpdate;
}

void AbstractClient::set_visible_rect_before_geometry_update(QRect const& rect)
{
    m_visibleRectBeforeGeometryUpdate = rect;
}

void AbstractClient::keyPressEvent(uint key_code)
{
    win::key_press_event(this, key_code);
}

QSize AbstractClient::resizeIncrements() const
{
    return QSize(1, 1);
}

void AbstractClient::setMoveResizePointerMode(win::position mode) {
    m_moveResize.pointer = mode;
}

void AbstractClient::destroyDecoration()
{
    delete m_decoration.decoration;
    m_decoration.decoration = nullptr;
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
        if (m_decoration.doubleClickTimer.isValid()) {
            const qint64 interval = m_decoration.doubleClickTimer.elapsed();
            m_decoration.doubleClickTimer.invalidate();
            if (interval > QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                m_decoration.doubleClickTimer.start(); // expired -> new first click and pot. init
            } else {
                Workspace::self()->performWindowOperation(this, options->operationTitlebarDblClick());
                win::dont_move_resize(this);
                return false;
            }
        }
         else {
            m_decoration.doubleClickTimer.start(); // new first click and pot. init, could be invalidated by release - see below
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
        setMoveResizePointerMode(win::mouse_position(this));
        setMoveResizePointerButtonDown(true);
        setMoveOffset(event->pos());
        setInvertedMoveOffset(rect().bottomRight() - moveOffset());
        setUnrestrictedMoveResize(false);
        startDelayedMoveResize();
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

void AbstractClient::startDecorationDoubleClickTimer()
{
    m_decoration.doubleClickTimer.start();
}

void AbstractClient::invalidateDecorationDoubleClickTimer()
{
    m_decoration.doubleClickTimer.invalidate();
}

bool AbstractClient::providesContextHelp() const
{
    return false;
}

void AbstractClient::showContextHelp()
{
}

QPointer<Decoration::DecoratedClientImpl> AbstractClient::decoratedClient() const
{
    return m_decoration.client;
}

void AbstractClient::setDecoratedClient(QPointer< Decoration::DecoratedClientImpl > client)
{
    m_decoration.client = client;
}

QRect AbstractClient::iconGeometry() const
{
    if (!windowManagementInterface() || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    int minDistance = INT_MAX;
    AbstractClient *candidatePanel = nullptr;
    QRect candidateGeom;

    for (auto i = windowManagementInterface()->minimizedGeometries().constBegin(), end = windowManagementInterface()->minimizedGeometries().constEnd(); i != end; ++i) {
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
    if (isDecorated()) {
        return Toplevel::inputGeometry() + decoration()->resizeOnlyBorders();
    }
    return Toplevel::inputGeometry();
}

bool AbstractClient::dockWantsInput() const
{
    return false;
}

void AbstractClient::setDesktopFileName(QByteArray name)
{
    name = rules()->checkDesktopFile(name).toUtf8();
    if (name == m_desktopFileName) {
        return;
    }
    m_desktopFileName = name;
    updateWindowRules(Rules::DesktopFile);
    emit desktopFileNameChanged();
}

QString AbstractClient::iconFromDesktopFile() const
{
    const QString desktopFileName = QString::fromUtf8(m_desktopFileName);
    QString desktopFilePath;

    if (QDir::isAbsolutePath(desktopFileName)) {
        desktopFilePath = desktopFileName;
    }

    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName);
    }
    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName + QLatin1String(".desktop"));
    }

    KDesktopFile df(desktopFilePath);
    return df.readIcon();
}

bool AbstractClient::hasApplicationMenu() const
{
    return ApplicationMenu::self()->applicationMenuEnabled() && !m_applicationMenuServiceName.isEmpty() && !m_applicationMenuObjectPath.isEmpty();
}

void AbstractClient::updateApplicationMenuServiceName(const QString &serviceName)
{
    const bool old_hasApplicationMenu = hasApplicationMenu();

    m_applicationMenuServiceName = serviceName;

    const bool new_hasApplicationMenu = hasApplicationMenu();

    if (old_hasApplicationMenu != new_hasApplicationMenu) {
        emit hasApplicationMenuChanged(new_hasApplicationMenu);
    }
}

void AbstractClient::updateApplicationMenuObjectPath(const QString &objectPath)
{
    const bool old_hasApplicationMenu = hasApplicationMenu();

    m_applicationMenuObjectPath = objectPath;

    const bool new_hasApplicationMenu = hasApplicationMenu();

    if (old_hasApplicationMenu != new_hasApplicationMenu) {
        emit hasApplicationMenuChanged(new_hasApplicationMenu);
    }
}

void AbstractClient::setApplicationMenuActive(bool applicationMenuActive)
{
    if (m_applicationMenuActive != applicationMenuActive) {
        m_applicationMenuActive = applicationMenuActive;
        emit applicationMenuActiveChanged(applicationMenuActive);
    }
}

bool AbstractClient::unresponsive() const
{
    return m_unresponsive;
}

void AbstractClient::setUnresponsive(bool unresponsive)
{
    if (m_unresponsive != unresponsive) {
        m_unresponsive = unresponsive;
        emit unresponsiveChanged(m_unresponsive);
        emit captionChanged();
    }
}

// We need to keep this function for now because of inheritance of child classes (InternalClient).
// TODO: remove when our inheritance hierarchy is flattened.
AbstractClient *AbstractClient::findClientWithSameCaption() const
{
    return win::find_client_with_same_caption(this);
}

QString AbstractClient::caption() const
{
    QString cap = captionNormal() + captionSuffix();
    if (unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

void AbstractClient::removeRule(Rules* rule)
{
    m_rules.remove(rule);
}

void AbstractClient::discardTemporaryRules()
{
    m_rules.discardTemporary();
}

void AbstractClient::evaluateWindowRules()
{
    setupWindowRules(true);
    applyWindowRules();
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

void AbstractClient::setElectricBorderMode(QuickTileMode mode)
{
    if (mode != QuickTileMode(QuickTileFlag::Maximize)) {
        // sanitize the mode, ie. simplify "invalid" combinations
        if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Horizontal))
            mode &= ~QuickTileMode(QuickTileFlag::Horizontal);
        if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Vertical))
            mode &= ~QuickTileMode(QuickTileFlag::Vertical);
    }
    m_electricMode = mode;
}

void AbstractClient::setElectricBorderMaximizing(bool maximizing)
{
    m_electricMaximizing = maximizing;

    if (maximizing)
        outline()->show(win::electric_border_maximize_geometry(this, Cursor::pos(), desktop()),
                        moveResizeGeometry());
    else
        outline()->hide();

    win::elevate(this, maximizing);
}

void AbstractClient::set_QuickTileMode_win(QuickTileMode mode)
{
    m_quickTileMode = mode;
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

void AbstractClient::setWindowManagementInterface(Wrapland::Server::PlasmaWindow* plasma_window)
{
    m_windowManagementInterface = plasma_window;
}

QPoint AbstractClient::clientPos() const
{
    return QPoint(win::left_border(this), win::top_border(this));
}

}
