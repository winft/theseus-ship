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

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "desktop/screen_locker_watcher.h"
#include "effect_loader.h"
#include "effectsadaptor.h"
#include "input/cursor.h"
#include "input/pointer_redirect.h"
#include "kwineffectquickview.h"
#include "kwinglutils.h"
#include "screens.h"
#include "scripting/effect.h"
#include "thumbnail_item.h"
#include "win/control.h"
#include "win/internal_window.h"
#include "win/meta.h"
#include "win/osd.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/transient.h"
#include "win/virtual_desktops.h"
#include "win/x11/group.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/window.h"
#include "win/x11/window_property_notify_filter.h"

#ifdef KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

#include <QDebug>

#include <Plasma/Theme>

#include "platform.h"
#include "render/compositor.h"
#include "render/effect_frame.h"

#include "decorations/decorationbridge.h"
#include <KDecoration2/DecorationSettings>

namespace KWin::render
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
        base::x11::xcb::property prop(false, win, atom, XCB_ATOM_ANY, 0, len);
        if (prop.is_null()) {
            // get property failed
            return QByteArray();
        }
        if (prop->bytes_after > 0) {
            len *= 2;
            continue;
        }
        return prop.to_byte_array(format, type);
    }
}

static void deleteWindowProperty(xcb_window_t win, long int atom)
{
    if (win == XCB_WINDOW_NONE) {
        return;
    }
    xcb_delete_property(kwinApp()->x11Connection(), win, atom);
}

static xcb_atom_t registerSupportProperty(const QByteArray& propertyName)
{
    auto c = kwinApp()->x11Connection();
    if (!c) {
        return XCB_ATOM_NONE;
    }
    // get the atom for the propertyName
    unique_cptr<xcb_intern_atom_reply_t> atomReply(xcb_intern_atom_reply(
        c,
        xcb_intern_atom_unchecked(c, false, propertyName.size(), propertyName.constData()),
        nullptr));
    if (!atomReply) {
        return XCB_ATOM_NONE;
    }
    // announce property on root window
    unsigned char dummy = 0;
    xcb_change_property(c,
                        XCB_PROP_MODE_REPLACE,
                        kwinApp()->x11RootWindow(),
                        atomReply->atom,
                        atomReply->atom,
                        8,
                        1,
                        &dummy);
    // TODO: add to _NET_SUPPORTED
    return atomReply->atom;
}

//---------------------

effects_handler_impl::effects_handler_impl(render::compositor* compositor, render::scene* scene)
    : EffectsHandler(scene->compositingType())
    , keyboard_grab_effect(nullptr)
    , fullscreen_effect(nullptr)
    , next_window_quad_type(EFFECT_QUAD_TYPE_START)
    , m_compositor(compositor)
    , m_scene(scene)
    , m_desktopRendering(false)
    , m_currentRenderedDesktop(0)
    , m_effectLoader(new effect_loader(this))
    , m_trackingCursorChanges(0)
{
    qRegisterMetaType<QVector<KWin::EffectWindow*>>();
    connect(m_effectLoader,
            &basic_effect_loader::effectLoaded,
            this,
            [this](Effect* effect, const QString& name) {
                effect_order.insert(effect->requestedEffectChainPosition(),
                                    EffectPair(name, effect));
                loaded_effects << EffectPair(name, effect);
                effectsChanged();
            });
    m_effectLoader->setConfig(kwinApp()->config());
    new EffectsAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Effects"), this);
    // init is important, otherwise causes crashes when quads are build before the first painting
    // pass start
    m_currentBuildQuadsIterator = m_activeEffects.constEnd();

    auto ws = workspace();
    auto vds = win::virtual_desktop_manager::self();
    connect(
        ws, &win::space::showingDesktopChanged, this, &effects_handler_impl::showingDesktopChanged);
    connect(ws, &win::space::currentDesktopChanged, this, [this](int old, Toplevel* c) {
        int const newDesktop = win::virtual_desktop_manager::self()->current();
        if (old != 0 && newDesktop != old) {
            assert(!c || c->render);
            assert(!c || c->render->effect);
            auto eff_win = c ? c->render->effect.get() : nullptr;
            Q_EMIT desktopChanged(old, newDesktop, eff_win);
            // TODO: remove in 4.10
            Q_EMIT desktopChanged(old, newDesktop);
        }
    });
    connect(ws, &win::space::desktopPresenceChanged, this, [this](Toplevel* c, int old) {
        assert(c);
        assert(c->render);
        assert(c->render->effect);
        Q_EMIT desktopPresenceChanged(c->render->effect.get(), old, c->desktop());
    });
    connect(ws, &win::space::clientAdded, this, [this](auto c) {
        if (c->readyForPainting())
            slotClientShown(c);
        else
            connect(c, &Toplevel::windowShown, this, &effects_handler_impl::slotClientShown);
    });
    connect(ws, &win::space::unmanagedAdded, this, [this](Toplevel* u) {
        // it's never initially ready but has synthetic 50ms delay
        connect(u, &Toplevel::windowShown, this, &effects_handler_impl::slotUnmanagedShown);
    });
    connect(ws, &win::space::internalClientAdded, this, [this](auto client) {
        assert(client->render);
        assert(client->render->effect);
        setupAbstractClientConnections(client);
        Q_EMIT windowAdded(client->render->effect.get());
    });
    connect(ws, &win::space::clientActivated, this, [this](KWin::Toplevel* window) {
        assert(!window || window->render);
        assert(!window || window->render->effect);
        auto eff_win = window ? window->render->effect.get() : nullptr;
        Q_EMIT windowActivated(eff_win);
    });
    connect(ws, &win::space::deletedRemoved, this, [this](KWin::Toplevel* d) {
        assert(d->render);
        assert(d->render->effect);
        Q_EMIT windowDeleted(d->render->effect.get());
        elevated_windows.removeAll(d->render->effect.get());
    });
    connect(ws->sessionManager(),
            &win::session_manager::stateChanged,
            this,
            &KWin::EffectsHandler::sessionStateChanged);
    connect(vds,
            &win::virtual_desktop_manager::countChanged,
            this,
            &EffectsHandler::numberDesktopsChanged);
    connect(
        input::get_cursor(), &input::cursor::mouse_changed, this, &EffectsHandler::mouseChanged);

    auto& base = kwinApp()->get_base();
    connect(&base, &base::platform::output_added, this, &EffectsHandler::numberScreensChanged);
    connect(&base, &base::platform::output_removed, this, &EffectsHandler::numberScreensChanged);

    QObject::connect(
        &base, &base::platform::topology_changed, this, [this](auto old_topo, auto new_topo) {
            if (old_topo.size != new_topo.size) {
                Q_EMIT virtualScreenSizeChanged();
                Q_EMIT virtualScreenGeometryChanged();
            }
        });

    connect(ws->stacking_order,
            &win::stacking_order::changed,
            this,
            &EffectsHandler::stackingOrderChanged);
#ifdef KWIN_BUILD_TABBOX
    win::tabbox* tabBox = win::tabbox::self();
    connect(tabBox, &win::tabbox::tabbox_added, this, &EffectsHandler::tabBoxAdded);
    connect(tabBox, &win::tabbox::tabbox_updated, this, &EffectsHandler::tabBoxUpdated);
    connect(tabBox, &win::tabbox::tabbox_closed, this, &EffectsHandler::tabBoxClosed);
    connect(tabBox, &win::tabbox::tabbox_key_event, this, &EffectsHandler::tabBoxKeyEvent);
#endif
    connect(workspace()->edges.get(),
            &win::screen_edger::approaching,
            this,
            &EffectsHandler::screenEdgeApproaching);
    connect(kwinApp()->screen_locker_watcher.get(),
            &desktop::screen_locker_watcher::locked,
            this,
            &EffectsHandler::screenLockingChanged);
    connect(kwinApp()->screen_locker_watcher.get(),
            &desktop::screen_locker_watcher::about_to_lock,
            this,
            &EffectsHandler::screenAboutToLock);

    connect(kwinApp(), &Application::x11ConnectionChanged, this, [this] {
        registered_atoms.clear();
        for (auto it = m_propertiesForEffects.keyBegin(); it != m_propertiesForEffects.keyEnd();
             it++) {
            const auto atom = registerSupportProperty(*it);
            if (atom == XCB_ATOM_NONE) {
                continue;
            }
            m_compositor->keepSupportProperty(atom);
            m_managedProperties.insert(*it, atom);
            registerPropertyType(atom, true);
        }
        if (kwinApp()->x11Connection()) {
            m_x11WindowPropertyNotify
                = std::make_unique<win::x11::window_property_notify_filter>(this);
        } else {
            m_x11WindowPropertyNotify.reset();
        }
        Q_EMIT xcbConnectionChanged();
    });

    if (kwinApp()->x11Connection()) {
        m_x11WindowPropertyNotify = std::make_unique<win::x11::window_property_notify_filter>(this);
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
        if (auto internal = qobject_cast<win::internal_window*>(window)) {
            setupAbstractClientConnections(internal);
        }
    }

    connect(&kwinApp()->get_base(),
            &base::platform::output_added,
            this,
            &effects_handler_impl::slotOutputEnabled);
    connect(&kwinApp()->get_base(),
            &base::platform::output_removed,
            this,
            &effects_handler_impl::slotOutputDisabled);

    auto const outputs = kwinApp()->get_base().get_outputs();
    for (base::output* output : outputs) {
        slotOutputEnabled(output);
    }

    reconfigure();
}

