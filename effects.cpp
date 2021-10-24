/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

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

#include "effects.h"

#include "abstract_output.h"
#include "effectsadaptor.h"
#include "effectloader.h"
#ifdef KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif
#include "input/cursor.h"
#include "osd.h"
#include "input/pointer_redirect.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "screenedge.h"
#include "scripting/scriptedeffect.h"
#include "screens.h"
#include "screenlockerwatcher.h"
#include "thumbnailitem.h"
#include "virtualdesktops.h"
#include "workspace.h"
#include "kwinglutils.h"
#include "kwineffectquickview.h"

#include "win/control.h"
#include "win/internal_client.h"
#include "win/meta.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/x11/group.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/window.h"
#include "win/x11/window_property_notify_x11_filter.h"

#include <QDebug>

#include <Plasma/Theme>

#include "render/compositor.h"
#include "xcbutils.h"
#include "platform.h"

#include "decorations/decorationbridge.h"
#include <KDecoration2/DecorationSettings>

namespace KWin
{
//---------------------
// Static

static QByteArray readWindowProperty(xcb_window_t win, xcb_atom_t atom, xcb_atom_t type, int format)
{
    if (win == XCB_WINDOW_NONE) {
        return QByteArray();
    }
    uint32_t len = 32768;
    for (;;) {
        Xcb::Property prop(false, win, atom, XCB_ATOM_ANY, 0, len);
        if (prop.isNull()) {
            // get property failed
            return QByteArray();
        }
        if (prop->bytes_after > 0) {
            len *= 2;
            continue;
        }
        return prop.toByteArray(format, type);
    }
}

static void deleteWindowProperty(xcb_window_t win, long int atom)
{
    if (win == XCB_WINDOW_NONE) {
        return;
    }
    xcb_delete_property(kwinApp()->x11Connection(), win, atom);
}

static xcb_atom_t registerSupportProperty(const QByteArray &propertyName)
{
    auto c = kwinApp()->x11Connection();
    if (!c) {
        return XCB_ATOM_NONE;
    }
    // get the atom for the propertyName
    ScopedCPointer<xcb_intern_atom_reply_t> atomReply(xcb_intern_atom_reply(c,
        xcb_intern_atom_unchecked(c, false, propertyName.size(), propertyName.constData()),
        nullptr));
    if (atomReply.isNull()) {
        return XCB_ATOM_NONE;
    }
    // announce property on root window
    unsigned char dummy = 0;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, kwinApp()->x11RootWindow(), atomReply->atom, atomReply->atom, 8, 1, &dummy);
    // TODO: add to _NET_SUPPORTED
    return atomReply->atom;
}

//---------------------

EffectsHandlerImpl::EffectsHandlerImpl(render::compositor* compositor, Scene *scene)
    : EffectsHandler(scene->compositingType())
    , keyboard_grab_effect(nullptr)
    , fullscreen_effect(nullptr)
    , next_window_quad_type(EFFECT_QUAD_TYPE_START)
    , m_compositor(compositor)
    , m_scene(scene)
    , m_desktopRendering(false)
    , m_currentRenderedDesktop(0)
    , m_effectLoader(new EffectLoader(this))
    , m_trackingCursorChanges(0)
{
    qRegisterMetaType<QVector<KWin::EffectWindow*>>();
    connect(m_effectLoader, &AbstractEffectLoader::effectLoaded, this,
        [this](Effect *effect, const QString &name) {
            effect_order.insert(effect->requestedEffectChainPosition(), EffectPair(name, effect));
            loaded_effects << EffectPair(name, effect);
            effectsChanged();
        }
    );
    m_effectLoader->setConfig(kwinApp()->config());
    new EffectsAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Effects"), this);
    // init is important, otherwise causes crashes when quads are build before the first painting pass start
    m_currentBuildQuadsIterator = m_activeEffects.constEnd();

    Workspace *ws = Workspace::self();
    VirtualDesktopManager *vds = VirtualDesktopManager::self();
    connect(ws, &Workspace::showingDesktopChanged,
            this, &EffectsHandlerImpl::showingDesktopChanged);
    connect(ws, &Workspace::currentDesktopChanged, this,
        [this](int old, Toplevel *c) {
            const int newDesktop = VirtualDesktopManager::self()->current();
            if (old != 0 && newDesktop != old) {
                emit desktopChanged(old, newDesktop, c ? c->effectWindow() : nullptr);
                // TODO: remove in 4.10
                emit desktopChanged(old, newDesktop);
            }
        }
    );
    connect(ws, &Workspace::desktopPresenceChanged, this,
        [this](Toplevel *c, int old) {
            if (!c->effectWindow()) {
                return;
            }
            emit desktopPresenceChanged(c->effectWindow(), old, c->desktop());
        }
    );
    connect(ws, &Workspace::clientAdded, this,
        [this](auto c) {
            if (c->readyForPainting())
                slotClientShown(c);
            else
                connect(c, &Toplevel::windowShown, this, &EffectsHandlerImpl::slotClientShown);
        }
    );
    connect(ws, &Workspace::unmanagedAdded, this,
        [this](Toplevel* u) {
            // it's never initially ready but has synthetic 50ms delay
            connect(u, &Toplevel::windowShown, this, &EffectsHandlerImpl::slotUnmanagedShown);
        }
    );
    connect(ws, &Workspace::internalClientAdded, this,
        [this](win::InternalClient *client) {
            setupAbstractClientConnections(client);
            emit windowAdded(client->effectWindow());
        }
    );
    connect(ws, &Workspace::clientActivated, this,
        [this](KWin::Toplevel* window) {
            emit windowActivated(window ? window->effectWindow() : nullptr);
        }
    );
    connect(ws, &Workspace::deletedRemoved, this,
        [this](KWin::Toplevel* d) {
            emit windowDeleted(d->effectWindow());
            elevated_windows.removeAll(d->effectWindow());
        }
    );
    connect(ws->sessionManager(), &SessionManager::stateChanged, this,
            &KWin::EffectsHandler::sessionStateChanged);
    connect(vds, &VirtualDesktopManager::countChanged, this, &EffectsHandler::numberDesktopsChanged);
    connect(input::get_cursor(), &input::cursor::mouse_changed, this, &EffectsHandler::mouseChanged);
    connect(Screens::self(), &Screens::countChanged, this, &EffectsHandler::numberScreensChanged);
    connect(Screens::self(), &Screens::sizeChanged, this, &EffectsHandler::virtualScreenSizeChanged);
    connect(Screens::self(), &Screens::geometryChanged, this, &EffectsHandler::virtualScreenGeometryChanged);
#ifdef KWIN_BUILD_ACTIVITIES
    if (Activities *activities = Activities::self()) {
        connect(activities, &Activities::added,          this, &EffectsHandler::activityAdded);
        connect(activities, &Activities::removed,        this, &EffectsHandler::activityRemoved);
        connect(activities, &Activities::currentChanged, this, &EffectsHandler::currentActivityChanged);
    }
#endif
    connect(ws->stacking_order, &win::stacking_order::changed, this, &EffectsHandler::stackingOrderChanged);
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox *tabBox = TabBox::TabBox::self();
    connect(tabBox, &TabBox::TabBox::tabBoxAdded,    this, &EffectsHandler::tabBoxAdded);
    connect(tabBox, &TabBox::TabBox::tabBoxUpdated,  this, &EffectsHandler::tabBoxUpdated);
    connect(tabBox, &TabBox::TabBox::tabBoxClosed,   this, &EffectsHandler::tabBoxClosed);
    connect(tabBox, &TabBox::TabBox::tabBoxKeyEvent, this, &EffectsHandler::tabBoxKeyEvent);
#endif
    connect(ScreenEdges::self(), &ScreenEdges::approaching, this, &EffectsHandler::screenEdgeApproaching);
    connect(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked, this, &EffectsHandler::screenLockingChanged);
    connect(ScreenLockerWatcher::self(), &ScreenLockerWatcher::aboutToLock, this, &EffectsHandler::screenAboutToLock);

    connect(kwinApp(), &Application::x11ConnectionChanged, this,
        [this] {
            registered_atoms.clear();
            for (auto it = m_propertiesForEffects.keyBegin(); it != m_propertiesForEffects.keyEnd(); it++) {
                const auto atom = registerSupportProperty(*it);
                if (atom == XCB_ATOM_NONE) {
                    continue;
                }
                m_compositor->keepSupportProperty(atom);
                m_managedProperties.insert(*it, atom);
                registerPropertyType(atom, true);
            }
            if (kwinApp()->x11Connection()) {
                m_x11WindowPropertyNotify = std::make_unique<win::x11::WindowPropertyNotifyX11Filter>(this);
            } else {
                m_x11WindowPropertyNotify.reset();
            }
            emit xcbConnectionChanged();
        }
    );

    if (kwinApp()->x11Connection()) {
        m_x11WindowPropertyNotify = std::make_unique<win::x11::WindowPropertyNotifyX11Filter>(this);
    }

    // connect all clients
    for (auto& client : ws->allClientList()) {
        // TODO: Can we merge this with the one for Wayland XdgShellClients below?
        auto x11_client = qobject_cast<win::x11::window*>(client);
        if (!x11_client) {
            continue;
        }
        setupClientConnections(x11_client);
    }
    for (auto u : ws->unmanagedList()) {
        setupUnmanagedConnections(u);
    }
    for (auto window : ws->windows()) {
        if (auto internal = qobject_cast<win::InternalClient*>(window)) {
            setupAbstractClientConnections(internal);
        }
    }

    connect(kwinApp()->platform, &Platform::output_added, this, &EffectsHandlerImpl::slotOutputEnabled);
    connect(kwinApp()->platform, &Platform::output_removed, this, &EffectsHandlerImpl::slotOutputDisabled);

    const QVector<AbstractOutput *> outputs = kwinApp()->platform->enabledOutputs();
    for (AbstractOutput *output : outputs) {
        slotOutputEnabled(output);
    }

    reconfigure();
}

