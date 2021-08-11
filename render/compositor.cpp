/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2019-2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor.h"

#include "abstract_output.h"
#include "dbusinterface.h"
#include "effects.h"
#include "perf/ftrace.h"
#include "platform.h"
#include "scene.h"
#include "screens.h"
#include "utils.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/net.h"
#include "win/remnant.h"
#include "win/scene.h"
#include "win/stacking_order.h"
#include "win/x11/stacking_tree.h"
#include "x11/compositor_selection_owner.h"

#include <KPluginLoader>
#include <KPluginMetaData>

#include <QQuickWindow>
#include <QTimerEvent>

namespace KWin::render
{

// 2 sec which should be enough to restart the compositor.
constexpr auto compositor_lost_message_delay = 2000;

compositor* compositor::self()
{
    return kwinApp()->compositor;
}

bool compositor::compositing()
{
    return kwinApp()->compositor != nullptr && kwinApp()->compositor->isActive();
}

compositor::compositor(QObject* workspace)
    : QObject(workspace)
    , m_state(State::Off)
    , m_selectionOwner(nullptr)
    , m_delay(0)
    , m_bufferSwapPending(false)
    , m_scene(nullptr)
{
    connect(options, &Options::configChanged, this, &compositor::configChanged);
    connect(options, &Options::animationSpeedChanged, this, &compositor::configChanged);

    m_unusedSupportPropertyTimer.setInterval(compositor_lost_message_delay);
    m_unusedSupportPropertyTimer.setSingleShot(true);
    connect(&m_unusedSupportPropertyTimer,
            &QTimer::timeout,
            this,
            &compositor::deleteUnusedSupportProperties);

    // Delay the call to start by one event cycle.
    // The ctor of this class is invoked from the Workspace ctor, that means before
    // Workspace is completely constructed, so calling Workspace::self() would result
    // in undefined behavior. This is fixed by using a delayed invocation.
    QTimer::singleShot(0, this, &compositor::start);

    // register DBus
    new CompositorDBusInterface(this);
}

compositor::~compositor()
{
    Q_EMIT aboutToDestroy();
    stop();
    deleteUnusedSupportProperties();
    destroyCompositorSelection();
    kwinApp()->compositor = nullptr;
}

bool compositor::setupStart()
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

    Q_EMIT aboutToToggleCompositing();

    auto supportedCompositors = kwinApp()->platform()->supportedCompositors();
    const auto userConfigIt = std::find(
        supportedCompositors.begin(), supportedCompositors.end(), options->compositingMode());

    if (userConfigIt != supportedCompositors.end()) {
        supportedCompositors.erase(userConfigIt);
        supportedCompositors.prepend(options->compositingMode());
    } else {
        qCWarning(KWIN_CORE)
            << "Configured compositor not supported by Platform. Falling back to defaults";
    }

    const auto availablePlugins = KPluginLoader::findPlugins(QStringLiteral("org.kde.kwin.scenes"));