effects_handler_impl::~effects_handler_impl()
{
    unloadAllEffects();
}

void effects_handler_impl::unloadAllEffects()
{
    for (const EffectPair& pair : loaded_effects) {
        destroyEffect(pair.second);
    }

    effect_order.clear();
    m_effectLoader->clear();

    effectsChanged();
}

void effects_handler_impl::setupAbstractClientConnections(Toplevel* window)
{
    connect(window, &Toplevel::windowClosed, this, &effects_handler_impl::slotWindowClosed);
    connect(window,
            static_cast<void (Toplevel::*)(KWin::Toplevel*, win::maximize_mode)>(
                &Toplevel::clientMaximizedStateChanged),
            this,
            &effects_handler_impl::slotClientMaximized);
    connect(window, &Toplevel::clientStartUserMovedResized, this, [this](Toplevel* c) {
        Q_EMIT windowStartUserMovedResized(c->render->effect.get());
    });
    connect(window,
            &Toplevel::clientStepUserMovedResized,
            this,
            [this](Toplevel* c, const QRect& geometry) {
                Q_EMIT windowStepUserMovedResized(c->render->effect.get(), geometry);
            });
    connect(window, &Toplevel::clientFinishUserMovedResized, this, [this](Toplevel* c) {
        Q_EMIT windowFinishUserMovedResized(c->render->effect.get());
    });
    connect(window, &Toplevel::opacityChanged, this, &effects_handler_impl::slotOpacityChanged);
    connect(window, &Toplevel::clientMinimized, this, [this](Toplevel* c, bool animate) {
        // TODO: notify effects even if it should not animate?
        if (animate) {
            Q_EMIT windowMinimized(c->render->effect.get());
        }
    });
    connect(window, &Toplevel::clientUnminimized, this, [this](Toplevel* c, bool animate) {
        // TODO: notify effects even if it should not animate?
        if (animate) {
            Q_EMIT windowUnminimized(c->render->effect.get());
        }
    });
    connect(
        window, &Toplevel::modalChanged, this, &effects_handler_impl::slotClientModalityChanged);
    connect(window,
            &Toplevel::frame_geometry_changed,
            this,
            &effects_handler_impl::slotGeometryShapeChanged);
    connect(window,
            &Toplevel::frame_geometry_changed,
            this,
            &effects_handler_impl::slotFrameGeometryChanged);
    connect(window, &Toplevel::damaged, this, &effects_handler_impl::slotWindowDamaged);
    connect(window, &Toplevel::unresponsiveChanged, this, [this, window](bool unresponsive) {
        Q_EMIT windowUnresponsiveChanged(window->render->effect.get(), unresponsive);
    });
    connect(window, &Toplevel::windowShown, this, [this](Toplevel* c) {
        Q_EMIT windowShown(c->render->effect.get());
    });
    connect(window, &Toplevel::windowHidden, this, [this](Toplevel* c) {
        Q_EMIT windowHidden(c->render->effect.get());
    });
    connect(window, &Toplevel::keepAboveChanged, this, [this, window](bool above) {
        Q_UNUSED(above)
        Q_EMIT windowKeepAboveChanged(window->render->effect.get());
    });
    connect(window, &Toplevel::keepBelowChanged, this, [this, window](bool below) {
        Q_UNUSED(below)
        Q_EMIT windowKeepBelowChanged(window->render->effect.get());
    });
    connect(window, &Toplevel::fullScreenChanged, this, [this, window]() {
        Q_EMIT windowFullScreenChanged(window->render->effect.get());
    });
    connect(window, &Toplevel::visible_geometry_changed, this, [this, window]() {
        Q_EMIT windowExpandedGeometryChanged(window->render->effect.get());
    });
}

void effects_handler_impl::setupClientConnections(win::x11::window* c)
{
    setupAbstractClientConnections(c);
    connect(c, &win::x11::window::paddingChanged, this, &effects_handler_impl::slotPaddingChanged);
}

void effects_handler_impl::setupUnmanagedConnections(Toplevel* u)
{
    connect(u, &Toplevel::windowClosed, this, &effects_handler_impl::slotWindowClosed);
    connect(u, &Toplevel::opacityChanged, this, &effects_handler_impl::slotOpacityChanged);
    connect(u,
            &Toplevel::frame_geometry_changed,
            this,
            &effects_handler_impl::slotGeometryShapeChanged);
    connect(u,
            &Toplevel::frame_geometry_changed,
            this,
            &effects_handler_impl::slotFrameGeometryChanged);
    connect(u, &Toplevel::paddingChanged, this, &effects_handler_impl::slotPaddingChanged);
    connect(u, &Toplevel::damaged, this, &effects_handler_impl::slotWindowDamaged);
    connect(u, &Toplevel::visible_geometry_changed, this, [this, u]() {
        Q_EMIT windowExpandedGeometryChanged(u->render->effect.get());
    });
}

void effects_handler_impl::reconfigure()
{
    m_effectLoader->queryAndLoadAll();
}

// the idea is that effects call this function again which calls the next one
void effects_handler_impl::prePaintScreen(ScreenPrePaintData& data,
                                          std::chrono::milliseconds presentTime)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->prePaintScreen(data, presentTime);
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_impl::paintScreen(int mask, const QRegion& region, ScreenPaintData& data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->paintScreen(mask, region, data);
        --m_currentPaintScreenIterator;
    } else
        m_scene->finalPaintScreen(static_cast<render::paint_type>(mask), region, data);
}

void effects_handler_impl::paintDesktop(int desktop,
                                        int mask,
                                        QRegion region,
                                        ScreenPaintData& data)
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

void effects_handler_impl::postPaintScreen()
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->postPaintScreen();
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_impl::prePaintWindow(EffectWindow* w,
                                          WindowPrePaintData& data,
                                          std::chrono::milliseconds presentTime)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->prePaintWindow(w, data, presentTime);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

void effects_handler_impl::paintWindow(EffectWindow* w,
                                       int mask,
                                       const QRegion& region,
                                       WindowPaintData& data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->paintWindow(w, mask, region, data);
        --m_currentPaintWindowIterator;
    } else
        m_scene->finalPaintWindow(static_cast<effects_window_impl*>(w),
                                  static_cast<render::paint_type>(mask),
                                  region,
                                  data);
}

void effects_handler_impl::paintEffectFrame(EffectFrame* frame,
                                            const QRegion& region,
                                            double opacity,
                                            double frameOpacity)
{
    if (m_currentPaintEffectFrameIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintEffectFrameIterator++)
            ->paintEffectFrame(frame, region, opacity, frameOpacity);
        --m_currentPaintEffectFrameIterator;
    } else {
        const effect_frame_impl* frameImpl = static_cast<const effect_frame_impl*>(frame);
        frameImpl->finalRender(region, opacity, frameOpacity);
    }
}

void effects_handler_impl::postPaintWindow(EffectWindow* w)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->postPaintWindow(w);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

Effect* effects_handler_impl::provides(Effect::Feature ef)
{
    for (int i = 0; i < loaded_effects.size(); ++i)
        if (loaded_effects.at(i).second->provides(ef))
            return loaded_effects.at(i).second;
    return nullptr;
}