EffectsHandlerImpl::~EffectsHandlerImpl()
{
    unloadAllEffects();
}

void EffectsHandlerImpl::unloadAllEffects()
{
    for (const EffectPair &pair : loaded_effects) {
        destroyEffect(pair.second);
    }

    effect_order.clear();
    m_effectLoader->clear();

    effectsChanged();
}

void EffectsHandlerImpl::setupAbstractClientConnections(Toplevel* window)
{
    connect(window, &Toplevel::windowClosed, this, &EffectsHandlerImpl::slotWindowClosed);
    connect(window, static_cast<void (Toplevel::*)(KWin::Toplevel*, win::maximize_mode)>(
                &Toplevel::clientMaximizedStateChanged),
            this, &EffectsHandlerImpl::slotClientMaximized);
    connect(window, &Toplevel::clientStartUserMovedResized, this,
        [this](Toplevel *c) {
            emit windowStartUserMovedResized(c->effectWindow());
        }
    );
    connect(window, &Toplevel::clientStepUserMovedResized, this,
        [this](Toplevel *c, const QRect &geometry) {
            emit windowStepUserMovedResized(c->effectWindow(), geometry);
        }
    );
    connect(window, &Toplevel::clientFinishUserMovedResized, this,
        [this](Toplevel *c) {
            emit windowFinishUserMovedResized(c->effectWindow());
        }
    );
    connect(window, &Toplevel::opacityChanged, this, &EffectsHandlerImpl::slotOpacityChanged);
    connect(window, &Toplevel::clientMinimized, this,
        [this](Toplevel* c, bool animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                emit windowMinimized(c->effectWindow());
            }
        }
    );
    connect(window, &Toplevel::clientUnminimized, this,
        [this](Toplevel* c, bool animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                emit windowUnminimized(c->effectWindow());
            }
        }
    );
    connect(window, &Toplevel::modalChanged,         this, &EffectsHandlerImpl::slotClientModalityChanged);
    connect(window, &Toplevel::frame_geometry_changed, this, &EffectsHandlerImpl::slotGeometryShapeChanged);
    connect(window, &Toplevel::frame_geometry_changed, this, &EffectsHandlerImpl::slotFrameGeometryChanged);
    connect(window, &Toplevel::damaged,              this, &EffectsHandlerImpl::slotWindowDamaged);
    connect(window, &Toplevel::unresponsiveChanged, this,
        [this, window](bool unresponsive) {
            emit windowUnresponsiveChanged(window->effectWindow(), unresponsive);
        }
    );
    connect(window, &Toplevel::windowShown, this,
        [this](Toplevel *c) {
            emit windowShown(c->effectWindow());
        }
    );
    connect(window, &Toplevel::windowHidden, this,
        [this](Toplevel *c) {
            emit windowHidden(c->effectWindow());
        }
    );
    connect(window, &Toplevel::keepAboveChanged, this,
        [this, window](bool above) {
            Q_UNUSED(above)
            emit windowKeepAboveChanged(window->effectWindow());
        }
    );
    connect(window, &Toplevel::keepBelowChanged, this,
        [this, window](bool below) {
            Q_UNUSED(below)
            emit windowKeepBelowChanged(window->effectWindow());
        }
    );
    connect(window, &Toplevel::fullScreenChanged, this,
        [this, window]() {
            emit windowFullScreenChanged(window->effectWindow());
        }
    );
    connect(window, &Toplevel::visible_geometry_changed, this, [this, window]() {
        Q_EMIT windowExpandedGeometryChanged(window->effectWindow());
    });
}

void EffectsHandlerImpl::setupClientConnections(win::x11::window *c)
{
    setupAbstractClientConnections(c);
    connect(c, &win::x11::window::paddingChanged, this, &EffectsHandlerImpl::slotPaddingChanged);
}

void EffectsHandlerImpl::setupUnmanagedConnections(Toplevel* u)
{
    connect(u, &Toplevel::windowClosed,         this, &EffectsHandlerImpl::slotWindowClosed);
    connect(u, &Toplevel::opacityChanged,       this, &EffectsHandlerImpl::slotOpacityChanged);
    connect(u, &Toplevel::frame_geometry_changed, this, &EffectsHandlerImpl::slotGeometryShapeChanged);
    connect(u, &Toplevel::frame_geometry_changed, this, &EffectsHandlerImpl::slotFrameGeometryChanged);
    connect(u, &Toplevel::paddingChanged,       this, &EffectsHandlerImpl::slotPaddingChanged);
    connect(u, &Toplevel::damaged,              this, &EffectsHandlerImpl::slotWindowDamaged);
    connect(u, &Toplevel::visible_geometry_changed, this, [this, u]() {
        Q_EMIT windowExpandedGeometryChanged(u->effectWindow());
    });
}

void EffectsHandlerImpl::reconfigure()
{
    m_effectLoader->queryAndLoadAll();
}

// the idea is that effects call this function again which calls the next one
void EffectsHandlerImpl::prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->prePaintScreen(data, presentTime);
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void EffectsHandlerImpl::paintScreen(int mask, const QRegion &region, ScreenPaintData& data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->paintScreen(mask, region, data);
        --m_currentPaintScreenIterator;
    } else
        m_scene->finalPaintScreen(mask, region, data);
}

void EffectsHandlerImpl::paintDesktop(int desktop, int mask, QRegion region, ScreenPaintData &data)
{
    if (desktop < 1 || desktop > numberOfDesktops()) {
        return;
    }
    m_currentRenderedDesktop = desktop;
    m_desktopRendering = true;
    // save the paint screen iterator
    EffectsIterator savedIterator = m_currentPaintScreenIterator;
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
    effects->paintScreen(mask, region, data);
    // restore the saved iterator
    m_currentPaintScreenIterator = savedIterator;
    m_desktopRendering = false;
}

void EffectsHandlerImpl::postPaintScreen()
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->postPaintScreen();
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void EffectsHandlerImpl::prePaintWindow(EffectWindow* w, WindowPrePaintData& data, std::chrono::milliseconds presentTime)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->prePaintWindow(w, data, presentTime);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

void EffectsHandlerImpl::paintWindow(EffectWindow* w, int mask, const QRegion &region, WindowPaintData& data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->paintWindow(w, mask, region, data);
        --m_currentPaintWindowIterator;
    } else
        m_scene->finalPaintWindow(static_cast<EffectWindowImpl*>(w), mask, region, data);
}

void EffectsHandlerImpl::paintEffectFrame(EffectFrame* frame, const QRegion &region, double opacity, double frameOpacity)
{
    if (m_currentPaintEffectFrameIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintEffectFrameIterator++)->paintEffectFrame(frame, region, opacity, frameOpacity);
        --m_currentPaintEffectFrameIterator;
    } else {
        const EffectFrameImpl* frameImpl = static_cast<const EffectFrameImpl*>(frame);
        frameImpl->finalRender(region, opacity, frameOpacity);
    }
}

void EffectsHandlerImpl::postPaintWindow(EffectWindow* w)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->postPaintWindow(w);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

Effect *EffectsHandlerImpl::provides(Effect::Feature ef)
{
    for (int i = 0; i < loaded_effects.size(); ++i)
        if (loaded_effects.at(i).second->provides(ef))
            return loaded_effects.at(i).second;
    return nullptr;
}

void EffectsHandlerImpl::drawWindow(EffectWindow* w, int mask, const QRegion &region, WindowPaintData& data)
{
    if (m_currentDrawWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentDrawWindowIterator++)->drawWindow(w, mask, region, data);
        --m_currentDrawWindowIterator;
    } else
        m_scene->finalDrawWindow(static_cast<EffectWindowImpl*>(w), mask, region, data);
}

void EffectsHandlerImpl::buildQuads(EffectWindow* w, WindowQuadList& quadList)
{
    static bool initIterator = true;
    if (initIterator) {
        m_currentBuildQuadsIterator = m_activeEffects.constBegin();
        initIterator = false;
    }
    if (m_currentBuildQuadsIterator != m_activeEffects.constEnd()) {
        (*m_currentBuildQuadsIterator++)->buildQuads(w, quadList);
        --m_currentBuildQuadsIterator;
    }
    if (m_currentBuildQuadsIterator == m_activeEffects.constBegin())
        initIterator = true;
}

bool EffectsHandlerImpl::hasDecorationShadows() const
{
    return false;
}

bool EffectsHandlerImpl::decorationsHaveAlpha() const
{
    return true;
}

bool EffectsHandlerImpl::decorationSupportsBlurBehind() const
{
    return Decoration::DecorationBridge::self()->needsBlur();
}

// start another painting pass
void EffectsHandlerImpl::startPaint()
{
    m_activeEffects.clear();
    m_activeEffects.reserve(loaded_effects.count());
    for(QVector< KWin::EffectPair >::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->isActive()) {
            m_activeEffects << it->second;
        }
    }
    m_currentDrawWindowIterator = m_activeEffects.constBegin();
    m_currentPaintWindowIterator = m_activeEffects.constBegin();
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
    m_currentPaintEffectFrameIterator = m_activeEffects.constBegin();
}

void EffectsHandlerImpl::slotClientMaximized(Toplevel* window, win::maximize_mode maxMode)
{
    bool horizontal = false;
    bool vertical = false;
    switch (maxMode) {
    case win::maximize_mode::horizontal:
        horizontal = true;
        break;
    case win::maximize_mode::vertical:
        vertical = true;
        break;
    case win::maximize_mode::full:
        horizontal = true;
        vertical = true;
        break;
    case win::maximize_mode::restore: // fall through
    default:
        // default - nothing to do
        break;
    }
    if (auto ew = window->effectWindow()) {
        emit windowMaximizedStateChanged(ew, horizontal, vertical);
    }
}

