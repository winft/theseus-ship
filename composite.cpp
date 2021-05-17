/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright © 2006        Lubos Lunak <l.lunak@kde.org>
Copyright © 2019-2020   Roman Gilg <subdiff@gmail.com>

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
#include "composite.h"

#include "abstract_wayland_output.h"
#include "dbusinterface.h"
#include "decorations/decoratedclient.h"
#include "effects.h"
#include "internal_client.h"
#include "overlaywindow.h"
#include "perf/ftrace.h"
#include "platform.h"
#include "presentation.h"
#include "scene.h"
#include "screens.h"
#include "shadow.h"
#include "useractions.h"
#include "utils.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xcbutils.h"

#include "render/wayland/output.h"

#include "win/net.h"
#include "win/remnant.h"
#include "win/scene.h"
#include "win/transient.h"

#include <kwingltexture.h>

#include <Wrapland/Server/surface.h>

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPluginLoader>
#include <KPluginMetaData>
#include <KNotification>
#include <KSelectionOwner>

#include <QDateTime>
#include <QFutureWatcher>
#include <QMenu>
#include <QOpenGLContext>
#include <QQuickWindow>
#include <QtConcurrentRun>
#include <QTextStream>
#include <QTimerEvent>

#include <xcb/composite.h>
#include <xcb/damage.h>

#include <cstdio>

Q_DECLARE_METATYPE(KWin::X11Compositor::SuspendReason)