void effects_handler_impl::drawWindow(EffectWindow* w,
                                      int mask,
                                      const QRegion& region,
                                      WindowPaintData& data)
{
    if (m_currentDrawWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentDrawWindowIterator++)->drawWindow(w, mask, region, data);
        --m_currentDrawWindowIterator;
    } else
        m_scene->finalDrawWindow(static_cast<effects_window_impl*>(w),
                                 static_cast<render::paint_type>(mask),
                                 region,
                                 data);
}

void effects_handler_impl::buildQuads(EffectWindow* w, WindowQuadList& quadList)
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

bool effects_handler_impl::hasDecorationShadows() const
{
    return false;
}

bool effects_handler_impl::decorationsHaveAlpha() const
{
    return true;
}

bool effects_handler_impl::decorationSupportsBlurBehind() const
{
    return Decoration::DecorationBridge::self()->needsBlur();
}

// start another painting pass
void effects_handler_impl::startPaint()
{
    m_activeEffects.clear();
    m_activeEffects.reserve(loaded_effects.count());
    for (QVector<KWin::EffectPair>::const_iterator it = loaded_effects.constBegin();
         it != loaded_effects.constEnd();
         ++it) {
        if (it->second->isActive()) {
            m_activeEffects << it->second;
        }
    }
    m_currentDrawWindowIterator = m_activeEffects.constBegin();
    m_currentPaintWindowIterator = m_activeEffects.constBegin();
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
    m_currentPaintEffectFrameIterator = m_activeEffects.constBegin();
}

void effects_handler_impl::slotClientMaximized(Toplevel* window, win::maximize_mode maxMode)
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

    auto ew = window->render->effect.get();
    assert(ew);
    Q_EMIT windowMaximizedStateChanged(ew, horizontal, vertical);
}

void effects_handler_impl::slotOpacityChanged(Toplevel* t, qreal oldOpacity)
{
    assert(t->render->effect);

    if (t->opacity() == oldOpacity) {
        return;
    }

    Q_EMIT windowOpacityChanged(t->render->effect.get(), oldOpacity, (qreal)t->opacity());
}

void effects_handler_impl::slotClientShown(KWin::Toplevel* t)
{
    assert(qobject_cast<win::x11::window*>(t));
    auto c = static_cast<win::x11::window*>(t);
    disconnect(c, &Toplevel::windowShown, this, &effects_handler_impl::slotClientShown);
    setupClientConnections(c);
    Q_EMIT windowAdded(c->render->effect.get());
}

void effects_handler_impl::slotXdgShellClientShown(Toplevel* t)
{
    setupAbstractClientConnections(t);
    Q_EMIT windowAdded(t->render->effect.get());
}

void effects_handler_impl::slotUnmanagedShown(KWin::Toplevel* t)
{ // regardless, unmanaged windows are -yet?- not synced anyway
    assert(!t->control);
    setupUnmanagedConnections(t);
    Q_EMIT windowAdded(t->render->effect.get());
}

void effects_handler_impl::slotWindowClosed(KWin::Toplevel* c, Toplevel* remnant)
{
    c->disconnect(this);
    if (remnant) {
        Q_EMIT windowClosed(remnant->render->effect.get());
    }
}

void effects_handler_impl::slotClientModalityChanged()
{
    Q_EMIT windowModalityChanged(static_cast<win::x11::window*>(sender())->render->effect.get());
}

void effects_handler_impl::slotCurrentTabAboutToChange(EffectWindow* from, EffectWindow* to)
{
    Q_EMIT currentTabAboutToChange(from, to);
}

void effects_handler_impl::slotTabAdded(EffectWindow* w, EffectWindow* to)
{
    Q_EMIT tabAdded(w, to);
}

void effects_handler_impl::slotTabRemoved(EffectWindow* w, EffectWindow* leaderOfFormerGroup)
{
    Q_EMIT tabRemoved(w, leaderOfFormerGroup);
}

void effects_handler_impl::slotWindowDamaged(Toplevel* t, const QRegion& r)
{
    assert(t->render);
    assert(t->render->effect);
    Q_EMIT windowDamaged(t->render->effect.get(), r);
}

void effects_handler_impl::slotGeometryShapeChanged(Toplevel* t, const QRect& old)
{
    assert(t);
    assert(t->render);
    assert(t->render->effect);

    if (t->control && (win::is_move(t) || win::is_resize(t))) {
        // For that we have windowStepUserMovedResized.
        return;
    }

    Q_EMIT windowGeometryShapeChanged(t->render->effect.get(), old);
}

void effects_handler_impl::slotFrameGeometryChanged(Toplevel* toplevel, const QRect& oldGeometry)
{
    assert(toplevel->render);
    assert(toplevel->render->effect);
    Q_EMIT windowFrameGeometryChanged(toplevel->render->effect.get(), oldGeometry);
}

void effects_handler_impl::slotPaddingChanged(Toplevel* t, const QRect& old)
{
    assert(t);
    assert(t->render);
    assert(t->render->effect);
    Q_EMIT windowPaddingChanged(t->render->effect.get(), old);
}

void effects_handler_impl::setActiveFullScreenEffect(Effect* e)
{
    if (fullscreen_effect == e) {
        return;
    }
    const bool activeChanged = (e == nullptr || fullscreen_effect == nullptr);
    fullscreen_effect = e;
    Q_EMIT activeFullScreenEffectChanged();
    if (activeChanged) {
        Q_EMIT hasActiveFullScreenEffectChanged();
    }
}

Effect* effects_handler_impl::activeFullScreenEffect() const
{
    return fullscreen_effect;
}

bool effects_handler_impl::hasActiveFullScreenEffect() const
{
    return fullscreen_effect;
}

bool effects_handler_impl::grabKeyboard(Effect* effect)
{
    if (keyboard_grab_effect != nullptr)
        return false;
    if (!doGrabKeyboard()) {
        return false;
    }
    keyboard_grab_effect = effect;
    return true;
}

bool effects_handler_impl::doGrabKeyboard()
{
    return true;
}

void effects_handler_impl::ungrabKeyboard()
{
    Q_ASSERT(keyboard_grab_effect != nullptr);
    doUngrabKeyboard();
    keyboard_grab_effect = nullptr;
}

void effects_handler_impl::doUngrabKeyboard()
{
}

void effects_handler_impl::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (keyboard_grab_effect != nullptr)
        keyboard_grab_effect->grabbedKeyboardEvent(e);
}

void effects_handler_impl::startMouseInterception(Effect* effect, Qt::CursorShape shape)
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

void effects_handler_impl::doStartMouseInterception(Qt::CursorShape shape)
{
    kwinApp()->input->redirect->pointer()->setEffectsOverrideCursor(shape);
}

void effects_handler_impl::stopMouseInterception(Effect* effect)
{
    if (!m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.removeAll(effect);
    if (m_grabbedMouseEffects.isEmpty()) {
        doStopMouseInterception();
    }
}

void effects_handler_impl::doStopMouseInterception()
{
    kwinApp()->input->redirect->pointer()->removeEffectsOverrideCursor();
}

bool effects_handler_impl::isMouseInterception() const
{
    return m_grabbedMouseEffects.count() > 0;
}

bool effects_handler_impl::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchDown(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool effects_handler_impl::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchMotion(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool effects_handler_impl::touchUp(qint32 id, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchUp(id, time)) {
            return true;
        }
    }
    return false;
}

void effects_handler_impl::registerGlobalShortcut(const QKeySequence& shortcut, QAction* action)
{
    kwinApp()->input->redirect->registerShortcut(shortcut, action);
}

void effects_handler_impl::registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                                   Qt::MouseButton pointerButtons,
                                                   QAction* action)
{
    kwinApp()->input->redirect->registerPointerShortcut(modifiers, pointerButtons, action);
}

void effects_handler_impl::registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                                                PointerAxisDirection axis,
                                                QAction* action)
{
    kwinApp()->input->redirect->registerAxisShortcut(modifiers, axis, action);
}

void effects_handler_impl::registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action)
{
    kwinApp()->input->redirect->registerTouchpadSwipeShortcut(direction, action);
}

void* effects_handler_impl::getProxy(QString name)
{
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin();
         it != loaded_effects.constEnd();
         ++it)
        if ((*it).first == name)
            return (*it).second->proxy();

    return nullptr;
}

void effects_handler_impl::startMousePolling()
{
    if (auto cursor = input::get_cursor()) {
        cursor->start_mouse_polling();
    }
}