void EffectsHandlerImpl::slotOpacityChanged(Toplevel *t, qreal oldOpacity)
{
    if (t->opacity() == oldOpacity || !t->effectWindow()) {
        return;
    }
    emit windowOpacityChanged(t->effectWindow(), oldOpacity, (qreal)t->opacity());
}

void EffectsHandlerImpl::slotClientShown(KWin::Toplevel *t)
{
    assert(qobject_cast<win::x11::window*>(t));
    auto c = static_cast<win::x11::window*>(t);
    disconnect(c, &Toplevel::windowShown, this, &EffectsHandlerImpl::slotClientShown);
    setupClientConnections(c);
    emit windowAdded(c->effectWindow());
}

void EffectsHandlerImpl::slotXdgShellClientShown(Toplevel *t)
{
    setupAbstractClientConnections(t);
    Q_EMIT windowAdded(t->effectWindow());
}

void EffectsHandlerImpl::slotUnmanagedShown(KWin::Toplevel *t)
{   // regardless, unmanaged windows are -yet?- not synced anyway
    assert(!t->control);
    setupUnmanagedConnections(t);
    Q_EMIT windowAdded(t->effectWindow());
}

void EffectsHandlerImpl::slotWindowClosed(KWin::Toplevel* c, Toplevel* remnant)
{
    c->disconnect(this);
    if (remnant) {
        emit windowClosed(c->effectWindow());
    }
}

void EffectsHandlerImpl::slotClientModalityChanged()
{
    emit windowModalityChanged(static_cast<win::x11::window*>(sender())->effectWindow());
}

void EffectsHandlerImpl::slotCurrentTabAboutToChange(EffectWindow *from, EffectWindow *to)
{
    emit currentTabAboutToChange(from, to);
}

void EffectsHandlerImpl::slotTabAdded(EffectWindow* w, EffectWindow* to)
{
    emit tabAdded(w, to);
}

void EffectsHandlerImpl::slotTabRemoved(EffectWindow *w, EffectWindow* leaderOfFormerGroup)
{
    emit tabRemoved(w, leaderOfFormerGroup);
}

void EffectsHandlerImpl::slotWindowDamaged(Toplevel* t, const QRegion& r)
{
    if (!t->effectWindow()) {
        // can happen during tear down of window
        return;
    }
    emit windowDamaged(t->effectWindow(), r);
}

void EffectsHandlerImpl::slotGeometryShapeChanged(Toplevel* t, const QRect& old)
{
    // during late cleanup effectWindow() may be already NULL
    // in some functions that may still call this
    if (t == nullptr || t->effectWindow() == nullptr)
        return;
    if (t->control && (win::is_move(t) || win::is_resize(t))) {
        // For that we have windowStepUserMovedResized.
        return;
    }
    emit windowGeometryShapeChanged(t->effectWindow(), old);
}

void EffectsHandlerImpl::slotFrameGeometryChanged(Toplevel *toplevel, const QRect &oldGeometry)
{
    // effectWindow() might be nullptr during tear down of the client.
    if (toplevel->effectWindow()) {
        emit windowFrameGeometryChanged(toplevel->effectWindow(), oldGeometry);
    }
}

void EffectsHandlerImpl::slotPaddingChanged(Toplevel* t, const QRect& old)
{
    // during late cleanup effectWindow() may be already NULL
    // in some functions that may still call this
    if (t == nullptr || t->effectWindow() == nullptr)
        return;
    emit windowPaddingChanged(t->effectWindow(), old);
}

void EffectsHandlerImpl::setActiveFullScreenEffect(Effect* e)
{
    if (fullscreen_effect == e) {
        return;
    }
    const bool activeChanged = (e == nullptr || fullscreen_effect == nullptr);
    fullscreen_effect = e;
    emit activeFullScreenEffectChanged();
    if (activeChanged) {
        emit hasActiveFullScreenEffectChanged();
    }
}

Effect* EffectsHandlerImpl::activeFullScreenEffect() const
{
    return fullscreen_effect;
}

bool EffectsHandlerImpl::hasActiveFullScreenEffect() const
{
    return fullscreen_effect;
}

bool EffectsHandlerImpl::grabKeyboard(Effect* effect)
{
    if (keyboard_grab_effect != nullptr)
        return false;
    if (!doGrabKeyboard()) {
        return false;
    }
    keyboard_grab_effect = effect;
    return true;
}

bool EffectsHandlerImpl::doGrabKeyboard()
{
    return true;
}

void EffectsHandlerImpl::ungrabKeyboard()
{
    Q_ASSERT(keyboard_grab_effect != nullptr);
    doUngrabKeyboard();
    keyboard_grab_effect = nullptr;
}

void EffectsHandlerImpl::doUngrabKeyboard()
{
}

void EffectsHandlerImpl::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (keyboard_grab_effect != nullptr)
        keyboard_grab_effect->grabbedKeyboardEvent(e);
}