namespace KWin
{

// See main.cpp:
extern int screen_number;
extern bool is_multihead;

static ulong s_msc = 0;

Compositor *Compositor::s_compositor = nullptr;
Compositor *Compositor::self()
{
    return s_compositor;
}

WaylandCompositor *WaylandCompositor::create(QObject *parent)
{
    Q_ASSERT(!s_compositor);
    auto *compositor = new WaylandCompositor(parent);
    s_compositor = compositor;
    return compositor;
}
X11Compositor *X11Compositor::create(QObject *parent)
{
    Q_ASSERT(!s_compositor);
    auto *compositor = new X11Compositor(parent);
    s_compositor = compositor;
    return compositor;
}

class CompositorSelectionOwner : public KSelectionOwner
{
    Q_OBJECT
public:
    CompositorSelectionOwner(const char *selection)
        : KSelectionOwner(selection, connection(), rootWindow())
        , m_owning(false)
    {
        connect (this, &CompositorSelectionOwner::lostOwnership,
                 this, [this]() { m_owning = false; });
    }
    bool owning() const {
        return m_owning;
    }
    void setOwning(bool own) {
        m_owning = own;
    }
private:
    bool m_owning;
};

static inline qint64 milliToNano(int milli) { return qint64(milli) * 1000 * 1000; }
static inline qint64 nanoToMilli(int nano) { return nano / (1000*1000); }

Compositor::Compositor(QObject* workspace)
    : QObject(workspace)
    , m_state(State::Off)
    , m_selectionOwner(nullptr)
    , m_delay(0)
    , m_bufferSwapPending(false)
    , m_scene(nullptr)
{
    connect(options, &Options::configChanged, this, &Compositor::configChanged);
    connect(options, &Options::animationSpeedChanged, this, &Compositor::configChanged);

    m_monotonicClock.start();

    // 2 sec which should be enough to restart the compositor.
    static const int compositorLostMessageDelay = 2000;

    m_releaseSelectionTimer.setSingleShot(true);
    m_releaseSelectionTimer.setInterval(compositorLostMessageDelay);
    connect(&m_releaseSelectionTimer, &QTimer::timeout,
            this, &Compositor::releaseCompositorSelection);

    m_unusedSupportPropertyTimer.setInterval(compositorLostMessageDelay);
    m_unusedSupportPropertyTimer.setSingleShot(true);
    connect(&m_unusedSupportPropertyTimer, &QTimer::timeout,
            this, &Compositor::deleteUnusedSupportProperties);

    // Delay the call to start by one event cycle.
    // The ctor of this class is invoked from the Workspace ctor, that means before
    // Workspace is completely constructed, so calling Workspace::self() would result
    // in undefined behavior. This is fixed by using a delayed invocation.
    QTimer::singleShot(0, this, &Compositor::start);

    // register DBus
    new CompositorDBusInterface(this);
}

Compositor::~Compositor()
{
    emit aboutToDestroy();
    stop();
    deleteUnusedSupportProperties();
    destroyCompositorSelection();
    s_compositor = nullptr;
}

bool Compositor::setupStart()
{
    if (kwinApp()->isTerminating()) {
        // Don't start while KWin is terminating. An event to restart might be lingering
        // in the event queue due to graphics reset.
        return false;
    }
    if (m_state != State::Off) {
        return false;
    }
    m_state = State::Starting;

    options->reloadCompositingSettings(true);

    setupX11Support();

    // There might still be a deleted around, needs to be cleared before
    // creating the scene (BUG 333275).
    if (Workspace::self()) {
        while (!Workspace::self()->remnants().empty()) {
            Workspace::self()->remnants().front()->remnant()->discard();
        }
    }

    emit aboutToToggleCompositing();

    auto supportedCompositors = kwinApp()->platform()->supportedCompositors();
    const auto userConfigIt = std::find(supportedCompositors.begin(), supportedCompositors.end(),
                                        options->compositingMode());

    if (userConfigIt != supportedCompositors.end()) {
        supportedCompositors.erase(userConfigIt);
        supportedCompositors.prepend(options->compositingMode());
    } else {
        qCWarning(KWIN_CORE)
                << "Configured compositor not supported by Platform. Falling back to defaults";
    }

    const auto availablePlugins = KPluginLoader::findPlugins(QStringLiteral("org.kde.kwin.scenes"));

    for (const KPluginMetaData &pluginMetaData : availablePlugins) {
        qCDebug(KWIN_CORE) << "Available scene plugin:" << pluginMetaData.fileName();
    }

    for (auto type : qAsConst(supportedCompositors)) {
        switch (type) {
        case XRenderCompositing:
            qCDebug(KWIN_CORE) << "Attempting to load the XRender scene";
            break;
        case OpenGLCompositing:
        case OpenGL2Compositing:
            qCDebug(KWIN_CORE) << "Attempting to load the OpenGL scene";
            break;
        case QPainterCompositing:
            qCDebug(KWIN_CORE) << "Attempting to load the QPainter scene";
            break;
        case NoCompositing:
            Q_UNREACHABLE();
        }
        const auto pluginIt = std::find_if(availablePlugins.begin(), availablePlugins.end(),
            [type] (const auto &plugin) {
                const auto &metaData = plugin.rawData();
                auto it = metaData.find(QStringLiteral("CompositingType"));
                if (it != metaData.end()) {
                    if ((*it).toInt() == int{type}) {
                        return true;
                    }
                }
                return false;
            });
        if (pluginIt != availablePlugins.end()) {
            std::unique_ptr<SceneFactory>
                    factory{ qobject_cast<SceneFactory*>(pluginIt->instantiate()) };
            if (factory) {
                m_scene = factory->create(this);
                if (m_scene) {
                    if (!m_scene->initFailed()) {
                        qCDebug(KWIN_CORE) << "Instantiated compositing plugin:"
                                           << pluginIt->name();
                        break;
                    } else {
                        delete m_scene;
                        m_scene = nullptr;
                    }
                }
            }
        }
    }

    if (m_scene == nullptr || m_scene->initFailed()) {
        qCCritical(KWIN_CORE) << "Failed to initialize compositing, compositing disabled";
        m_state = State::Off;

        delete m_scene;
        m_scene = nullptr;

        if (m_selectionOwner) {
            m_selectionOwner->setOwning(false);
            m_selectionOwner->release();
        }
        if (!supportedCompositors.contains(NoCompositing)) {
            qCCritical(KWIN_CORE) << "The used windowing system requires compositing";
            qCCritical(KWIN_CORE) << "We are going to quit KWin now as it is broken";
            qApp->quit();
        }
        return false;
    }

    CompositingType compositingType = m_scene->compositingType();
    if (compositingType & OpenGLCompositing) {
        // Override for OpenGl sub-type OpenGL2Compositing.
        compositingType = OpenGLCompositing;
    }
    kwinApp()->platform()->setSelectedCompositor(compositingType);

    if (!Workspace::self() && m_scene && m_scene->compositingType() == QPainterCompositing) {
        // Force Software QtQuick on first startup with QPainter.
        QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);
    }