void effects_handler_impl::stopMousePolling()
{
    if (auto cursor = input::get_cursor()) {
        cursor->stop_mouse_polling();
    }
}

bool effects_handler_impl::hasKeyboardGrab() const
{
    return keyboard_grab_effect != nullptr;
}

void effects_handler_impl::desktopResized(const QSize& size)
{
    m_scene->handle_screen_geometry_change(size);
    Q_EMIT screenGeometryChanged(size);
}

void effects_handler_impl::registerPropertyType(long atom, bool reg)
{
    if (reg)
        ++registered_atoms[atom]; // initialized to 0 if not present yet
    else {
        if (--registered_atoms[atom] == 0)
            registered_atoms.remove(atom);
    }
}

xcb_atom_t effects_handler_impl::announceSupportProperty(const QByteArray& propertyName,
                                                         Effect* effect)
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

void effects_handler_impl::removeSupportProperty(const QByteArray& propertyName, Effect* effect)
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

QByteArray effects_handler_impl::readRootProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return readWindowProperty(kwinApp()->x11RootWindow(), atom, type, format);
}

void effects_handler_impl::activateWindow(EffectWindow* c)
{
    auto window = static_cast<effects_window_impl*>(c)->window();
    if (window && window->control) {
        workspace()->activateClient(window, true);
    }
}

EffectWindow* effects_handler_impl::activeWindow() const
{
    auto ac = workspace()->activeClient();
    return ac ? ac->render->effect.get() : nullptr;
}

void effects_handler_impl::moveWindow(EffectWindow* w,
                                      const QPoint& pos,
                                      bool snap,
                                      double snapAdjust)
{
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (!window || !window->isMovable()) {
        return;
    }

    if (snap) {
        win::move(window, workspace()->adjustClientPosition(window, pos, true, snapAdjust));
    } else {
        win::move(window, pos);
    }
}

void effects_handler_impl::windowToDesktop(EffectWindow* w, int desktop)
{
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (window && window->control && !win::is_desktop(window) && !win::is_dock(window)) {
        workspace()->sendClientToDesktop(window, desktop, true);
    }
}

void effects_handler_impl::windowToDesktops(EffectWindow* w, const QVector<uint>& desktopIds)
{
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (!window || !window->control || win::is_desktop(window) || win::is_dock(window)) {
        return;
    }
    QVector<win::virtual_desktop*> desktops;
    desktops.reserve(desktopIds.count());
    for (uint x11Id : desktopIds) {
        if (x11Id > win::virtual_desktop_manager::self()->count()) {
            continue;
        }
        auto d = win::virtual_desktop_manager::self()->desktopForX11Id(x11Id);
        Q_ASSERT(d);
        if (desktops.contains(d)) {
            continue;
        }
        desktops << d;
    }
    win::set_desktops(window, desktops);
}

void effects_handler_impl::windowToScreen(EffectWindow* w, int screen)
{
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (window && window->control && !win::is_desktop(window) && !win::is_dock(window))
        workspace()->sendClientToScreen(window, screen);
}

void effects_handler_impl::setShowingDesktop(bool showing)
{
    workspace()->setShowingDesktop(showing);
}

QString effects_handler_impl::currentActivity() const
{
    return QString();
}

int effects_handler_impl::currentDesktop() const
{
    return win::virtual_desktop_manager::self()->current();
}

int effects_handler_impl::numberOfDesktops() const
{
    return win::virtual_desktop_manager::self()->count();
}

void effects_handler_impl::setCurrentDesktop(int desktop)
{
    win::virtual_desktop_manager::self()->setCurrent(desktop);
}

void effects_handler_impl::setNumberOfDesktops(int desktops)
{
    win::virtual_desktop_manager::self()->setCount(desktops);
}

QSize effects_handler_impl::desktopGridSize() const
{
    return win::virtual_desktop_manager::self()->grid().size();
}

int effects_handler_impl::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int effects_handler_impl::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int effects_handler_impl::workspaceWidth() const
{
    return desktopGridWidth() * kwinApp()->get_base().topology.size.width();
}

int effects_handler_impl::workspaceHeight() const
{
    return desktopGridHeight() * kwinApp()->get_base().topology.size.height();
}

int effects_handler_impl::desktopAtCoords(QPoint coords) const
{
    if (auto vd = win::virtual_desktop_manager::self()->grid().at(coords)) {
        return vd->x11DesktopNumber();
    }
    return 0;
}

QPoint effects_handler_impl::desktopGridCoords(int id) const
{
    return win::virtual_desktop_manager::self()->grid().gridCoords(id);
}

QPoint effects_handler_impl::desktopCoords(int id) const
{
    QPoint coords = win::virtual_desktop_manager::self()->grid().gridCoords(id);
    if (coords.x() == -1) {
        return QPoint(-1, -1);
    }
    auto const& space_size = kwinApp()->get_base().topology.size;
    return QPoint(coords.x() * space_size.width(), coords.y() * space_size.height());
}

int effects_handler_impl::desktopAbove(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_above>(desktop, wrap);
}

int effects_handler_impl::desktopToRight(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_right>(desktop, wrap);
}

int effects_handler_impl::desktopBelow(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_below>(desktop, wrap);
}

int effects_handler_impl::desktopToLeft(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_left>(desktop, wrap);
}

QString effects_handler_impl::desktopName(int desktop) const
{
    return win::virtual_desktop_manager::self()->name(desktop);
}

bool effects_handler_impl::optionRollOverDesktops() const
{
    return kwinApp()->options->isRollOverDesktops();
}

double effects_handler_impl::animationTimeFactor() const
{
    return kwinApp()->options->animationTimeFactor();
}

WindowQuadType effects_handler_impl::newWindowQuadType()
{
    return WindowQuadType(next_window_quad_type++);
}

EffectWindow* effects_handler_impl::find_window_by_wid(WId id) const
{
    if (auto w = workspace()->findClient(win::x11::predicate_match::window, id)) {
        return w->render->effect.get();
    }
    if (auto unmanaged = workspace()->findUnmanaged(id)) {
        return unmanaged->render->effect.get();
    }
    return nullptr;
}

EffectWindow*
effects_handler_impl::find_window_by_surface(Wrapland::Server::Surface* /*surface*/) const
{
    return nullptr;
}

EffectWindow* effects_handler_impl::find_window_by_qwindow(QWindow* w) const
{
    if (Toplevel* toplevel = workspace()->findInternal(w)) {
        return toplevel->render->effect.get();
    }
    return nullptr;
}

EffectWindow* effects_handler_impl::find_window_by_uuid(const QUuid& id) const
{
    auto const toplevel
        = workspace()->findToplevel([&id](Toplevel const* t) { return t->internalId() == id; });
    return toplevel ? toplevel->render->effect.get() : nullptr;
}

EffectWindowList effects_handler_impl::stackingOrder() const
{
    auto list = workspace()->x_stacking_tree->as_list();
    EffectWindowList ret;
    for (auto t : list) {
        if (EffectWindow* w = effectWindow(t))
            ret.append(w);
    }
    return ret;
}

void effects_handler_impl::setElevatedWindow(KWin::EffectWindow* w, bool set)
{
    elevated_windows.removeAll(w);
    if (set)
        elevated_windows.append(w);
}

void effects_handler_impl::setTabBoxWindow(EffectWindow* w)
{
#ifdef KWIN_BUILD_TABBOX
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (window->control) {
        win::tabbox::self()->set_current_client(window);
    }
#else
    Q_UNUSED(w)
#endif
}

void effects_handler_impl::setTabBoxDesktop(int desktop)
{
#ifdef KWIN_BUILD_TABBOX
    win::tabbox::self()->set_current_desktop(desktop);
#else
    Q_UNUSED(desktop)
#endif
}

EffectWindowList effects_handler_impl::currentTabBoxWindowList() const
{
#ifdef KWIN_BUILD_TABBOX
    const auto clients = win::tabbox::self()->current_client_list();
    EffectWindowList ret;
    ret.reserve(clients.size());
    std::transform(std::cbegin(clients),
                   std::cend(clients),
                   std::back_inserter(ret),
                   [](auto client) { return client->render->effect.get(); });
    return ret;
#else
    return EffectWindowList();
#endif
}

void effects_handler_impl::refTabBox()
{
#ifdef KWIN_BUILD_TABBOX
    win::tabbox::self()->reference();
#endif
}