void EffectsHandlerImpl::startMouseInterception(Effect *effect, Qt::CursorShape shape)
{
    if (m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.append(effect);
    if (m_grabbedMouseEffects.size() != 1) {
        return;
    }
    doStartMouseInterception(shape);
}

void EffectsHandlerImpl::doStartMouseInterception(Qt::CursorShape shape)
{
    kwinApp()->input->redirect->pointer()->setEffectsOverrideCursor(shape);
}

void EffectsHandlerImpl::stopMouseInterception(Effect *effect)
{
    if (!m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.removeAll(effect);
    if (m_grabbedMouseEffects.isEmpty()) {
        doStopMouseInterception();
    }
}

void EffectsHandlerImpl::doStopMouseInterception()
{
    kwinApp()->input->redirect->pointer()->removeEffectsOverrideCursor();
}

bool EffectsHandlerImpl::isMouseInterception() const
{
    return m_grabbedMouseEffects.count() > 0;
}


bool EffectsHandlerImpl::touchDown(qint32 id, const QPointF &pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchDown(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::touchMotion(qint32 id, const QPointF &pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchMotion(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::touchUp(qint32 id, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchUp(id, time)) {
            return true;
        }
    }
    return false;
}

void EffectsHandlerImpl::registerGlobalShortcut(const QKeySequence &shortcut, QAction *action)
{
    kwinApp()->input->redirect->registerShortcut(shortcut, action);
}

void EffectsHandlerImpl::registerPointerShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton pointerButtons, QAction *action)
{
    kwinApp()->input->redirect->registerPointerShortcut(modifiers, pointerButtons, action);
}

void EffectsHandlerImpl::registerAxisShortcut(Qt::KeyboardModifiers modifiers, PointerAxisDirection axis, QAction *action)
{
    kwinApp()->input->redirect->registerAxisShortcut(modifiers, axis, action);
}

void EffectsHandlerImpl::registerTouchpadSwipeShortcut(SwipeDirection direction, QAction *action)
{
    kwinApp()->input->redirect->registerTouchpadSwipeShortcut(direction, action);
}

void* EffectsHandlerImpl::getProxy(QString name)
{
    for (QVector< EffectPair >::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
        if ((*it).first == name)
            return (*it).second->proxy();

    return nullptr;
}

void EffectsHandlerImpl::startMousePolling()
{
    if (auto cursor = input::get_cursor()) {
        cursor->start_mouse_polling();
    }
}

void EffectsHandlerImpl::stopMousePolling()
{
    if (auto cursor = input::get_cursor()) {
        cursor->stop_mouse_polling();
    }
}

bool EffectsHandlerImpl::hasKeyboardGrab() const
{
    return keyboard_grab_effect != nullptr;
}

void EffectsHandlerImpl::desktopResized(const QSize &size)
{
    m_scene->screenGeometryChanged(size);
    emit screenGeometryChanged(size);
}

void EffectsHandlerImpl::registerPropertyType(long atom, bool reg)
{
    if (reg)
        ++registered_atoms[ atom ]; // initialized to 0 if not present yet
    else {
        if (--registered_atoms[ atom ] == 0)
            registered_atoms.remove(atom);
    }
}

xcb_atom_t EffectsHandlerImpl::announceSupportProperty(const QByteArray &propertyName, Effect *effect)
{
    PropertyEffectMap::iterator it = m_propertiesForEffects.find(propertyName);
    if (it != m_propertiesForEffects.end()) {
        // property has already been registered for an effect
        // just append Effect and return the atom stored in m_managedProperties
        if (!it.value().contains(effect)) {
            it.value().append(effect);
        }
        return m_managedProperties.value(propertyName, XCB_ATOM_NONE);
    }
    m_propertiesForEffects.insert(propertyName, QList<Effect*>() << effect);
    const auto atom = registerSupportProperty(propertyName);
    if (atom == XCB_ATOM_NONE) {
        return atom;
    }
    m_compositor->keepSupportProperty(atom);
    m_managedProperties.insert(propertyName, atom);
    registerPropertyType(atom, true);
    return atom;
}

void EffectsHandlerImpl::removeSupportProperty(const QByteArray &propertyName, Effect *effect)
{
    PropertyEffectMap::iterator it = m_propertiesForEffects.find(propertyName);
    if (it == m_propertiesForEffects.end()) {
        // property is not registered - nothing to do
        return;
    }
    if (!it.value().contains(effect)) {
        // property is not registered for given effect - nothing to do
        return;
    }
    it.value().removeAll(effect);
    if (!it.value().isEmpty()) {
        // property still registered for another effect - nothing further to do
        return;
    }
    const xcb_atom_t atom = m_managedProperties.take(propertyName);
    registerPropertyType(atom, false);
    m_propertiesForEffects.remove(propertyName);
    m_compositor->removeSupportProperty(atom); // delayed removal
}

QByteArray EffectsHandlerImpl::readRootProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return readWindowProperty(kwinApp()->x11RootWindow(), atom, type, format);
}

void EffectsHandlerImpl::activateWindow(EffectWindow* c)
{
    auto window = static_cast<EffectWindowImpl*>(c)->window();
    if (window && window->control) {
        Workspace::self()->activateClient(window, true);
    }
}

EffectWindow* EffectsHandlerImpl::activeWindow() const
{
    return Workspace::self()->activeClient() ? Workspace::self()->activeClient()->effectWindow() : nullptr;
}

void EffectsHandlerImpl::moveWindow(EffectWindow* w, const QPoint& pos, bool snap, double snapAdjust)
{
    auto window = static_cast<EffectWindowImpl*>(w)->window();
    if (!window || !window->isMovable()) {
        return;
    }

    if (snap) {
        win::move(window, Workspace::self()->adjustClientPosition(window, pos, true, snapAdjust));
    } else {
        win::move(window, pos);
    }
}

void EffectsHandlerImpl::windowToDesktop(EffectWindow* w, int desktop)
{
    auto window = static_cast<EffectWindowImpl*>(w)->window();
    if (window && window->control && !win::is_desktop(window) && !win::is_dock(window)) {
        Workspace::self()->sendClientToDesktop(window, desktop, true);
    }
}

void EffectsHandlerImpl::windowToDesktops(EffectWindow *w, const QVector<uint> &desktopIds)
{
    auto window = static_cast<EffectWindowImpl*>(w)->window();
    if (!window || !window->control || win::is_desktop(window) || win::is_dock(window)) {
        return;
    }
    QVector<VirtualDesktop*> desktops;
    desktops.reserve(desktopIds.count());
    for (uint x11Id: desktopIds) {
        if (x11Id > VirtualDesktopManager::self()->count()) {
            continue;
        }
        VirtualDesktop *d = VirtualDesktopManager::self()->desktopForX11Id(x11Id);
        Q_ASSERT(d);
        if (desktops.contains(d)) {
            continue;
        }
        desktops << d;
    }
    win::set_desktops(window, desktops);
}

void EffectsHandlerImpl::windowToScreen(EffectWindow* w, int screen)
{
    auto window = static_cast<EffectWindowImpl*>(w)->window();
    if (window && window->control && !win::is_desktop(window) && !win::is_dock(window))
        Workspace::self()->sendClientToScreen(window, screen);
}

void EffectsHandlerImpl::setShowingDesktop(bool showing)
{
    Workspace::self()->setShowingDesktop(showing);
}

QString EffectsHandlerImpl::currentActivity() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return QString();
    }
    return Activities::self()->current();
#else
    return QString();
#endif
}

int EffectsHandlerImpl::currentDesktop() const
{
    return VirtualDesktopManager::self()->current();
}

int EffectsHandlerImpl::numberOfDesktops() const
{
    return VirtualDesktopManager::self()->count();
}

void EffectsHandlerImpl::setCurrentDesktop(int desktop)
{
    VirtualDesktopManager::self()->setCurrent(desktop);
}

void EffectsHandlerImpl::setNumberOfDesktops(int desktops)
{
    VirtualDesktopManager::self()->setCount(desktops);
}

QSize EffectsHandlerImpl::desktopGridSize() const
{
    return VirtualDesktopManager::self()->grid().size();
}

int EffectsHandlerImpl::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int EffectsHandlerImpl::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int EffectsHandlerImpl::workspaceWidth() const
{
    return desktopGridWidth() * Screens::self()->size().width();
}

int EffectsHandlerImpl::workspaceHeight() const
{
    return desktopGridHeight() * Screens::self()->size().height();
}

int EffectsHandlerImpl::desktopAtCoords(QPoint coords) const
{
    if (auto vd = VirtualDesktopManager::self()->grid().at(coords)) {
        return vd->x11DesktopNumber();
    }
    return 0;
}

QPoint EffectsHandlerImpl::desktopGridCoords(int id) const
{
    return VirtualDesktopManager::self()->grid().gridCoords(id);
}

QPoint EffectsHandlerImpl::desktopCoords(int id) const
{
    QPoint coords = VirtualDesktopManager::self()->grid().gridCoords(id);
    if (coords.x() == -1)
        return QPoint(-1, -1);
    const QSize displaySize = Screens::self()->size();
    return QPoint(coords.x() * displaySize.width(), coords.y() * displaySize.height());
}

int EffectsHandlerImpl::desktopAbove(int desktop, bool wrap) const
{
    return getDesktop<DesktopAbove>(desktop, wrap);
}

int EffectsHandlerImpl::desktopToRight(int desktop, bool wrap) const
{
    return getDesktop<DesktopRight>(desktop, wrap);
}

int EffectsHandlerImpl::desktopBelow(int desktop, bool wrap) const
{
    return getDesktop<DesktopBelow>(desktop, wrap);
}

int EffectsHandlerImpl::desktopToLeft(int desktop, bool wrap) const
{
    return getDesktop<DesktopLeft>(desktop, wrap);
}

QString EffectsHandlerImpl::desktopName(int desktop) const
{
    return VirtualDesktopManager::self()->name(desktop);
}

bool EffectsHandlerImpl::optionRollOverDesktops() const
{
    return options->isRollOverDesktops();
}

double EffectsHandlerImpl::animationTimeFactor() const
{
    return options->animationTimeFactor();
}

WindowQuadType EffectsHandlerImpl::newWindowQuadType()
{
    return WindowQuadType(next_window_quad_type++);
}

EffectWindow* EffectsHandlerImpl::findWindow(WId id) const
{
    if (auto w = Workspace::self()->findClient(win::x11::predicate_match::window, id)) {
        return w->effectWindow();
    }
    if (auto unmanaged = Workspace::self()->findUnmanaged(id)) {
        return unmanaged->effectWindow();
    }
    return nullptr;
}

EffectWindow* EffectsHandlerImpl::findWindow(Wrapland::Server::Surface* /*surf*/) const
{
    return nullptr;
}

EffectWindow *EffectsHandlerImpl::findWindow(QWindow *w) const
{
    if (Toplevel *toplevel = workspace()->findInternal(w)) {
        return toplevel->effectWindow();
    }
    return nullptr;
}

EffectWindow *EffectsHandlerImpl::findWindow(const QUuid &id) const
{
    auto const toplevel = workspace()->findToplevel(
        [&id] (Toplevel const* t) { return t->internalId() == id; });
    return toplevel ? toplevel->effectWindow() : nullptr;
}

EffectWindowList EffectsHandlerImpl::stackingOrder() const
{
    auto list = workspace()->x_stacking_tree->as_list();
    EffectWindowList ret;
    for (auto t : list) {
        if (EffectWindow *w = effectWindow(t))
            ret.append(w);
    }
    return ret;
}

void EffectsHandlerImpl::setElevatedWindow(KWin::EffectWindow* w, bool set)
{
    elevated_windows.removeAll(w);
    if (set)
        elevated_windows.append(w);
}

void EffectsHandlerImpl::setTabBoxWindow(EffectWindow* w)
{
#ifdef KWIN_BUILD_TABBOX
    auto window = static_cast<EffectWindowImpl*>(w)->window();
    if (window->control) {
        TabBox::TabBox::self()->setCurrentClient(window);
    }
#else
    Q_UNUSED(w)
#endif
}

void EffectsHandlerImpl::setTabBoxDesktop(int desktop)
{
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->setCurrentDesktop(desktop);
#else
    Q_UNUSED(desktop)
#endif
}

EffectWindowList EffectsHandlerImpl::currentTabBoxWindowList() const
{
#ifdef KWIN_BUILD_TABBOX
    const auto clients = TabBox::TabBox::self()->currentClientList();
    EffectWindowList ret;
    ret.reserve(clients.size());
    std::transform(std::cbegin(clients), std::cend(clients),
        std::back_inserter(ret),
        [](auto client) { return client->effectWindow(); });
    return ret;
#else
    return EffectWindowList();
#endif
}

void EffectsHandlerImpl::refTabBox()
{
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->reference();
#endif
}

void EffectsHandlerImpl::unrefTabBox()
{
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->unreference();
#endif
}

void EffectsHandlerImpl::closeTabBox()
{
#ifdef KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->close();
#endif
}

QList< int > EffectsHandlerImpl::currentTabBoxDesktopList() const
{
#ifdef KWIN_BUILD_TABBOX
    return TabBox::TabBox::self()->currentDesktopList();
#else
    return QList< int >();
#endif
}

int EffectsHandlerImpl::currentTabBoxDesktop() const
{
#ifdef KWIN_BUILD_TABBOX
    return TabBox::TabBox::self()->currentDesktop();
#else
    return -1;
#endif
}

EffectWindow* EffectsHandlerImpl::currentTabBoxWindow() const
{
#ifdef KWIN_BUILD_TABBOX
    if (auto c = TabBox::TabBox::self()->currentClient())
        return c->effectWindow();
#endif
    return nullptr;
}

void EffectsHandlerImpl::addRepaintFull()
{
    m_compositor->addRepaintFull();
}

void EffectsHandlerImpl::addRepaint(const QRect& r)
{
    m_compositor->addRepaint(r);
}

void EffectsHandlerImpl::addRepaint(const QRegion& r)
{
    m_compositor->addRepaint(r);
}

void EffectsHandlerImpl::addRepaint(int x, int y, int w, int h)
{
    m_compositor->addRepaint(x, y, w, h);
}

int EffectsHandlerImpl::activeScreen() const
{
    return Screens::self()->current();
}

int EffectsHandlerImpl::numScreens() const
{
    return Screens::self()->count();
}

int EffectsHandlerImpl::screenNumber(const QPoint& pos) const
{
    return Screens::self()->number(pos);
}

QRect EffectsHandlerImpl::clientArea(clientAreaOption opt, int screen, int desktop) const
{
    return Workspace::self()->clientArea(opt, screen, desktop);
}

QRect EffectsHandlerImpl::clientArea(clientAreaOption opt, const EffectWindow* c) const
{
    auto window = static_cast<EffectWindowImpl const*>(c)->window();
    if (window->control) {
        return Workspace::self()->clientArea(opt, window);
    } else {
        return Workspace::self()->clientArea(opt, window->frameGeometry().center(),
                                             VirtualDesktopManager::self()->current());
    }
}

QRect EffectsHandlerImpl::clientArea(clientAreaOption opt, const QPoint& p, int desktop) const
{
    return Workspace::self()->clientArea(opt, p, desktop);
}

QRect EffectsHandlerImpl::virtualScreenGeometry() const
{
    return Screens::self()->geometry();
}

QSize EffectsHandlerImpl::virtualScreenSize() const
{
    return Screens::self()->size();
}

void EffectsHandlerImpl::defineCursor(Qt::CursorShape shape)
{
    kwinApp()->input->redirect->pointer()->setEffectsOverrideCursor(shape);
}

bool EffectsHandlerImpl::checkInputWindowEvent(QMouseEvent *e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    foreach (Effect *effect, m_grabbedMouseEffects) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

bool EffectsHandlerImpl::checkInputWindowEvent(QWheelEvent *e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    foreach (Effect *effect, m_grabbedMouseEffects) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

void EffectsHandlerImpl::connectNotify(const QMetaMethod &signal)
{
    if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
        if (!m_trackingCursorChanges) {
            connect(input::get_cursor(), &input::cursor::image_changed,
                    this, &EffectsHandler::cursorShapeChanged);
            input::get_cursor()->start_image_tracking();
        }
        ++m_trackingCursorChanges;
    }
    EffectsHandler::connectNotify(signal);
}

void EffectsHandlerImpl::disconnectNotify(const QMetaMethod &signal)
{
    if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
        Q_ASSERT(m_trackingCursorChanges > 0);
        if (!--m_trackingCursorChanges) {
            input::get_cursor()->stop_image_tracking();
            disconnect(input::get_cursor(), &input::cursor::image_changed,
                       this, &EffectsHandler::cursorShapeChanged);
        }
    }
    EffectsHandler::disconnectNotify(signal);
}


void EffectsHandlerImpl::checkInputWindowStacking()
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return;
    }
    doCheckInputWindowStacking();
}