    connect(m_scene, &Scene::resetCompositing, this, &Compositor::reinitialize);
    emit sceneCreated();

    return true;
}

void Compositor::claimCompositorSelection()
{
    if (!m_selectionOwner) {
        char selection_name[ 100 ];
        sprintf(selection_name, "_NET_WM_CM_S%d", Application::x11ScreenNumber());
        m_selectionOwner = new CompositorSelectionOwner(selection_name);
        connect(m_selectionOwner, &CompositorSelectionOwner::lostOwnership,
                this, &Compositor::stop);
    }

    if (!m_selectionOwner) {
        // No X11 yet.
        return;
    }
    if (!m_selectionOwner->owning()) {
        // Force claim ownership.
        m_selectionOwner->claim(true);
        m_selectionOwner->setOwning(true);
    }
}

void Compositor::setupX11Support()
{
    auto *con = kwinApp()->x11Connection();
    if (!con) {
        delete m_selectionOwner;
        m_selectionOwner = nullptr;
        return;
    }
    claimCompositorSelection();
    xcb_composite_redirect_subwindows(con, kwinApp()->x11RootWindow(),
                                      XCB_COMPOSITE_REDIRECT_MANUAL);
}

void Compositor::startupWithWorkspace()
{
    connect(kwinApp(), &Application::x11ConnectionChanged,
            this, &Compositor::setupX11Support, Qt::UniqueConnection);
    Workspace::self()->markXStackingOrderAsDirty();
    Q_ASSERT(m_scene);

    connect(workspace(), &Workspace::destroyed, this, [this] { compositeTimer.stop(); });
    setupX11Support();

    // Sets also the 'effects' pointer.
    kwinApp()->platform()->createEffectsHandler(this, m_scene);
    connect(Workspace::self(), &Workspace::deletedRemoved, m_scene, &Scene::removeToplevel);
    connect(effects, &EffectsHandler::screenGeometryChanged, this, &Compositor::addRepaintFull);

    for (auto& client : Workspace::self()->windows()) {
        if (client->remnant()) {
            continue;
        }
        client->setupCompositing(!client->control);
        if (!win::is_desktop(client)) {
            win::update_shadow(client);
        }
    }

    m_state = State::On;
    emit compositingToggled(true);

    if (m_releaseSelectionTimer.isActive()) {
        m_releaseSelectionTimer.stop();
    }

    // Render at least once.
    addRepaintFull();
    performCompositing();
}

void Compositor::schedule_repaint()
{
    if (m_state != State::On) {
        return;
    }

    // Don't repaint if all outputs are disabled
    if (!kwinApp()->platform()->areOutputsEnabled()) {
        return;
    }

    // TODO: Make this distinction not on the question if there is a swap event but if per screen
    //       rendering? On X we get swap events but they are aligned with the "wrong" screen if
    //       it the primary/first one is not the one with the highest refresh rate.
    //       But on the other side Present extension does not allow to sync with another screen
    //       anyway.

    setCompositeTimer();
}

void Compositor::schedule_repaint([[maybe_unused]] Toplevel* window)
{
    schedule_repaint();
}