void effects_handler_impl::unrefTabBox()
{
#ifdef KWIN_BUILD_TABBOX
    win::tabbox::self()->unreference();
#endif
}

void effects_handler_impl::closeTabBox()
{
#ifdef KWIN_BUILD_TABBOX
    win::tabbox::self()->close();
#endif
}

QList<int> effects_handler_impl::currentTabBoxDesktopList() const
{
#ifdef KWIN_BUILD_TABBOX
    return win::tabbox::self()->current_desktop_list();
#else
    return QList<int>();
#endif
}

int effects_handler_impl::currentTabBoxDesktop() const
{
#ifdef KWIN_BUILD_TABBOX
    return win::tabbox::self()->current_desktop();
#else
    return -1;
#endif
}

EffectWindow* effects_handler_impl::currentTabBoxWindow() const
{
#ifdef KWIN_BUILD_TABBOX
    if (auto c = win::tabbox::self()->current_client())
        return c->render->effect.get();
#endif
    return nullptr;
}

void effects_handler_impl::addRepaintFull()
{
    m_compositor->addRepaintFull();
}

void effects_handler_impl::addRepaint(const QRect& r)
{
    m_compositor->addRepaint(r);
}

void effects_handler_impl::addRepaint(const QRegion& r)
{
    m_compositor->addRepaint(r);
}

void effects_handler_impl::addRepaint(int x, int y, int w, int h)
{
    m_compositor->addRepaint(x, y, w, h);
}

int effects_handler_impl::activeScreen() const
{
    return win::get_current_output(*workspace());
}

int effects_handler_impl::numScreens() const
{
    return kwinApp()->get_base().get_outputs().size();
}

int effects_handler_impl::screenNumber(const QPoint& pos) const
{
    return base::get_nearest_output(kwinApp()->get_base().get_outputs(), pos);
}

QRect effects_handler_impl::clientArea(clientAreaOption opt, int screen, int desktop) const
{
    return workspace()->clientArea(opt, screen, desktop);
}

QRect effects_handler_impl::clientArea(clientAreaOption opt, const EffectWindow* c) const
{
    auto window = static_cast<effects_window_impl const*>(c)->window();
    if (window->control) {
        return workspace()->clientArea(opt, window);
    } else {
        return workspace()->clientArea(
            opt, window->frameGeometry().center(), win::virtual_desktop_manager::self()->current());
    }
}

QRect effects_handler_impl::clientArea(clientAreaOption opt, const QPoint& p, int desktop) const
{
    return workspace()->clientArea(opt, p, desktop);
}

QRect effects_handler_impl::virtualScreenGeometry() const
{
    return QRect({}, kwinApp()->get_base().topology.size);
}

QSize effects_handler_impl::virtualScreenSize() const
{
    return kwinApp()->get_base().topology.size;
}

void effects_handler_impl::defineCursor(Qt::CursorShape shape)
{
    kwinApp()->input->redirect->pointer()->setEffectsOverrideCursor(shape);
}

bool effects_handler_impl::checkInputWindowEvent(QMouseEvent* e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (auto const& effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

bool effects_handler_impl::checkInputWindowEvent(QWheelEvent* e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (auto const& effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

void effects_handler_impl::connectNotify(const QMetaMethod& signal)
{
    if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
        if (!m_trackingCursorChanges) {
            connect(input::get_cursor(),
                    &input::cursor::image_changed,
                    this,
                    &EffectsHandler::cursorShapeChanged);
            input::get_cursor()->start_image_tracking();
        }
        ++m_trackingCursorChanges;
    }
    EffectsHandler::connectNotify(signal);
}

void effects_handler_impl::disconnectNotify(const QMetaMethod& signal)
{
    if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
        Q_ASSERT(m_trackingCursorChanges > 0);
        if (!--m_trackingCursorChanges) {
            input::get_cursor()->stop_image_tracking();
            disconnect(input::get_cursor(),
                       &input::cursor::image_changed,
                       this,
                       &EffectsHandler::cursorShapeChanged);
        }
    }
    EffectsHandler::disconnectNotify(signal);
}

void effects_handler_impl::checkInputWindowStacking()
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return;
    }
    doCheckInputWindowStacking();
}

void effects_handler_impl::doCheckInputWindowStacking()
{
}

QPoint effects_handler_impl::cursorPos() const
{
    return input::get_cursor()->pos();
}

void effects_handler_impl::reserveElectricBorder(ElectricBorder border, Effect* effect)
{
    workspace()->edges->reserve(border, effect, "borderActivated");
}

void effects_handler_impl::unreserveElectricBorder(ElectricBorder border, Effect* effect)
{
    workspace()->edges->unreserve(border, effect);
}

void effects_handler_impl::registerTouchBorder(ElectricBorder border, QAction* action)
{
    workspace()->edges->reserveTouch(border, action);
}

void effects_handler_impl::unregisterTouchBorder(ElectricBorder border, QAction* action)
{
    workspace()->edges->unreserveTouch(border, action);
}

unsigned long effects_handler_impl::xrenderBufferPicture()
{
    return m_scene->xrenderBufferPicture();
}

QPainter* effects_handler_impl::scenePainter()
{
    return m_scene->scenePainter();
}

void effects_handler_impl::toggleEffect(const QString& name)
{
    if (isEffectLoaded(name))
        unloadEffect(name);
    else
        loadEffect(name);
}

QStringList effects_handler_impl::loadedEffects() const
{
    QStringList listModules;
    listModules.reserve(loaded_effects.count());
    std::transform(loaded_effects.constBegin(),
                   loaded_effects.constEnd(),
                   std::back_inserter(listModules),
                   [](const EffectPair& pair) { return pair.first; });
    return listModules;
}

QStringList effects_handler_impl::listOfEffects() const
{
    return m_effectLoader->listOfKnownEffects();
}

bool effects_handler_impl::loadEffect(const QString& name)
{
    makeOpenGLContextCurrent();
    m_compositor->addRepaintFull();

    return m_effectLoader->loadEffect(name);
}

void effects_handler_impl::unloadEffect(const QString& name)
{
    auto it = std::find_if(effect_order.begin(), effect_order.end(), [name](EffectPair& pair) {
        return pair.first == name;
    });
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

void effects_handler_impl::destroyEffect(Effect* effect)
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
    for (const QByteArray& property : properties) {
        removeSupportProperty(property, effect);
    }

    delete effect;
}

void effects_handler_impl::reconfigureEffect(const QString& name)
{
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin();
         it != loaded_effects.constEnd();
         ++it)
        if ((*it).first == name) {
            kwinApp()->config()->reparseConfiguration();
            makeOpenGLContextCurrent();
            (*it).second->reconfigure(Effect::ReconfigureAll);
            return;
        }
}

bool effects_handler_impl::isEffectLoaded(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [&name](const EffectPair& pair) { return pair.first == name; });
    return it != loaded_effects.constEnd();
}

bool effects_handler_impl::isEffectSupported(const QString& name)
{
    // If the effect is loaded, it is obviously supported.
    if (isEffectLoaded(name)) {
        return true;
    }

    // next checks might require a context
    makeOpenGLContextCurrent();

    return m_effectLoader->isEffectSupported(name);
}

QList<bool> effects_handler_impl::areEffectsSupported(const QStringList& names)
{
    QList<bool> retList;
    retList.reserve(names.count());
    std::transform(names.constBegin(),
                   names.constEnd(),
                   std::back_inserter(retList),
                   [this](const QString& name) { return isEffectSupported(name); });
    return retList;
}