void EffectsHandlerImpl::doCheckInputWindowStacking()
{
}

QPoint EffectsHandlerImpl::cursorPos() const
{
    return input::get_cursor()->pos();
}

void EffectsHandlerImpl::reserveElectricBorder(ElectricBorder border, Effect *effect)
{
    ScreenEdges::self()->reserve(border, effect, "borderActivated");
}

void EffectsHandlerImpl::unreserveElectricBorder(ElectricBorder border, Effect *effect)
{
    ScreenEdges::self()->unreserve(border, effect);
}

void EffectsHandlerImpl::registerTouchBorder(ElectricBorder border, QAction *action)
{
    ScreenEdges::self()->reserveTouch(border, action);
}

void EffectsHandlerImpl::unregisterTouchBorder(ElectricBorder border, QAction *action)
{
    ScreenEdges::self()->unreserveTouch(border, action);
}

unsigned long EffectsHandlerImpl::xrenderBufferPicture()
{
    return m_scene->xrenderBufferPicture();
}

QPainter *EffectsHandlerImpl::scenePainter()
{
    return m_scene->scenePainter();
}

void EffectsHandlerImpl::toggleEffect(const QString& name)
{
    if (isEffectLoaded(name))
        unloadEffect(name);
    else
        loadEffect(name);
}

QStringList EffectsHandlerImpl::loadedEffects() const
{
    QStringList listModules;
    listModules.reserve(loaded_effects.count());
    std::transform(loaded_effects.constBegin(), loaded_effects.constEnd(),
        std::back_inserter(listModules),
        [](const EffectPair &pair) { return pair.first; });
    return listModules;
}

QStringList EffectsHandlerImpl::listOfEffects() const
{
    return m_effectLoader->listOfKnownEffects();
}

bool EffectsHandlerImpl::loadEffect(const QString& name)
{
    makeOpenGLContextCurrent();
    m_compositor->addRepaintFull();

    return m_effectLoader->loadEffect(name);
}

void EffectsHandlerImpl::unloadEffect(const QString& name)
{
    auto it = std::find_if(effect_order.begin(), effect_order.end(),
        [name](EffectPair &pair) {
            return pair.first == name;
        }
    );
    if (it == effect_order.end()) {
        qCDebug(KWIN_CORE) << "EffectsHandler::unloadEffect : Effect not loaded :" << name;
        return;
    }

    qCDebug(KWIN_CORE) << "EffectsHandler::unloadEffect : Unloading Effect :" << name;
    destroyEffect((*it).second);
    effect_order.erase(it);
    effectsChanged();

    m_compositor->addRepaintFull();
}

void EffectsHandlerImpl::destroyEffect(Effect *effect)
{
    makeOpenGLContextCurrent();

    if (fullscreen_effect == effect) {
        setActiveFullScreenEffect(nullptr);
    }

    if (keyboard_grab_effect == effect) {
        ungrabKeyboard();
    }

    stopMouseInterception(effect);

    const QList<QByteArray> properties = m_propertiesForEffects.keys();
    for (const QByteArray &property : properties) {
        removeSupportProperty(property, effect);
    }

    delete effect;
}

void EffectsHandlerImpl::reconfigureEffect(const QString& name)
{
    for (QVector< EffectPair >::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
        if ((*it).first == name) {
            kwinApp()->config()->reparseConfiguration();
            makeOpenGLContextCurrent();
            (*it).second->reconfigure(Effect::ReconfigureAll);
            return;
        }
}

bool EffectsHandlerImpl::isEffectLoaded(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(), loaded_effects.constEnd(),
        [&name](const EffectPair &pair) { return pair.first == name; });
    return it != loaded_effects.constEnd();
}

bool EffectsHandlerImpl::isEffectSupported(const QString &name)
{
    // If the effect is loaded, it is obviously supported.
    if (isEffectLoaded(name)) {
        return true;
    }

    // next checks might require a context
    makeOpenGLContextCurrent();
    m_compositor->addRepaintFull();

    return m_effectLoader->isEffectSupported(name);

}

QList<bool> EffectsHandlerImpl::areEffectsSupported(const QStringList &names)
{
    QList<bool> retList;
    retList.reserve(names.count());
    std::transform(names.constBegin(), names.constEnd(),
        std::back_inserter(retList),
        [this](const QString &name) {
            return isEffectSupported(name);
        });
    return retList;
}

void EffectsHandlerImpl::reloadEffect(Effect *effect)
{
    QString effectName;
    for (QVector< EffectPair >::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).second == effect) {
            effectName = (*it).first;
            break;
        }
    }
    if (!effectName.isNull()) {
        unloadEffect(effectName);
        m_effectLoader->loadEffect(effectName);
    }
}

void EffectsHandlerImpl::effectsChanged()
{
    loaded_effects.clear();
    m_activeEffects.clear(); // it's possible to have a reconfigure and a quad rebuild between two paint cycles - bug #308201

    loaded_effects.reserve(effect_order.count());
    std::copy(effect_order.constBegin(), effect_order.constEnd(),
        std::back_inserter(loaded_effects));

    m_activeEffects.reserve(loaded_effects.count());
}

QStringList EffectsHandlerImpl::activeEffects() const
{
    QStringList ret;
    for(QVector< KWin::EffectPair >::const_iterator it = loaded_effects.constBegin(),
                                                    end = loaded_effects.constEnd(); it != end; ++it) {
            if (it->second->isActive()) {
                ret << it->first;
            }
        }
    return ret;
}

Wrapland::Server::Display *EffectsHandlerImpl::waylandDisplay() const
{
    return nullptr;
}

EffectFrame* EffectsHandlerImpl::effectFrame(EffectFrameStyle style, bool staticSize, const QPoint& position, Qt::Alignment alignment) const
{
    return new EffectFrameImpl(style, staticSize, position, alignment);
}


QVariant EffectsHandlerImpl::kwinOption(KWinOption kwopt)
{
    switch (kwopt) {
    case CloseButtonCorner:
        // TODO: this could become per window and be derived from the actual position in the deco
        return Decoration::DecorationBridge::self()->settings()->decorationButtonsLeft().contains(KDecoration2::DecorationButtonType::Close) ? Qt::TopLeftCorner : Qt::TopRightCorner;
    case SwitchDesktopOnScreenEdge:
        return ScreenEdges::self()->isDesktopSwitching();
    case SwitchDesktopOnScreenEdgeMovingWindows:
        return ScreenEdges::self()->isDesktopSwitchingMovingClients();
    default:
        return QVariant(); // an invalid one
    }
}

QString EffectsHandlerImpl::supportInformation(const QString &name) const
{
    auto it = std::find_if(loaded_effects.constBegin(), loaded_effects.constEnd(),
        [name](const EffectPair &pair) { return pair.first == name; });
    if (it == loaded_effects.constEnd()) {
        return QString();
    }

    QString support((*it).first + QLatin1String(":\n"));
    const QMetaObject *metaOptions = (*it).second->metaObject();
    for (int i=0; i<metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (qstrcmp(property.name(), "objectName") == 0) {
            continue;
        }
        support += QString::fromUtf8(property.name()) + QLatin1String(": ") + (*it).second->property(property.name()).toString() + QLatin1Char('\n');
    }

    return support;
}