    for (const KPluginMetaData& pluginMetaData : availablePlugins) {
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
        const auto pluginIt = std::find_if(
            availablePlugins.begin(), availablePlugins.end(), [type](const auto& plugin) {
                const auto& metaData = plugin.rawData();
                auto it = metaData.find(QStringLiteral("CompositingType"));
                if (it != metaData.end()) {
                    if ((*it).toInt() == int{type}) {
                        return true;
                    }
                }
                return false;
            });
        if (pluginIt != availablePlugins.end()) {
            std::unique_ptr<SceneFactory> factory{
                qobject_cast<SceneFactory*>(pluginIt->instantiate())};
            if (factory) {
                m_scene = factory->create(this);
                if (m_scene) {
                    if (!m_scene->initFailed()) {
                        qCDebug(KWIN_CORE)
                            << "Instantiated compositing plugin:" << pluginIt->name();
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
            m_selectionOwner->disown();
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

    connect(m_scene, &Scene::resetCompositing, this, &compositor::reinitialize);
    Q_EMIT sceneCreated();

    return true;
}

void compositor::claimCompositorSelection()
{
    using CompositorSelectionOwner = x11::compositor_selection_owner;

    if (!m_selectionOwner) {
        char selection_name[100];
        sprintf(selection_name, "_NET_WM_CM_S%d", kwinApp()->x11ScreenNumber());
        m_selectionOwner = new CompositorSelectionOwner(selection_name);
        connect(
            m_selectionOwner, &CompositorSelectionOwner::lostOwnership, this, &compositor::stop);
    }

    if (!m_selectionOwner) {
        // No X11 yet.
        return;
    }

    m_selectionOwner->own();
}

void compositor::setupX11Support()
{
    auto con = kwinApp()->x11Connection();
    if (!con) {
        delete m_selectionOwner;
        m_selectionOwner = nullptr;
        return;
    }
    claimCompositorSelection();
    xcb_composite_redirect_subwindows(
        con, kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
}

void compositor::startupWithWorkspace()
{
    connect(kwinApp(),
            &Application::x11ConnectionChanged,
            this,
            &compositor::setupX11Support,
            Qt::UniqueConnection);
    workspace()->x_stacking_tree->mark_as_dirty();
    assert(m_scene);

    connect(
        workspace(),
        &Workspace::destroyed,
        this,
        [this] { compositeTimer.stop(); },
        Qt::UniqueConnection);
    setupX11Support();

    // Sets also the 'effects' pointer.
    kwinApp()->platform()->createEffectsHandler(this, m_scene);
    connect(Workspace::self(), &Workspace::deletedRemoved, m_scene, &Scene::removeToplevel);
    connect(effects, &EffectsHandler::screenGeometryChanged, this, &compositor::addRepaintFull);
    connect(workspace()->stacking_order, &win::stacking_order::unlocked, this, []() {
        if (auto eff_impl = static_cast<EffectsHandlerImpl*>(effects)) {
            eff_impl->checkInputWindowStacking();
        }
    });
    connect(workspace()->stacking_order,
            &win::stacking_order::changed,
            this,
            &compositor::addRepaintFull);

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
    Q_EMIT compositingToggled(true);

    // Render at least once.
    addRepaintFull();
    performCompositing();
}

void compositor::schedule_repaint()
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

void compositor::schedule_repaint([[maybe_unused]] Toplevel* window)
{
    schedule_repaint();
}

void compositor::stop()
{
    if (m_state == State::Off || m_state == State::Stopping) {
        return;
    }
    m_state = State::Stopping;
    Q_EMIT aboutToToggleCompositing();

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

        if (auto con = kwinApp()->x11Connection()) {
            xcb_composite_unredirect_subwindows(
                con, kwinApp()->x11RootWindow(), XCB_COMPOSITE_REDIRECT_MANUAL);
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
    Q_EMIT compositingToggled(false);
}

void compositor::destroyCompositorSelection()
{
    delete m_selectionOwner;
    m_selectionOwner = nullptr;
}

void compositor::keepSupportProperty(xcb_atom_t atom)
{
    m_unusedSupportProperties.removeAll(atom);
}

void compositor::removeSupportProperty(xcb_atom_t atom)
{
    m_unusedSupportProperties << atom;
    m_unusedSupportPropertyTimer.start();
}

void compositor::deleteUnusedSupportProperties()
{
    if (m_state == State::Starting || m_state == State::Stopping) {
        // Currently still maybe restarting the compositor.
        m_unusedSupportPropertyTimer.start();
        return;
    }
    if (auto con = kwinApp()->x11Connection()) {
        for (auto const& atom : qAsConst(m_unusedSupportProperties)) {
            // remove property from root window
            xcb_delete_property(con, kwinApp()->x11RootWindow(), atom);
        }
        m_unusedSupportProperties.clear();
    }
}

void compositor::configChanged()
{
    reinitialize();
    addRepaintFull();
}

void compositor::reinitialize()
{
    // Reparse config. Config options will be reloaded by start()
    kwinApp()->config()->reparseConfiguration();

    // Restart compositing
    stop();
    start();

    if (effects) {
        // start() may fail
        effects->reconfigure();
    }
}

void compositor::addRepaint(int x, int y, int w, int h)
{
    addRepaint(QRegion(x, y, w, h));
}

void compositor::addRepaint(QRect const& rect)
{
    addRepaint(QRegion(rect));
}

void compositor::addRepaint([[maybe_unused]] QRegion const& region)
{
}

void compositor::addRepaintFull()
{
    auto const size = screens()->size();
    addRepaint(QRegion(0, 0, size.width(), size.height()));
}

void compositor::timerEvent(QTimerEvent* te)
{
    if (te->timerId() == compositeTimer.timerId()) {
        performCompositing();
    } else
        QObject::timerEvent(te);
}

void compositor::aboutToSwapBuffers()
{
    assert(!m_bufferSwapPending);
    m_bufferSwapPending = true;
}

void compositor::bufferSwapComplete(bool present)
{
    Q_UNUSED(present)

    if (!m_bufferSwapPending) {
        qDebug()
            << "KWin::Compositor::bufferSwapComplete() called but m_bufferSwapPending is false";
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

void compositor::update_paint_periods(int64_t duration)
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

void compositor::retard_next_composition()
{
    if (m_scene->hasSwapEvent()) {
        // We wait on an explicit callback from the backend to unlock next composition runs.
        return;
    }
    m_delay = refreshLength();
    setCompositeTimer();
}

qint64 compositor::refreshLength() const
{
    return 1000 * 1000 / qint64(refreshRate());
}

void compositor::setCompositeTimer()
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

bool compositor::isActive()
{
    return m_state == State::On;
}

int compositor::refreshRate() const
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

}