void effects_handler_impl::reloadEffect(Effect* effect)
{
    QString effectName;
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin();
         it != loaded_effects.constEnd();
         ++it) {
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

void effects_handler_impl::effectsChanged()
{
    loaded_effects.clear();
    m_activeEffects.clear(); // it's possible to have a reconfigure and a quad rebuild between two
                             // paint cycles - bug #308201

    loaded_effects.reserve(effect_order.count());
    std::copy(
        effect_order.constBegin(), effect_order.constEnd(), std::back_inserter(loaded_effects));

    m_activeEffects.reserve(loaded_effects.count());
}

QStringList effects_handler_impl::activeEffects() const
{
    QStringList ret;
    for (QVector<KWin::EffectPair>::const_iterator it = loaded_effects.constBegin(),
                                                   end = loaded_effects.constEnd();
         it != end;
         ++it) {
        if (it->second->isActive()) {
            ret << it->first;
        }
    }
    return ret;
}

Wrapland::Server::Display* effects_handler_impl::waylandDisplay() const
{
    return nullptr;
}

EffectFrame* effects_handler_impl::effectFrame(EffectFrameStyle style,
                                               bool staticSize,
                                               const QPoint& position,
                                               Qt::Alignment alignment) const
{
    return new effect_frame_impl(style, staticSize, position, alignment);
}

QVariant effects_handler_impl::kwinOption(KWinOption kwopt)
{
    switch (kwopt) {
    case CloseButtonCorner: {
        // TODO: this could become per window and be derived from the actual position in the deco
        auto deco_settings = Decoration::DecorationBridge::self()->settings();
        auto close_enum = KDecoration2::DecorationButtonType::Close;
        return deco_settings && deco_settings->decorationButtonsLeft().contains(close_enum)
            ? Qt::TopLeftCorner
            : Qt::TopRightCorner;
    }
    case SwitchDesktopOnScreenEdge:
        return workspace()->edges->desktop_switching.always;
    case SwitchDesktopOnScreenEdgeMovingWindows:
        return workspace()->edges->desktop_switching.when_moving_client;
    default:
        return QVariant(); // an invalid one
    }
}

QString effects_handler_impl::supportInformation(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [name](const EffectPair& pair) { return pair.first == name; });
    if (it == loaded_effects.constEnd()) {
        return QString();
    }

    QString support((*it).first + QLatin1String(":\n"));
    const QMetaObject* metaOptions = (*it).second->metaObject();
    for (int i = 0; i < metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (qstrcmp(property.name(), "objectName") == 0) {
            continue;
        }
        support += QString::fromUtf8(property.name()) + QLatin1String(": ")
            + (*it).second->property(property.name()).toString() + QLatin1Char('\n');
    }

    return support;
}

bool effects_handler_impl::isScreenLocked() const
{
    return kwinApp()->screen_locker_watcher->is_locked();
}

QString effects_handler_impl::debug(const QString& name, const QString& parameter) const
{
    QString internalName = name.toLower();
    ;
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin();
         it != loaded_effects.constEnd();
         ++it) {
        if ((*it).first == internalName) {
            return it->second->debug(parameter);
        }
    }
    return QString();
}

bool effects_handler_impl::makeOpenGLContextCurrent()
{
    return m_scene->makeOpenGLContextCurrent();
}

void effects_handler_impl::doneOpenGLContextCurrent()
{
    m_scene->doneOpenGLContextCurrent();
}

bool effects_handler_impl::animationsSupported() const
{
    static const QByteArray forceEnvVar = qgetenv("KWIN_EFFECTS_FORCE_ANIMATIONS");
    if (!forceEnvVar.isEmpty()) {
        static const int forceValue = forceEnvVar.toInt();
        return forceValue == 1;
    }
    return m_scene->animationsSupported();
}

void effects_handler_impl::highlightWindows(const QVector<EffectWindow*>& windows)
{
    Effect* e = provides(Effect::HighlightWindows);
    if (!e) {
        return;
    }
    e->perform(Effect::HighlightWindows, QVariantList{QVariant::fromValue(windows)});
}

PlatformCursorImage effects_handler_impl::cursorImage() const
{
    return kwinApp()->input->cursor->platform_image();
}

void effects_handler_impl::hideCursor()
{
    kwinApp()->input->cursor->hide();
}

void effects_handler_impl::showCursor()
{
    kwinApp()->input->cursor->show();
}

void effects_handler_impl::startInteractiveWindowSelection(
    std::function<void(KWin::EffectWindow*)> callback)
{
    kwinApp()->input->start_interactive_window_selection([callback](KWin::Toplevel* t) {
        if (t) {
            assert(t->render);
            assert(t->render->effect);
            callback(t->render->effect.get());
        } else {
            callback(nullptr);
        }
    });
}

void effects_handler_impl::startInteractivePositionSelection(
    std::function<void(const QPoint&)> callback)
{
    kwinApp()->input->start_interactive_position_selection(callback);
}

void effects_handler_impl::showOnScreenMessage(const QString& message, const QString& iconName)
{
    win::osd_show(message, iconName);
}

void effects_handler_impl::hideOnScreenMessage(OnScreenMessageHideFlags flags)
{
    win::osd_hide_flags internal_flags{};
    if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
        internal_flags |= win::osd_hide_flags::skip_close_animation;
    }
    win::osd_hide(internal_flags);
}

KSharedConfigPtr effects_handler_impl::config() const
{
    return kwinApp()->config();
}

KSharedConfigPtr effects_handler_impl::inputConfig() const
{
    return kwinApp()->inputConfig();
}

Effect* effects_handler_impl::findEffect(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [name](const EffectPair& pair) { return pair.first == name; });
    if (it == loaded_effects.constEnd()) {
        return nullptr;
    }
    return (*it).second;
}

void effects_handler_impl::renderEffectQuickView(EffectQuickView* w) const
{
    if (!w->isVisible()) {
        return;
    }
    scene()->paintEffectQuickView(w);
}

SessionState effects_handler_impl::sessionState() const
{
    return workspace()->sessionManager()->state();
}

QList<EffectScreen*> effects_handler_impl::screens() const
{
    return m_effectScreens;
}

EffectScreen* effects_handler_impl::screenAt(const QPoint& point) const
{
    return m_effectScreens.value(screenNumber(point));
}

EffectScreen* effects_handler_impl::findScreen(const QString& name) const
{
    for (EffectScreen* screen : qAsConst(m_effectScreens)) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return nullptr;
}

EffectScreen* effects_handler_impl::findScreen(int screenId) const
{
    return m_effectScreens.value(screenId);
}

void effects_handler_impl::slotOutputEnabled(base::output* output)
{
    EffectScreen* screen = new effect_screen_impl(output, this);
    m_effectScreens.append(screen);
    Q_EMIT screenAdded(screen);
}

void effects_handler_impl::slotOutputDisabled(base::output* output)
{
    auto it = std::find_if(
        m_effectScreens.begin(), m_effectScreens.end(), [&output](EffectScreen* screen) {
            return static_cast<effect_screen_impl*>(screen)->platformOutput() == output;
        });
    if (it != m_effectScreens.end()) {
        EffectScreen* screen = *it;
        m_effectScreens.erase(it);
        Q_EMIT screenRemoved(screen);
        delete screen;
    }
}

bool effects_handler_impl::isCursorHidden() const
{
    return input::get_cursor()->is_hidden();
}

//****************************************
// effect_screen_impl
//****************************************

effect_screen_impl::effect_screen_impl(base::output* output, QObject* parent)
    : EffectScreen(parent)
    , m_platformOutput(output)
{
    connect(output, &base::output::wake_up, this, &EffectScreen::wakeUp);
    connect(output, &base::output::about_to_turn_off, this, &EffectScreen::aboutToTurnOff);
    connect(output, &base::output::scale_changed, this, &EffectScreen::devicePixelRatioChanged);
    connect(output, &base::output::geometry_changed, this, &EffectScreen::geometryChanged);
}

base::output* effect_screen_impl::platformOutput() const
{
    return m_platformOutput;
}

QString effect_screen_impl::name() const
{
    return m_platformOutput->name();
}

qreal effect_screen_impl::devicePixelRatio() const
{
    return m_platformOutput->scale();
}

QRect effect_screen_impl::geometry() const
{
    return m_platformOutput->geometry();
}

//****************************************
// effects_window_impl
//****************************************

effects_window_impl::effects_window_impl(Toplevel* toplevel)
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
    x11Client
        = qobject_cast<KWin::win::x11::window*>(toplevel) != nullptr || toplevel->xcb_window();
}

effects_window_impl::~effects_window_impl()
{
    QVariant cachedTextureVariant = data(LanczosCacheRole);
    if (cachedTextureVariant.isValid()) {
        GLTexture* cachedTexture = static_cast<GLTexture*>(cachedTextureVariant.value<void*>());
        delete cachedTexture;
    }
}

bool effects_window_impl::isPaintingEnabled()
{
    return sceneWindow()->isPaintingEnabled();
}

void effects_window_impl::enablePainting(int reason)
{
    sceneWindow()->enablePainting(static_cast<render::window_paint_disable_type>(reason));
}