bool EffectsHandlerImpl::isScreenLocked() const
{
    return ScreenLockerWatcher::self()->isLocked();
}

QString EffectsHandlerImpl::debug(const QString& name, const QString& parameter) const
{
    QString internalName = name.toLower();;
    for (QVector< EffectPair >::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).first == internalName) {
            return it->second->debug(parameter);
        }
    }
    return QString();
}

bool EffectsHandlerImpl::makeOpenGLContextCurrent()
{
    return m_scene->makeOpenGLContextCurrent();
}

void EffectsHandlerImpl::doneOpenGLContextCurrent()
{
    m_scene->doneOpenGLContextCurrent();
}

bool EffectsHandlerImpl::animationsSupported() const
{
    static const QByteArray forceEnvVar = qgetenv("KWIN_EFFECTS_FORCE_ANIMATIONS");
    if (!forceEnvVar.isEmpty()) {
        static const int forceValue = forceEnvVar.toInt();
        return forceValue == 1;
    }
    return m_scene->animationsSupported();
}

void EffectsHandlerImpl::highlightWindows(const QVector<EffectWindow *> &windows)
{
    Effect *e = provides(Effect::HighlightWindows);
    if (!e) {
        return;
    }
    e->perform(Effect::HighlightWindows, QVariantList{QVariant::fromValue(windows)});
}

PlatformCursorImage EffectsHandlerImpl::cursorImage() const
{
    return kwinApp()->input->cursor->platform_image();
}

void EffectsHandlerImpl::hideCursor()
{
    kwinApp()->input->cursor->hide();
}

void EffectsHandlerImpl::showCursor()
{
    kwinApp()->input->cursor->show();
}

void EffectsHandlerImpl::startInteractiveWindowSelection(std::function<void(KWin::EffectWindow*)> callback)
{
    kwinApp()->input->start_interactive_window_selection(
        [callback] (KWin::Toplevel *t) {
            if (t && t->effectWindow()) {
                callback(t->effectWindow());
            } else {
                callback(nullptr);
            }
        }
    );
}

void EffectsHandlerImpl::startInteractivePositionSelection(std::function<void(const QPoint&)> callback)
{
    kwinApp()->input->start_interactive_position_selection(callback);
}

void EffectsHandlerImpl::showOnScreenMessage(const QString &message, const QString &iconName)
{
    OSD::show(message, iconName);
}

void EffectsHandlerImpl::hideOnScreenMessage(OnScreenMessageHideFlags flags)
{
    OSD::HideFlags osdFlags;
    if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
        osdFlags |= OSD::HideFlag::SkipCloseAnimation;
    }
    OSD::hide(osdFlags);
}

KSharedConfigPtr EffectsHandlerImpl::config() const
{
    return kwinApp()->config();
}

KSharedConfigPtr EffectsHandlerImpl::inputConfig() const
{
    return kwinApp()->inputConfig();
}

Effect *EffectsHandlerImpl::findEffect(const QString &name) const
{
    auto it = std::find_if(loaded_effects.constBegin(), loaded_effects.constEnd(),
        [name] (const EffectPair &pair) {
            return pair.first == name;
        }
    );
    if (it == loaded_effects.constEnd()) {
        return nullptr;
    }
    return (*it).second;
}

void EffectsHandlerImpl::renderEffectQuickView(EffectQuickView *w) const
{
    if (!w->isVisible()) {
        return;
    }
    scene()->paintEffectQuickView(w);
}

SessionState EffectsHandlerImpl::sessionState() const
{
    return Workspace::self()->sessionManager()->state();
}

QList<EffectScreen *> EffectsHandlerImpl::screens() const
{
    return m_effectScreens;
}

EffectScreen *EffectsHandlerImpl::screenAt(const QPoint &point) const
{
    return m_effectScreens.value(screenNumber(point));
}

EffectScreen *EffectsHandlerImpl::findScreen(const QString &name) const
{
    for (EffectScreen *screen : qAsConst(m_effectScreens)) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return nullptr;
}

EffectScreen *EffectsHandlerImpl::findScreen(int screenId) const
{
    return m_effectScreens.value(screenId);
}

void EffectsHandlerImpl::slotOutputEnabled(AbstractOutput *output)
{
    EffectScreen *screen = new EffectScreenImpl(output, this);
    m_effectScreens.append(screen);
    emit screenAdded(screen);
}

void EffectsHandlerImpl::slotOutputDisabled(AbstractOutput *output)
{
    auto it = std::find_if(m_effectScreens.begin(), m_effectScreens.end(), [&output](EffectScreen *screen) {
        return static_cast<EffectScreenImpl *>(screen)->platformOutput() == output;
    });
    if (it != m_effectScreens.end()) {
        EffectScreen *screen = *it;
        m_effectScreens.erase(it);
        emit screenRemoved(screen);
        delete screen;
    }
}

//****************************************
// EffectScreenImpl
//****************************************

EffectScreenImpl::EffectScreenImpl(AbstractOutput *output, QObject *parent)
    : EffectScreen(parent)
    , m_platformOutput(output)
{
    connect(output, &AbstractOutput::wake_up, this, &EffectScreen::wakeUp);
    connect(output, &AbstractOutput::about_to_turn_off, this, &EffectScreen::aboutToTurnOff);
    connect(output, &AbstractOutput::scale_changed, this, &EffectScreen::devicePixelRatioChanged);
    connect(output, &AbstractOutput::geometry_changed, this, &EffectScreen::geometryChanged);
}

AbstractOutput *EffectScreenImpl::platformOutput() const
{
    return m_platformOutput;
}

QString EffectScreenImpl::name() const
{
    return m_platformOutput->name();
}

qreal EffectScreenImpl::devicePixelRatio() const
{
    return m_platformOutput->scale();
}

QRect EffectScreenImpl::geometry() const
{
    return m_platformOutput->geometry();
}

//****************************************
// EffectWindowImpl
//****************************************

EffectWindowImpl::EffectWindowImpl(Toplevel *toplevel)
    : EffectWindow(toplevel)
    , toplevel(toplevel)
    , sw(nullptr)
{
    // Deleted windows are not managed. So, when windowClosed signal is
    // emitted, effects can't distinguish managed windows from unmanaged
    // windows(e.g. combo box popups, popup menus, etc). Save value of the
    // managed property during construction of EffectWindow. At that time,
    // parent can be Client, XdgShellClient, or Unmanaged. So, later on, when
    // an instance of Deleted becomes parent of the EffectWindow, effects
    // can still figure out whether it is/was a managed window.
    managed = toplevel->isClient();

    waylandClient = toplevel->is_wayland_window();
    x11Client = qobject_cast<KWin::win::x11::window*>(toplevel) != nullptr || toplevel->xcb_window();
}

EffectWindowImpl::~EffectWindowImpl()
{
    QVariant cachedTextureVariant = data(LanczosCacheRole);
    if (cachedTextureVariant.isValid()) {
        GLTexture *cachedTexture = static_cast< GLTexture*>(cachedTextureVariant.value<void*>());
        delete cachedTexture;
    }
}

bool EffectWindowImpl::isPaintingEnabled()
{
    return sceneWindow()->isPaintingEnabled();
}

void EffectWindowImpl::enablePainting(int reason)
{
    sceneWindow()->enablePainting(reason);
}

void EffectWindowImpl::disablePainting(int reason)
{
    sceneWindow()->disablePainting(reason);
}

void EffectWindowImpl::addRepaint(const QRect &r)
{
    toplevel->addRepaint(r);
}

void EffectWindowImpl::addRepaint(int x, int y, int w, int h)
{
    toplevel->addRepaint(x, y, w, h);
}

void EffectWindowImpl::addRepaintFull()
{
    toplevel->addRepaintFull();
}

void EffectWindowImpl::addLayerRepaint(const QRect &r)
{
    toplevel->addLayerRepaint(r);
}

void EffectWindowImpl::addLayerRepaint(int x, int y, int w, int h)
{
    toplevel->addLayerRepaint(x, y, w, h);
}

const EffectWindowGroup* EffectWindowImpl::group() const
{
    if (auto c = qobject_cast<win::x11::window*>(toplevel)) {
        return c->group()->effectGroup();
    }
    return nullptr; // TODO
}

void EffectWindowImpl::refWindow()
{
    if (toplevel->transient()->annexed) {
        return;
    }
    if (auto remnant = toplevel->remnant()) {
        return remnant->ref();
    }
    abort(); // TODO
}

void EffectWindowImpl::unrefWindow()
{
    if (toplevel->transient()->annexed) {
        return;
    }
    if (auto remnant = toplevel->remnant()) {
        // delays deletion in case
        return remnant->unref();
    }
    abort(); // TODO
}

QRect EffectWindowImpl::rect() const
{
    return QRect(QPoint(), toplevel->size());
}

#define TOPLEVEL_HELPER( rettype, prototype, toplevelPrototype) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        return toplevel->toplevelPrototype(); \
    }

TOPLEVEL_HELPER(double, opacity, opacity)
TOPLEVEL_HELPER(bool, hasAlpha, hasAlpha)
TOPLEVEL_HELPER(int, x, pos().x)
TOPLEVEL_HELPER(int, y, pos().y)
TOPLEVEL_HELPER(int, width, size().width)
TOPLEVEL_HELPER(int, height, size().height)
TOPLEVEL_HELPER(QPoint, pos, pos)
TOPLEVEL_HELPER(QSize, size, size)
TOPLEVEL_HELPER(int, screen, screen)
TOPLEVEL_HELPER(QRect, geometry, frameGeometry)
TOPLEVEL_HELPER(QRect, frameGeometry, frameGeometry)
TOPLEVEL_HELPER(int, desktop, desktop)
TOPLEVEL_HELPER(bool, isDeleted, isDeleted)
TOPLEVEL_HELPER(QString, windowRole, windowRole)
TOPLEVEL_HELPER(QStringList, activities, activities)
TOPLEVEL_HELPER(bool, skipsCloseAnimation, skipsCloseAnimation)
TOPLEVEL_HELPER(Wrapland::Server::Surface *, surface, surface)
TOPLEVEL_HELPER(bool, isOutline, isOutline)
TOPLEVEL_HELPER(bool, isLockScreen, isLockScreen)
TOPLEVEL_HELPER(pid_t, pid, pid)
TOPLEVEL_HELPER(bool, isModal, transient()->modal)