void Compositor::stop()
{
    if (m_state == State::Off || m_state == State::Stopping) {
        return;
    }
    m_state = State::Stopping;
    emit aboutToToggleCompositing();

    m_releaseSelectionTimer.start();

    // Some effects might need access to effect windows when they are about to
    // be destroyed, for example to unreference deleted windows, so we have to
    // make sure that effect windows outlive effects.
    delete effects;
    effects = nullptr;

    if (Workspace::self()) {
        for (auto& c : Workspace::self()->windows()) {
            if (c->remnant()) {
                continue;
            }
            m_scene->removeToplevel(c);
            c->finishCompositing();
        }

        if (auto *con = kwinApp()->x11Connection()) {
            xcb_composite_unredirect_subwindows(con, kwinApp()->x11RootWindow(),
                                                XCB_COMPOSITE_REDIRECT_MANUAL);
        }
        while (!workspace()->remnants().empty()) {
            workspace()->remnants().front()->remnant()->discard();
        }
    }

    delete m_scene;
    m_scene = nullptr;
    m_bufferSwapPending = false;
    compositeTimer.stop();
    repaints_region = QRegion();

    m_state = State::Off;
    emit compositingToggled(false);
}

void Compositor::destroyCompositorSelection()
{
    delete m_selectionOwner;
    m_selectionOwner = nullptr;
}

void Compositor::releaseCompositorSelection()
{
    switch (m_state) {
    case State::On:
        // We are compositing at the moment. Don't release.
        break;
    case State::Off:
        if (m_selectionOwner) {
            qCDebug(KWIN_CORE) << "Releasing compositor selection";
            m_selectionOwner->setOwning(false);
            m_selectionOwner->release();
        }
        break;
    case State::Starting:
    case State::Stopping:
        // Still starting or shutting down the compositor. Starting might fail
        // or after stopping a restart might follow. So test again later on.
        m_releaseSelectionTimer.start();
        break;
    }
}

void Compositor::keepSupportProperty(xcb_atom_t atom)
{
    m_unusedSupportProperties.removeAll(atom);
}

void Compositor::removeSupportProperty(xcb_atom_t atom)
{
    m_unusedSupportProperties << atom;
    m_unusedSupportPropertyTimer.start();
}

void Compositor::deleteUnusedSupportProperties()
{
    if (m_state == State::Starting || m_state == State::Stopping) {
        // Currently still maybe restarting the compositor.
        m_unusedSupportPropertyTimer.start();
        return;
    }
    if (auto *con = kwinApp()->x11Connection()) {
        for (const xcb_atom_t &atom : qAsConst(m_unusedSupportProperties)) {
            // remove property from root window
            xcb_delete_property(con, kwinApp()->x11RootWindow(), atom);
        }
        m_unusedSupportProperties.clear();
    }
}

void Compositor::configChanged()
{
    reinitialize();
    addRepaintFull();
}

void Compositor::reinitialize()
{
    // Reparse config. Config options will be reloaded by start()
    kwinApp()->config()->reparseConfiguration();

    // Restart compositing
    stop();
    start();

    if (effects) { // start() may fail
        effects->reconfigure();
    }
}

void Compositor::addRepaint(int x, int y, int w, int h)
{
    addRepaint(QRegion(x, y, w, h));
}

void Compositor::addRepaint(QRect const& rect)
{
    addRepaint(QRegion(rect));
}

void Compositor::addRepaint(QRegion const& region)
{
    if (m_state != State::On) {
        return;
    }
    repaints_region += region;
    schedule_repaint();
}

void Compositor::addRepaintFull()
{
    auto const size = screens()->size();
    addRepaint(QRegion(0, 0, size.width(), size.height()));
}

void Compositor::timerEvent(QTimerEvent *te)
{
    if (te->timerId() == compositeTimer.timerId()) {
        performCompositing();
    } else
        QObject::timerEvent(te);
}

void Compositor::aboutToSwapBuffers()
{
    Q_ASSERT(!m_bufferSwapPending);
    m_bufferSwapPending = true;
}