void effects_window_impl::disablePainting(int reason)
{
    sceneWindow()->disablePainting(static_cast<render::window_paint_disable_type>(reason));
}

void effects_window_impl::addRepaint(const QRect& r)
{
    toplevel->addRepaint(r);
}

void effects_window_impl::addRepaint(int x, int y, int w, int h)
{
    toplevel->addRepaint(x, y, w, h);
}

void effects_window_impl::addRepaintFull()
{
    toplevel->addRepaintFull();
}

void effects_window_impl::addLayerRepaint(const QRect& r)
{
    toplevel->addLayerRepaint(r);
}

void effects_window_impl::addLayerRepaint(int x, int y, int w, int h)
{
    toplevel->addLayerRepaint(x, y, w, h);
}

const EffectWindowGroup* effects_window_impl::group() const
{
    if (auto c = qobject_cast<win::x11::window*>(toplevel)) {
        return c->group()->effectGroup();
    }
    return nullptr; // TODO
}

void effects_window_impl::refWindow()
{
    if (toplevel->transient()->annexed) {
        return;
    }
    if (auto remnant = toplevel->remnant()) {
        return remnant->ref();
    }
    abort(); // TODO
}

void effects_window_impl::unrefWindow()
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

QRect effects_window_impl::rect() const
{
    return QRect(QPoint(), toplevel->size());
}

#define TOPLEVEL_HELPER(rettype, prototype, toplevelPrototype)                                     \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        return toplevel->toplevelPrototype();                                                      \
    }

TOPLEVEL_HELPER(double, opacity, opacity)
TOPLEVEL_HELPER(bool, hasAlpha, hasAlpha)
TOPLEVEL_HELPER(int, x, pos().x)
TOPLEVEL_HELPER(int, y, pos().y)
TOPLEVEL_HELPER(int, width, size().width)
TOPLEVEL_HELPER(int, height, size().height)
TOPLEVEL_HELPER(QPoint, pos, pos)
TOPLEVEL_HELPER(QSize, size, size)
TOPLEVEL_HELPER(QRect, geometry, frameGeometry)
TOPLEVEL_HELPER(QRect, frameGeometry, frameGeometry)
TOPLEVEL_HELPER(int, desktop, desktop)
TOPLEVEL_HELPER(bool, isDeleted, isDeleted)
TOPLEVEL_HELPER(QString, windowRole, windowRole)
TOPLEVEL_HELPER(bool, skipsCloseAnimation, skipsCloseAnimation)
TOPLEVEL_HELPER(Wrapland::Server::Surface*, surface, surface)
TOPLEVEL_HELPER(bool, isOutline, isOutline)
TOPLEVEL_HELPER(bool, isLockScreen, isLockScreen)
TOPLEVEL_HELPER(pid_t, pid, pid)
TOPLEVEL_HELPER(bool, isModal, transient()->modal)

#undef TOPLEVEL_HELPER

#define TOPLEVEL_HELPER_WIN(rettype, prototype, function)                                          \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        return win::function(toplevel);                                                            \
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

#define CLIENT_HELPER_WITH_DELETED_WIN(rettype, prototype, propertyname, defaultValue)             \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control || toplevel->remnant()) {                                            \
            return win::propertyname(toplevel);                                                    \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER_WITH_DELETED_WIN(QString, caption, caption, QString())
CLIENT_HELPER_WITH_DELETED_WIN(QVector<uint>, desktops, x11_desktop_ids, QVector<uint>())

#undef CLIENT_HELPER_WITH_DELETED_WIN

#define CLIENT_HELPER_WITH_DELETED_WIN_CTRL(rettype, prototype, propertyname, defaultValue)        \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control) {                                                                   \
            return toplevel->control->propertyname();                                              \
        }                                                                                          \
        if (auto remnant = toplevel->remnant()) {                                                  \
            return remnant->propertyname;                                                          \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, keepAbove, keep_above, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, keepBelow, keep_below, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, isMinimized, minimized, false)
CLIENT_HELPER_WITH_DELETED_WIN_CTRL(bool, isFullScreen, fullscreen, false)

#undef CLIENT_HELPER_WITH_DELETED_WIN_CTRL

QStringList effects_window_impl::activities() const
{
    // No support for Activities.
    return {};
}

int effects_window_impl::screen() const
{
    return base::get_output_index(kwinApp()->get_base().get_outputs(), toplevel->central_output);
}

QRect effects_window_impl::clientGeometry() const
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

QRect effects_window_impl::expandedGeometry() const
{
    return expanded_geometry_recursion(toplevel);
}

// legacy from tab groups, can be removed when no effects use this any more.
bool effects_window_impl::isCurrentTab() const
{
    return true;
}

QString effects_window_impl::windowClass() const
{
    return toplevel->resourceName() + QLatin1Char(' ') + toplevel->resourceClass();
}

QRect effects_window_impl::contentsRect() const
{
    // TODO(romangg): This feels kind of wrong. Why are the frame extents not part of it (i.e. just
    //                using frame_to_client_rect)? But some clients rely on the current version,
    //                for example Latte for its behind-dock blur.
    auto const deco_offset = QPoint(win::left_border(toplevel), win::top_border(toplevel));
    auto const client_size = win::frame_relative_client_rect(toplevel).size();

    return QRect(deco_offset, client_size);
}

NET::WindowType effects_window_impl::windowType() const
{
    return toplevel->windowType();
}

#define CLIENT_HELPER(rettype, prototype, propertyname, defaultValue)                              \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control) {                                                                   \
            return toplevel->propertyname();                                                       \
        }                                                                                          \
        return defaultValue;                                                                       \
    }

CLIENT_HELPER(bool, isMovable, isMovable, false)
CLIENT_HELPER(bool, isMovableAcrossScreens, isMovableAcrossScreens, false)
CLIENT_HELPER(QRect, iconGeometry, iconGeometry, QRect())
CLIENT_HELPER(bool, acceptsFocus, wantsInput, true) // We don't actually know...

#undef CLIENT_HELPER

#define CLIENT_HELPER_WIN(rettype, prototype, function, default_value)                             \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control) {                                                                   \
            return win::function(toplevel);                                                        \
        }                                                                                          \
        return default_value;                                                                      \
    }

CLIENT_HELPER_WIN(bool, isSpecialWindow, is_special_window, true)
CLIENT_HELPER_WIN(bool, isUserMove, is_move, false)
CLIENT_HELPER_WIN(bool, isUserResize, is_resize, false)
CLIENT_HELPER_WIN(bool, decorationHasAlpha, decoration_has_alpha, false)

#undef CLIENT_HELPER_WIN

#define CLIENT_HELPER_WIN_CONTROL(rettype, prototype, function, default_value)                     \
    rettype effects_window_impl::prototype() const                                                 \
    {                                                                                              \
        if (toplevel->control) {                                                                   \
            return toplevel->control->function();                                                  \
        }                                                                                          \
        return default_value;                                                                      \
    }

CLIENT_HELPER_WIN_CONTROL(bool, isSkipSwitcher, skip_switcher, false)
CLIENT_HELPER_WIN_CONTROL(QIcon, icon, icon, QIcon())
CLIENT_HELPER_WIN_CONTROL(bool, isUnresponsive, unresponsive, false)

#undef CLIENT_HELPER_WIN_CONTROL

QSize effects_window_impl::basicUnit() const
{
    if (auto client = qobject_cast<win::x11::window*>(toplevel)) {
        return client->basicUnit();
    }
    return QSize(1, 1);
}

void effects_window_impl::setWindow(Toplevel* w)
{
    toplevel = w;
    setParent(w);
}

void effects_window_impl::setSceneWindow(render::window* w)
{
    sw = w;
}

QRect effects_window_impl::decorationInnerRect() const
{
    return contentsRect();
}

QByteArray effects_window_impl::readProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return readWindowProperty(window()->xcb_window(), atom, type, format);
}

void effects_window_impl::deleteProperty(long int atom) const
{
    if (kwinApp()->x11Connection()) {
        deleteWindowProperty(window()->xcb_window(), atom);
    }
}

EffectWindow* effects_window_impl::findModal()
{
    if (!toplevel->control) {
        return nullptr;
    }

    auto modal = toplevel->findModal();
    if (modal) {
        return modal->render->effect.get();
    }

    return nullptr;
}