#undef TOPLEVEL_HELPER

#define TOPLEVEL_HELPER_WIN( rettype, prototype, function) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        return win::function(toplevel); \
    }

TOPLEVEL_HELPER_WIN(bool, isComboBox, is_combo_box)
TOPLEVEL_HELPER_WIN(bool, isCriticalNotification, is_critical_notification)
TOPLEVEL_HELPER_WIN(bool, isDesktop, is_desktop)
TOPLEVEL_HELPER_WIN(bool, isDialog, is_dialog)
TOPLEVEL_HELPER_WIN(bool, isDNDIcon, is_dnd_icon)
TOPLEVEL_HELPER_WIN(bool, isDock, is_dock)
TOPLEVEL_HELPER_WIN(bool, isDropdownMenu, is_dropdown_menu)
TOPLEVEL_HELPER_WIN(bool, isMenu, is_menu)
TOPLEVEL_HELPER_WIN(bool, isNormalWindow, is_normal)
TOPLEVEL_HELPER_WIN(bool, isNotification, is_notification)
TOPLEVEL_HELPER_WIN(bool, isPopupMenu, is_popup_menu)
TOPLEVEL_HELPER_WIN(bool, isPopupWindow, is_popup)
TOPLEVEL_HELPER_WIN(bool, isOnScreenDisplay, is_on_screen_display)
TOPLEVEL_HELPER_WIN(bool, isSplash, is_splash)
TOPLEVEL_HELPER_WIN(bool, isToolbar, is_toolbar)
TOPLEVEL_HELPER_WIN(bool, isUtility, is_utility)
TOPLEVEL_HELPER_WIN(bool, isTooltip, is_tooltip)
TOPLEVEL_HELPER_WIN(QRect, bufferGeometry, render_geometry)

#undef TOPLEVEL_HELPER_WIN

#define CLIENT_HELPER_WITH_DELETED_WIN( rettype, prototype, propertyname, defaultValue ) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        if (toplevel->control || toplevel->remnant()) { \
            return win::propertyname(toplevel); \
        } \
        return defaultValue; \
    }

CLIENT_HELPER_WITH_DELETED_WIN(QString, caption, caption, QString())
CLIENT_HELPER_WITH_DELETED_WIN(QVector<uint>, desktops, x11_desktop_ids, QVector<uint>())

#undef CLIENT_HELPER_WITH_DELETED_WIN

#define CLIENT_HELPER_WITH_DELETED_WIN_CTRL( rettype, prototype, propertyname, defaultValue ) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        if (toplevel->control) { \
            return toplevel->control->propertyname(); \
        } \
        if (auto remnant = toplevel->remnant()) { \
            return remnant->propertyname; \
        } \
        return defaultValue; \
    }

CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, keepAbove, keep_above, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, keepBelow, keep_below, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, isMinimized, minimized, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, isFullScreen, fullscreen, false)

#undef CLIENT_HELPER_WITH_DELETED_WIN_CTRL

QRect EffectWindowImpl::clientGeometry() const
{
    return win::frame_to_client_rect(toplevel, toplevel->frameGeometry());
}

QRect expanded_geometry_recursion(Toplevel* window)
{
    QRect geo;
    for (auto child : window->transient()->children) {
        if (child->transient()->annexed) {
            geo |= expanded_geometry_recursion(child);
        }
    }
    return geo |= win::visible_rect(window);
}

QRect EffectWindowImpl::expandedGeometry() const
{
    return expanded_geometry_recursion(toplevel);
}

// legacy from tab groups, can be removed when no effects use this any more.
bool EffectWindowImpl::isCurrentTab() const
{
    return true;
}

QString EffectWindowImpl::windowClass() const
{
    return toplevel->resourceName() + QLatin1Char(' ') + toplevel->resourceClass();
}

QRect EffectWindowImpl::contentsRect() const
{
    // TODO(romangg): This feels kind of wrong. Why are the frame extents not part of it (i.e. just
    //                using frame_to_client_rect)? But some clients rely on the current version,
    //                for example Latte for its behind-dock blur.
    auto const deco_offset = QPoint(win::left_border(toplevel), win::top_border(toplevel));
    auto const client_size = win::frame_relative_client_rect(toplevel).size();

    return QRect(deco_offset, client_size);
}

NET::WindowType EffectWindowImpl::windowType() const
{
    return toplevel->windowType();
}

#define CLIENT_HELPER( rettype, prototype, propertyname, defaultValue ) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        if (toplevel->control) { \
            return toplevel->propertyname(); \
        } \
        return defaultValue; \
    }

CLIENT_HELPER(bool, isMovable, isMovable, false)
CLIENT_HELPER(bool, isMovableAcrossScreens, isMovableAcrossScreens, false)
CLIENT_HELPER(QRect, iconGeometry, iconGeometry, QRect())
CLIENT_HELPER(bool, acceptsFocus, wantsInput, true) // We don't actually know...

#undef CLIENT_HELPER

#define CLIENT_HELPER_WIN( rettype, prototype, function, default_value ) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        if (toplevel->control) { \
            return win::function(toplevel); \
        } \
        return default_value; \
    }

CLIENT_HELPER_WIN(bool, isSpecialWindow, is_special_window, true)
CLIENT_HELPER_WIN(bool, isUserMove, is_move, false)
CLIENT_HELPER_WIN(bool, isUserResize, is_resize, false)
CLIENT_HELPER_WIN(bool, decorationHasAlpha, decoration_has_alpha, false)

#undef CLIENT_HELPER_WIN

#define CLIENT_HELPER_WIN_CONTROL( rettype, prototype, function, default_value ) \
    rettype EffectWindowImpl::prototype ( ) const \
    { \
        if (toplevel->control) { \
            return toplevel->control->function(); \
        } \
        return default_value; \
    }

CLIENT_HELPER_WIN_CONTROL(bool, isSkipSwitcher, skip_switcher, false)
CLIENT_HELPER_WIN_CONTROL(QIcon, icon, icon, QIcon())
CLIENT_HELPER_WIN_CONTROL(bool, isUnresponsive, unresponsive, false)

#undef CLIENT_HELPER_WIN_CONTROL

QSize EffectWindowImpl::basicUnit() const
{
    if (auto client = qobject_cast<win::x11::window*>(toplevel)){
        return client->basicUnit();
    }
    return QSize(1,1);
}

void EffectWindowImpl::setWindow(Toplevel* w)
{
    toplevel = w;
    setParent(w);
}

void EffectWindowImpl::setSceneWindow(Scene::Window* w)
{
    sw = w;
}

QRect EffectWindowImpl::decorationInnerRect() const
{
    return contentsRect();
}

QByteArray EffectWindowImpl::readProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return readWindowProperty(window()->xcb_window(), atom, type, format);
}

void EffectWindowImpl::deleteProperty(long int atom) const
{
    if (kwinApp()->x11Connection()) {
        deleteWindowProperty(window()->xcb_window(), atom);
    }
}

EffectWindow* EffectWindowImpl::findModal()
{
    if (!toplevel->control) {
        return nullptr;
    }

    auto modal = toplevel->findModal();
    if (modal) {
        return modal->effectWindow();
    }

    return nullptr;
}

EffectWindow* EffectWindowImpl::transientFor()
{
    if (!toplevel->control) {
        return nullptr;
    }

    auto transientFor = toplevel->transient()->lead();
    if (transientFor) {
        return transientFor->effectWindow();
    }

    return nullptr;
}

QWindow *EffectWindowImpl::internalWindow() const
{
    auto client = qobject_cast<win::InternalClient *>(toplevel);
    if (!client) {
        return nullptr;
    }
    return client->internalWindow();
}

template <typename T>
EffectWindowList getMainWindows(T *c)
{
    const auto leads = c->transient()->leads();
    EffectWindowList ret;
    ret.reserve(leads.size());
    std::transform(std::cbegin(leads), std::cend(leads),
        std::back_inserter(ret),
        [](auto client) { return client->effectWindow(); });
    return ret;
}

EffectWindowList EffectWindowImpl::mainWindows() const
{
    if (toplevel->control || toplevel->remnant()) {
        return getMainWindows(toplevel);
    }
    return {};
}

WindowQuadList EffectWindowImpl::buildQuads(bool force) const
{
    return sceneWindow()->buildQuads(force);
}

void EffectWindowImpl::setData(int role, const QVariant &data)
{
    if (!data.isNull())
        dataMap[ role ] = data;
    else
        dataMap.remove(role);
    emit effects->windowDataChanged(this, role);
}

QVariant EffectWindowImpl::data(int role) const
{
    return dataMap.value(role);
}

EffectWindow* effectWindow(Toplevel* w)
{
    EffectWindowImpl* ret = w->effectWindow();
    return ret;
}

EffectWindow* effectWindow(Scene::Window* w)
{
    EffectWindowImpl* ret = w->window()->effectWindow();
    ret->setSceneWindow(w);
    return ret;
}

void EffectWindowImpl::elevate(bool elevate)
{
    effects->setElevatedWindow(this, elevate);
}