void Compositor::bufferSwapComplete(bool present)
{
    Q_UNUSED(present)

    if (!m_bufferSwapPending) {
        qDebug() << "KWin::Compositor::bufferSwapComplete() called but m_bufferSwapPending is false";
        return;
    }
    m_bufferSwapPending = false;

    // We delay the next paint shortly before next vblank. For that we assume that the swap
    // event is close to the actual vblank (TODO: it would be better to take the actual flip
    // time that for example DRM events provide). We take 10% of refresh cycle length.
    // We also assume the paint duration is relatively constant over time. We take 3 times the
    // previous paint duration.
    //
    // All temporary calculations are in nanoseconds but the final timer offset in the end in
    // milliseconds. Atleast we take here one millisecond.
    const qint64 refresh = refreshLength();
    const qint64 vblankMargin = refresh / 10;

    auto maxPaintDuration = [this]() {
        if (m_lastPaintDurations[0] > m_lastPaintDurations[1]) {
            return m_lastPaintDurations[0];
        }
        return m_lastPaintDurations[1];
    };
    auto const paintMargin = maxPaintDuration();
    m_delay = qMax(refresh - vblankMargin - paintMargin, qint64(0));

    compositeTimer.stop();
    setCompositeTimer();
}

void WaylandCompositor::addRepaint(QRegion const& region)
{
    if (!isActive()) {
        return;
    }
    for (auto& [key, output] : outputs) {
        output->add_repaint(region);
    }
}

void WaylandCompositor::check_idle()
{
    for (auto& [key, output] : outputs) {
        if (!output->idle) {
            return;
        }
    }
    scene()->idle();
}

void WaylandCompositor::swapped(AbstractWaylandOutput* output)
{
    auto render_output = outputs.at(output).get();
    render_output->swapped_sw();
}

void WaylandCompositor::swapped(AbstractWaylandOutput* output, unsigned int sec, unsigned int usec)
{
    auto render_output = outputs.at(output).get();
    render_output->swapped_hw(sec, usec);
}

bool Compositor::prepare_composition(QRegion& repaints, std::deque<Toplevel*>& windows)
{
    compositeTimer.stop();

    // If a buffer swap is still pending, we return to the event loop and
    // continue processing events until the swap has completed.
    if (m_bufferSwapPending) {
        return false;
    }

    // If outputs are disabled, we return to the event loop and
    // continue processing events until the outputs are enabled again
    if (!kwinApp()->platform()->areOutputsEnabled()) {
        return false;
    }

    // Create a list of all windows in the stacking order
    windows = Workspace::self()->xStackingOrder();
    std::vector<Toplevel*> damaged;

    // Reset the damage state of each window and fetch the damage region
    // without waiting for a reply
    for (auto win : windows) {
        if (win->resetAndFetchDamage()) {
            damaged.push_back(win);
        }
    }

    if (damaged.size() > 0) {
        m_scene->triggerFence();
        if (auto c = kwinApp()->x11Connection()) {
            xcb_flush(c);
        }
    }

    // Move elevated windows to the top of the stacking order
    for (EffectWindow *c : static_cast<EffectsHandlerImpl *>(effects)->elevatedWindows()) {
        auto t = static_cast<EffectWindowImpl *>(c)->window();
        remove_all(windows, t);
        windows.push_back(t);
    }

    // Get the replies
    for (auto win : damaged) {
        // Discard the cached lanczos texture
        if (win->transient()->annexed) {
            win = win::lead_of_annexed_transient(win);
        }
        if (win->effectWindow()) {
            const QVariant texture = win->effectWindow()->data(LanczosCacheRole);
            if (texture.isValid()) {
                delete static_cast<GLTexture *>(texture.value<void*>());
                win->effectWindow()->setData(LanczosCacheRole, QVariant());
            }
        }

        win->getDamageRegionReply();
    }

    if (auto const& wins = workspace()->windows();
        repaints_region.isEmpty() && !std::any_of(wins.cbegin(), wins.cend(), [](auto const& win) {
            return win->has_pending_repaints();
        })) {
        m_scene->idle();

        // This means the next time we composite it is done without timer delay.
        m_delay = 0;
        return false;
    }

    // Skip windows that are not yet ready for being painted and if screen is locked skip windows
    // that are neither lockscreen nor inputmethod windows.
    //
    // TODO? This cannot be used so carelessly - needs protections against broken clients, the
    // window should not get focus before it's displayed, handle unredirected windows properly and
    // so on.
    for (auto win : windows) {
        if (!win->readyForPainting()) {
            windows.erase(std::remove(windows.begin(), windows.end(), win), windows.end());
        }
        if (waylandServer() && waylandServer()->isScreenLocked()) {
            if(!win->isLockScreen() && !win->isInputMethod()) {
                windows.erase(std::remove(windows.begin(), windows.end(), win), windows.end());
            }
        }
    }

    repaints = repaints_region;
    // clear all repaints, so that post-pass can add repaints for the next repaint
    repaints_region = QRegion();

    return true;
}