EffectWindow* effects_window_impl::transientFor()
{
    if (!toplevel->control) {
        return nullptr;
    }

    auto transientFor = toplevel->transient()->lead();
    if (transientFor) {
        return transientFor->render->effect.get();
    }

    return nullptr;
}

QWindow* effects_window_impl::internalWindow() const
{
    auto client = qobject_cast<win::internal_window*>(toplevel);
    if (!client) {
        return nullptr;
    }
    return client->internalWindow();
}

template<typename T>
EffectWindowList getMainWindows(T* c)
{
    const auto leads = c->transient()->leads();
    EffectWindowList ret;
    ret.reserve(leads.size());
    std::transform(std::cbegin(leads), std::cend(leads), std::back_inserter(ret), [](auto client) {
        return client->render->effect.get();
    });
    return ret;
}

EffectWindowList effects_window_impl::mainWindows() const
{
    if (toplevel->control || toplevel->remnant()) {
        return getMainWindows(toplevel);
    }
    return {};
}

WindowQuadList effects_window_impl::buildQuads(bool force) const
{
    return sceneWindow()->buildQuads(force);
}

void effects_window_impl::setData(int role, const QVariant& data)
{
    if (!data.isNull())
        dataMap[role] = data;
    else
        dataMap.remove(role);
    Q_EMIT effects->windowDataChanged(this, role);
}

QVariant effects_window_impl::data(int role) const
{
    return dataMap.value(role);
}

EffectWindow* effectWindow(Toplevel* w)
{
    return w->render->effect.get();
}

void effects_window_impl::elevate(bool elevate)
{
    effects->setElevatedWindow(this, elevate);
}

void effects_window_impl::registerThumbnail(basic_thumbnail_item* item)
{
    if (auto thumb = qobject_cast<window_thumbnail_item*>(item)) {
        insertThumbnail(thumb);
        connect(thumb, &QObject::destroyed, this, &effects_window_impl::thumbnailDestroyed);
        connect(thumb,
                &window_thumbnail_item::wIdChanged,
                this,
                &effects_window_impl::thumbnailTargetChanged);
    } else if (auto desktopThumb = qobject_cast<desktop_thumbnail_item*>(item)) {
        m_desktopThumbnails.append(desktopThumb);
        connect(desktopThumb,
                &QObject::destroyed,
                this,
                &effects_window_impl::desktopThumbnailDestroyed);
    }
}

void effects_window_impl::thumbnailDestroyed(QObject* object)
{
    // we know it is a window_thumbnail_item
    m_thumbnails.remove(static_cast<window_thumbnail_item*>(object));
}

void effects_window_impl::thumbnailTargetChanged()
{
    if (auto item = qobject_cast<window_thumbnail_item*>(sender())) {
        insertThumbnail(item);
    }
}

void effects_window_impl::insertThumbnail(window_thumbnail_item* item)
{
    EffectWindow* w = effects->findWindow(item->wId());
    if (w) {
        m_thumbnails.insert(item,
                            QPointer<effects_window_impl>(static_cast<effects_window_impl*>(w)));
    } else {
        m_thumbnails.insert(item, QPointer<effects_window_impl>());
    }
}

void effects_window_impl::desktopThumbnailDestroyed(QObject* object)
{
    // we know it is a desktop_thumbnail_item
    m_desktopThumbnails.removeAll(static_cast<desktop_thumbnail_item*>(object));
}

void effects_window_impl::minimize()
{
    if (toplevel->control) {
        win::set_minimized(toplevel, true);
    }
}

void effects_window_impl::unminimize()
{
    if (toplevel->control) {
        win::set_minimized(toplevel, false);
    }
}

void effects_window_impl::closeWindow()
{
    if (toplevel->control) {
        toplevel->closeWindow();
    }
}

void effects_window_impl::referencePreviousWindowPixmap()
{
    if (sw) {
        sw->referencePreviousPixmap();
    }
}

void effects_window_impl::unreferencePreviousWindowPixmap()
{
    if (sw) {
        sw->unreferencePreviousPixmap();
    }
}

bool effects_window_impl::isManaged() const
{
    return managed;
}

bool effects_window_impl::isWaylandClient() const
{
    return waylandClient;
}

bool effects_window_impl::isX11Client() const
{
    return x11Client;
}

//****************************************
// effect_window_group_impl
//****************************************

EffectWindowList effect_window_group_impl::members() const
{
    const auto memberList = group->members();
    EffectWindowList ret;
    ret.reserve(memberList.size());
    std::transform(std::cbegin(memberList),
                   std::cend(memberList),
                   std::back_inserter(ret),
                   [](auto toplevel) { return toplevel->render->effect.get(); });
    return ret;
}

//****************************************
// effect_frame_impl
//****************************************

effect_frame_impl::effect_frame_impl(EffectFrameStyle style,
                                     bool staticSize,
                                     QPoint position,
                                     Qt::Alignment alignment)
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
        connect(
            m_theme, &Plasma::Theme::themeChanged, this, &effect_frame_impl::plasmaThemeChanged);
    }
    m_selection.setImagePath(QStringLiteral("widgets/viewitem"));
    m_selection.setElementPrefix(QStringLiteral("hover"));
    m_selection.setCacheAllRenderedFrames(true);
    m_selection.setEnabledBorders(Plasma::FrameSvg::AllBorders);

    m_sceneFrame = render::compositor::self()->scene()->createEffectFrame(this);
}

effect_frame_impl::~effect_frame_impl()
{
    delete m_sceneFrame;
}

const QFont& effect_frame_impl::font() const
{
    return m_font;
}

void effect_frame_impl::setFont(const QFont& font)
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

void effect_frame_impl::free()
{
    m_sceneFrame->free();
}

const QRect& effect_frame_impl::geometry() const
{
    return m_geometry;
}

void effect_frame_impl::setGeometry(const QRect& geometry, bool force)
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
        m_frame.getMargins(left, top, right, bottom); // m_geometry is the inner geometry
        m_frame.resizeFrame(m_geometry.adjusted(-left, -top, right, bottom).size());
    }

    free();
}

const QIcon& effect_frame_impl::icon() const
{
    return m_icon;
}

void effect_frame_impl::setIcon(const QIcon& icon)
{
    m_icon = icon;
    if (isCrossFade()) {
        m_sceneFrame->crossFadeIcon();
    }
    if (m_iconSize.isEmpty()
        && !m_icon.availableSizes().isEmpty()) { // Set a size if we don't already have one
        setIconSize(m_icon.availableSizes().first());
    }
    m_sceneFrame->freeIconFrame();
}

const QSize& effect_frame_impl::iconSize() const
{
    return m_iconSize;
}

void effect_frame_impl::setIconSize(const QSize& size)
{
    if (m_iconSize == size) {
        return;
    }
    m_iconSize = size;
    autoResize();
    m_sceneFrame->freeIconFrame();
}

void effect_frame_impl::plasmaThemeChanged()
{
    free();
}

void effect_frame_impl::render(const QRegion& region, double opacity, double frameOpacity)
{
    if (m_geometry.isEmpty()) {
        return; // Nothing to display
    }
    m_shader = nullptr;
    setScreenProjectionMatrix(
        static_cast<effects_handler_impl*>(effects)->scene()->screenProjectionMatrix());
    effects->paintEffectFrame(this, region, opacity, frameOpacity);
}

void effect_frame_impl::finalRender(QRegion region, double opacity, double frameOpacity) const
{
    region = infiniteRegion(); // TODO: Old region doesn't seem to work with OpenGL

    m_sceneFrame->render(region, opacity, frameOpacity);
}

Qt::Alignment effect_frame_impl::alignment() const
{
    return m_alignment;
}

void effect_frame_impl::align(QRect& geometry)
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

void effect_frame_impl::setAlignment(Qt::Alignment alignment)
{
    m_alignment = alignment;
    align(m_geometry);
    setGeometry(m_geometry);
}

void effect_frame_impl::setPosition(const QPoint& point)
{
    m_point = point;
    QRect geometry
        = m_geometry; // this is important, setGeometry need call repaint for old & new geometry
    align(geometry);
    setGeometry(geometry);
}

const QString& effect_frame_impl::text() const
{
    return m_text;
}

void effect_frame_impl::setText(const QString& text)
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

void effect_frame_impl::setSelection(const QRect& selection)
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

void effect_frame_impl::autoResize()
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

QColor effect_frame_impl::styledTextColor()
{
    return m_theme->color(Plasma::Theme::TextColor);
}

} // namespace