void EffectWindowImpl::registerThumbnail(AbstractThumbnailItem *item)
{
    if (WindowThumbnailItem *thumb = qobject_cast<WindowThumbnailItem*>(item)) {
        insertThumbnail(thumb);
        connect(thumb, &QObject::destroyed, this, &EffectWindowImpl::thumbnailDestroyed);
        connect(thumb, &WindowThumbnailItem::wIdChanged, this, &EffectWindowImpl::thumbnailTargetChanged);
    } else if (DesktopThumbnailItem *desktopThumb = qobject_cast<DesktopThumbnailItem*>(item)) {
        m_desktopThumbnails.append(desktopThumb);
        connect(desktopThumb, &QObject::destroyed, this, &EffectWindowImpl::desktopThumbnailDestroyed);
    }
}

void EffectWindowImpl::thumbnailDestroyed(QObject *object)
{
    // we know it is a ThumbnailItem
    m_thumbnails.remove(static_cast<WindowThumbnailItem*>(object));
}

void EffectWindowImpl::thumbnailTargetChanged()
{
    if (WindowThumbnailItem *item = qobject_cast<WindowThumbnailItem*>(sender())) {
        insertThumbnail(item);
    }
}

void EffectWindowImpl::insertThumbnail(WindowThumbnailItem *item)
{
    EffectWindow *w = effects->findWindow(item->wId());
    if (w) {
        m_thumbnails.insert(item, QPointer<EffectWindowImpl>(static_cast<EffectWindowImpl*>(w)));
    } else {
        m_thumbnails.insert(item, QPointer<EffectWindowImpl>());
    }
}

void EffectWindowImpl::desktopThumbnailDestroyed(QObject *object)
{
    // we know it is a DesktopThumbnailItem
    m_desktopThumbnails.removeAll(static_cast<DesktopThumbnailItem*>(object));
}

void EffectWindowImpl::minimize()
{
    if (toplevel->control) {
        win::set_minimized(toplevel, true);
    }
}

void EffectWindowImpl::unminimize()
{
    if (toplevel->control) {
        win::set_minimized(toplevel, false);
    }
}

void EffectWindowImpl::closeWindow()
{
    if (toplevel->control) {
        toplevel->closeWindow();
    }
}

void EffectWindowImpl::referencePreviousWindowPixmap()
{
    if (sw) {
        sw->referencePreviousPixmap();
    }
}

void EffectWindowImpl::unreferencePreviousWindowPixmap()
{
    if (sw) {
        sw->unreferencePreviousPixmap();
    }
}

bool EffectWindowImpl::isManaged() const
{
    return managed;
}

bool EffectWindowImpl::isWaylandClient() const
{
    return waylandClient;
}

bool EffectWindowImpl::isX11Client() const
{
    return x11Client;
}


//****************************************
// EffectWindowGroupImpl
//****************************************


EffectWindowList EffectWindowGroupImpl::members() const
{
    const auto memberList = group->members();
    EffectWindowList ret;
    ret.reserve(memberList.size());
    std::transform(std::cbegin(memberList), std::cend(memberList),
        std::back_inserter(ret),
        [](auto toplevel) { return toplevel->effectWindow(); });
    return ret;
}

//****************************************
// EffectFrameImpl
//****************************************

EffectFrameImpl::EffectFrameImpl(EffectFrameStyle style, bool staticSize, QPoint position, Qt::Alignment alignment)
    : QObject(nullptr)
    , EffectFrame()
    , m_style(style)
    , m_static(staticSize)
    , m_point(position)
    , m_alignment(alignment)
    , m_shader(nullptr)
    , m_theme(new Plasma::Theme(this))
{
    if (m_style == EffectFrameStyled) {
        m_frame.setImagePath(QStringLiteral("widgets/background"));
        m_frame.setCacheAllRenderedFrames(true);
        connect(m_theme, &Plasma::Theme::themeChanged, this, &EffectFrameImpl::plasmaThemeChanged);
    }
    m_selection.setImagePath(QStringLiteral("widgets/viewitem"));
    m_selection.setElementPrefix(QStringLiteral("hover"));
    m_selection.setCacheAllRenderedFrames(true);
    m_selection.setEnabledBorders(Plasma::FrameSvg::AllBorders);

    m_sceneFrame = render::compositor::self()->scene()->createEffectFrame(this);
}

EffectFrameImpl::~EffectFrameImpl()
{
    delete m_sceneFrame;
}

const QFont& EffectFrameImpl::font() const
{
    return m_font;
}

void EffectFrameImpl::setFont(const QFont& font)
{
    if (m_font == font) {
        return;
    }
    m_font = font;
    QRect oldGeom = m_geometry;
    if (!m_text.isEmpty()) {
        autoResize();
    }
    if (oldGeom == m_geometry) {
        // Wasn't updated in autoResize()
        m_sceneFrame->freeTextFrame();
    }
}

void EffectFrameImpl::free()
{
    m_sceneFrame->free();
}

const QRect& EffectFrameImpl::geometry() const
{
    return m_geometry;
}

void EffectFrameImpl::setGeometry(const QRect& geometry, bool force)
{
    QRect oldGeom = m_geometry;
    m_geometry = geometry;
    if (m_geometry == oldGeom && !force) {
        return;
    }
    effects->addRepaint(oldGeom);
    effects->addRepaint(m_geometry);
    if (m_geometry.size() == oldGeom.size() && !force) {
        return;
    }

    if (m_style == EffectFrameStyled) {
        qreal left, top, right, bottom;
        m_frame.getMargins(left, top, right, bottom);   // m_geometry is the inner geometry
        m_frame.resizeFrame(m_geometry.adjusted(-left, -top, right, bottom).size());
    }

    free();
}

const QIcon& EffectFrameImpl::icon() const
{
    return m_icon;
}

void EffectFrameImpl::setIcon(const QIcon& icon)
{
    m_icon = icon;
    if (isCrossFade()) {
        m_sceneFrame->crossFadeIcon();
    }
    if (m_iconSize.isEmpty() && !m_icon.availableSizes().isEmpty()) { // Set a size if we don't already have one
        setIconSize(m_icon.availableSizes().first());
    }
    m_sceneFrame->freeIconFrame();
}

const QSize& EffectFrameImpl::iconSize() const
{
    return m_iconSize;
}

void EffectFrameImpl::setIconSize(const QSize& size)
{
    if (m_iconSize == size) {
        return;
    }
    m_iconSize = size;
    autoResize();
    m_sceneFrame->freeIconFrame();
}

void EffectFrameImpl::plasmaThemeChanged()
{
    free();
}

void EffectFrameImpl::render(const QRegion &region, double opacity, double frameOpacity)
{
    if (m_geometry.isEmpty()) {
        return; // Nothing to display
    }
    m_shader = nullptr;
    setScreenProjectionMatrix(static_cast<EffectsHandlerImpl*>(effects)->scene()->screenProjectionMatrix());
    effects->paintEffectFrame(this, region, opacity, frameOpacity);
}

void EffectFrameImpl::finalRender(QRegion region, double opacity, double frameOpacity) const
{
    region = infiniteRegion(); // TODO: Old region doesn't seem to work with OpenGL

    m_sceneFrame->render(region, opacity, frameOpacity);
}

Qt::Alignment EffectFrameImpl::alignment() const
{
    return m_alignment;
}


void
EffectFrameImpl::align(QRect &geometry)
{
    if (m_alignment & Qt::AlignLeft)
        geometry.moveLeft(m_point.x());
    else if (m_alignment & Qt::AlignRight)
        geometry.moveLeft(m_point.x() - geometry.width());
    else
        geometry.moveLeft(m_point.x() - geometry.width() / 2);
    if (m_alignment & Qt::AlignTop)
        geometry.moveTop(m_point.y());
    else if (m_alignment & Qt::AlignBottom)
        geometry.moveTop(m_point.y() - geometry.height());
    else
        geometry.moveTop(m_point.y() - geometry.height() / 2);
}


void EffectFrameImpl::setAlignment(Qt::Alignment alignment)
{
    m_alignment = alignment;
    align(m_geometry);
    setGeometry(m_geometry);
}

void EffectFrameImpl::setPosition(const QPoint& point)
{
    m_point = point;
    QRect geometry = m_geometry; // this is important, setGeometry need call repaint for old & new geometry
    align(geometry);
    setGeometry(geometry);
}

const QString& EffectFrameImpl::text() const
{
    return m_text;
}

void EffectFrameImpl::setText(const QString& text)
{
    if (m_text == text) {
        return;
    }
    if (isCrossFade()) {
        m_sceneFrame->crossFadeText();
    }
    m_text = text;
    QRect oldGeom = m_geometry;
    autoResize();
    if (oldGeom == m_geometry) {
        // Wasn't updated in autoResize()
        m_sceneFrame->freeTextFrame();
    }
}

void EffectFrameImpl::setSelection(const QRect& selection)
{
    if (selection == m_selectionGeometry) {
        return;
    }
    m_selectionGeometry = selection;
    if (m_selectionGeometry.size() != m_selection.frameSize().toSize()) {
        m_selection.resizeFrame(m_selectionGeometry.size());
    }
    // TODO; optimize to only recreate when resizing
    m_sceneFrame->freeSelection();
}

void EffectFrameImpl::autoResize()
{
    if (m_static)
        return; // Not automatically resizing

    QRect geometry;
    // Set size
    if (!m_text.isEmpty()) {
        QFontMetrics metrics(m_font);
        geometry.setSize(metrics.size(0, m_text));
    }
    if (!m_icon.isNull() && !m_iconSize.isEmpty()) {
        geometry.setLeft(-m_iconSize.width());
        if (m_iconSize.height() > geometry.height())
            geometry.setHeight(m_iconSize.height());
    }

    align(geometry);
    setGeometry(geometry);
}

QColor EffectFrameImpl::styledTextColor()
{
    return m_theme->color(Plasma::Theme::TextColor);
}

} // namespace