void Compositor::update_paint_periods(int64_t duration)
{
    if (duration > m_lastPaintDurations[1]) {
        m_lastPaintDurations[1] = duration;
    }

    m_paintPeriods++;

    // We take the maximum over the last 100 frames.
    if (m_paintPeriods == 100) {
        m_lastPaintDurations[0] = m_lastPaintDurations[1];
        m_lastPaintDurations[1] = 0;
        m_paintPeriods = 0;
    }
}

void Compositor::retard_next_composition()
{
    if (m_scene->hasSwapEvent()) {
        // We wait on an explicit callback from the backend to unlock next composition runs.
        return;
    }
    m_delay = refreshLength();
    setCompositeTimer();
}

qint64 Compositor::refreshLength() const
{
    return 1000 * 1000 / qint64(refreshRate());
}

void Compositor::setCompositeTimer()
{
    if (compositeTimer.isActive() || m_bufferSwapPending) {
        // Abort since we will composite when the timer runs out or the timer will only get
        // started at buffer swap.
        return;
    }

    // In milliseconds.
    const uint waitTime = m_delay / 1000 / 1000;
    Perf::Ftrace::mark(QStringLiteral("timer ") + QString::number(waitTime));

    // Force 4fps minimum:
    compositeTimer.start(qMin(waitTime, 250u), this);
}

bool Compositor::isActive()
{
    return m_state == State::On;
}

WaylandCompositor::WaylandCompositor(QObject *parent)
    : Compositor(parent)
    , presentation(new Presentation(this))
{
    if (!presentation->initClock(kwinApp()->platform()->supportsClockId(),
                                   kwinApp()->platform()->clockId())) {
        qCCritical(KWIN_CORE) << "Presentation clock failed. Exit.";
        qApp->quit();
    }
    connect(kwinApp(), &Application::x11ConnectionAboutToBeDestroyed,
            this, &WaylandCompositor::destroyCompositorSelection);

    for (auto output : kwinApp()->platform()->enabledOutputs()) {
        auto wl_out = static_cast<AbstractWaylandOutput*>(output);
        outputs.emplace(wl_out, new render::wayland::output(wl_out, this));
    }

    connect(kwinApp()->platform(), &Platform::output_added, this, [this](auto output) {
        auto wl_out = static_cast<AbstractWaylandOutput*>(output);
        outputs.emplace(wl_out, new render::wayland::output(wl_out, this));
    });

    connect(kwinApp()->platform(), &Platform::output_removed, this, [this](auto output) {
        for (auto it=outputs.begin(); it!=outputs.end(); ++it) {
            if (it->first == output) {
                outputs.erase(it);
                break;
            }
        }
        if (auto workspace = Workspace::self()) {
            for (auto& win : workspace->windows()) {
                remove_all(win->repaint_outputs, output);
            }
        }
    });

    connect(workspace(), &Workspace::destroyed, this, [this] {
        for (auto& [key, output] : outputs) {
            output->delay_timer.stop();
        }
    });
}

WaylandCompositor::~WaylandCompositor() = default;

void WaylandCompositor::schedule_repaint(Toplevel* window)
{
    if (!isActive()) {
        return;
    }

    if (!kwinApp()->platform()->areOutputsEnabled()) {
        return;
    }

    for (auto& [base, output] : outputs) {
        if (!win::visible_rect(window).intersected(base->geometry()).isEmpty()) {
            output->set_delay_timer();
        }
    }
}

void WaylandCompositor::toggleCompositing()
{
    // For the shortcut. Not possible on Wayland because we always composite.
}

void WaylandCompositor::start()
{
    if (!Compositor::setupStart()) {
        // Internal setup failed, abort.
        return;
    }

    if (Workspace::self()) {
        startupWithWorkspace();
    } else {
        connect(kwinApp(), &Application::workspaceCreated,
                this, &WaylandCompositor::startupWithWorkspace);
    }
}

std::deque<Toplevel*> WaylandCompositor::performCompositing()
{
    for (auto& [output, render_output] : outputs) {
        render_output->run();
    }

    return std::deque<Toplevel*>();
}

int Compositor::refreshRate() const
{
    int max_refresh_rate = 60000;
    for (auto output : kwinApp()->platform()->outputs()) {
        auto const rate = output->refreshRate();
        if (rate > max_refresh_rate) {
            max_refresh_rate = rate;
        }
    }
    return max_refresh_rate;
}

X11Compositor::X11Compositor(QObject *parent)
    : Compositor(parent)
    , m_suspended(options->isUseCompositing() ? NoReasonSuspend : UserSuspend)
{
    if (qEnvironmentVariableIsSet("KWIN_MAX_FRAMES_TESTED")) {
       m_framesToTestForSafety = qEnvironmentVariableIntValue("KWIN_MAX_FRAMES_TESTED");
    }
}

void X11Compositor::toggleCompositing()
{
    if (m_suspended) {
        // Direct user call; clear all bits.
        resume(AllReasonSuspend);
    } else {
        // But only set the user one (sufficient to suspend).
        suspend(UserSuspend);
    }
}

void X11Compositor::reinitialize()
{
    // Resume compositing if suspended.
    m_suspended = NoReasonSuspend;
    Compositor::reinitialize();
}

void X11Compositor::configChanged()
{
    if (m_suspended) {
        stop();
        return;
    }
    Compositor::configChanged();
}

void X11Compositor::suspend(X11Compositor::SuspendReason reason)
{
    Q_ASSERT(reason != NoReasonSuspend);
    m_suspended |= reason;

    if (reason & ScriptSuspend) {
        // When disabled show a shortcut how the user can get back compositing.
        const auto shortcuts = KGlobalAccel::self()->shortcut(
            workspace()->findChild<QAction*>(QStringLiteral("Suspend Compositing")));
        if (!shortcuts.isEmpty()) {
            // Display notification only if there is the shortcut.
            const QString message =
                    i18n("Desktop effects have been suspended by another application.<br/>"
                         "You can resume using the '%1' shortcut.",
                         shortcuts.first().toString(QKeySequence::NativeText));
            KNotification::event(QStringLiteral("compositingsuspendeddbus"), message);
        }
    }
    stop();
}

void X11Compositor::resume(X11Compositor::SuspendReason reason)
{
    Q_ASSERT(reason != NoReasonSuspend);
    m_suspended &= ~reason;
    start();
}

void X11Compositor::start()
{
    if (m_suspended) {
        QStringList reasons;
        if (m_suspended & UserSuspend) {
            reasons << QStringLiteral("Disabled by User");
        }
        if (m_suspended & BlockRuleSuspend) {
            reasons << QStringLiteral("Disabled by Window");
        }
        if (m_suspended & ScriptSuspend) {
            reasons << QStringLiteral("Disabled by Script");
        }
        qCDebug(KWIN_CORE) << "Compositing is suspended, reason:" << reasons;
        return;
    } else if (!kwinApp()->platform()->compositingPossible()) {
        qCCritical(KWIN_CORE) << "Compositing is not possible";
        return;
    }
    if (!Compositor::setupStart()) {
        // Internal setup failed, abort.
        return;
    }
    startupWithWorkspace();
}
std::deque<Toplevel*> X11Compositor::performCompositing()
{
    if (scene()->usesOverlayWindow() && !isOverlayWindowVisible()) {
        // Return since nothing is visible.
        return std::deque<Toplevel*>();
    }

    QRegion repaints;
    std::deque<Toplevel*> windows;

    if (!prepare_composition(repaints, windows)) {
        return std::deque<Toplevel*>();
    }

    Perf::Ftrace::begin(QStringLiteral("Paint"), ++s_msc);
    create_opengl_safepoint(OpenGLSafePoint::PreFrame);

    auto now_ns = std::chrono::steady_clock::now().time_since_epoch();
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(now_ns);

    // Start the actual painting process.
    auto const duration = scene()->paint(repaints, windows, now);

    update_paint_periods(duration);
    create_opengl_safepoint(OpenGLSafePoint::PostFrame);
    retard_next_composition();

    Perf::Ftrace::end(QStringLiteral("Paint"), s_msc);

    return windows;
}

void X11Compositor::create_opengl_safepoint(OpenGLSafePoint safepoint)
{
    if (m_framesToTestForSafety <= 0) {
        return;
    }
    if (!(scene()->compositingType() & OpenGLCompositing)) {
        return;
    }

    kwinApp()->platform()->createOpenGLSafePoint(safepoint);

    if (safepoint == OpenGLSafePoint::PostFrame) {
        if (--m_framesToTestForSafety == 0) {
            kwinApp()->platform()->createOpenGLSafePoint(OpenGLSafePoint::PostLastGuardedFrame);
        }
    }
}

bool X11Compositor::checkForOverlayWindow(WId w) const
{
    if (!scene()) {
        // No scene, so it cannot be the overlay window.
        return false;
    }
    if (!scene()->overlayWindow()) {
        // No overlay window, it cannot be the overlay.
        return false;
    }
    // Compare the window ID's.
    return w == scene()->overlayWindow()->window();
}

bool X11Compositor::isOverlayWindowVisible() const
{
    if (!scene()) {
        return false;
    }
    if (!scene()->overlayWindow()) {
        return false;
    }
    return scene()->overlayWindow()->isVisible();
}

void X11Compositor::updateClientCompositeBlocking(Toplevel* window)
{
    if (window) {
        if (window->isBlockingCompositing()) {
            // Do NOT attempt to call suspend(true) from within the eventchain!
            if (!(m_suspended & BlockRuleSuspend))
                QMetaObject::invokeMethod(this, [this]() {
                        suspend(BlockRuleSuspend);
                    }, Qt::QueuedConnection);
        }
    }
    else if (m_suspended & BlockRuleSuspend) {
        // If !c we just check if we can resume in case a blocking client was lost.
        bool shouldResume = true;

        for (auto const& client : Workspace::self()->allClientList()) {
            if (client->isBlockingCompositing()) {
                shouldResume = false;
                break;
            }
        }
        if (shouldResume) {
            // Do NOT attempt to call suspend(false) from within the eventchain!
                QMetaObject::invokeMethod(this, [this]() {
                        resume(BlockRuleSuspend);
                    }, Qt::QueuedConnection);
        }
    }
}

X11Compositor *X11Compositor::self()
{
    return qobject_cast<X11Compositor *>(Compositor::self());
}

}

// included for CompositorSelectionOwner
#include "composite.moc"
