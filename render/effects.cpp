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

#include "compositor.h"
#include "effect/frame.h"
#include "effect/screen_impl.h"
#include "effect/window_impl.h"
#include "effect_loader.h"
#include "effectsadaptor.h"
#include "gl/backend.h"
#include "gl/scene.h"
#include "platform.h"
#include "singleton_interface.h"
#include "x11/effect.h"
#include "x11/property_notify_filter.h"

#include "base/logging.h"
#include "base/output.h"
#include "base/platform.h"
#include "desktop/screen_locker_watcher.h"
#include "input/cursor.h"
#include "input/pointer_redirect.h"
#include "scripting/effect.h"
#include "win/activation.h"
#include "win/control.h"
#include "win/deco/bridge.h"
#include "win/desktop_get.h"
#include "win/internal_window.h"
#include "win/osd.h"
#include "win/remnant.h"
#include "win/screen.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/virtual_desktops.h"
#include "win/window_area.h"
#include "win/x11/window.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <KDecoration2/DecorationSettings>

namespace KWin::render
{

effects_handler_wrap::effects_handler_wrap(CompositingType type)
    : EffectsHandler(type)
    , m_effectLoader(new effect_loader(*this, this))
{
    qRegisterMetaType<QVector<KWin::EffectWindow*>>();

    singleton_interface::effects = this;
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
}

effects_handler_impl::effects_handler_impl(render::compositor* compositor, render::scene* scene)
    : effects_handler_wrap(scene->compositingType())
    , m_compositor(compositor)
    , m_scene(scene)
{
    singleton_interface::register_thumbnail = [this](auto& eff_win, auto& thumbnail) {
        auto& impl_win = static_cast<render::effects_window_impl&>(eff_win);
        impl_win.registerThumbnail(&thumbnail);
    };

    QObject::connect(this, &effects_handler_impl::hasActiveFullScreenEffectChanged, this, [this] {
        Q_EMIT m_compositor->space->edges->qobject->checkBlocking();
    });

    auto ws = compositor->space;
    auto& vds = ws->virtual_desktop_manager;

    connect(ws->qobject.get(),
            &win::space::qobject_t::showingDesktopChanged,
            this,
            &effects_handler_wrap::showingDesktopChanged);
    connect(ws->qobject.get(),
            &win::space::qobject_t::currentDesktopChanged,
            this,
            [this, space = ws](int old) {
                auto c = space->move_resize_window;
                int const newDesktop = m_compositor->space->virtual_desktop_manager->current();
                if (old != 0 && newDesktop != old) {
                    assert(!c || c->render);
                    assert(!c || c->render->effect);
                    auto eff_win = c ? c->render->effect.get() : nullptr;
                    Q_EMIT desktopChanged(old, newDesktop, eff_win);
                    // TODO: remove in 4.10
                    Q_EMIT desktopChanged(old, newDesktop);
                }
            });
    connect(ws->qobject.get(),
            &win::space::qobject_t::desktopPresenceChanged,
            this,
            [this, space = ws](auto win_id, int old) {
                auto c = space->windows_map.at(win_id);
                assert(c);
                assert(c->render);
                assert(c->render->effect);
                Q_EMIT desktopPresenceChanged(c->render->effect.get(), old, c->desktop());
            });
    connect(ws->qobject.get(),
            &win::space::qobject_t::clientAdded,
            this,
            [this, space = ws](auto win_id) {
                auto c = space->windows_map.at(win_id);
                if (c->ready_for_painting) {
                    slotClientShown(c);
                } else {
                    QObject::connect(c->qobject.get(),
                                     &win::window_qobject::windowShown,
                                     this,
                                     [this, c] { slotClientShown(c); });
                }
            });
    connect(ws->qobject.get(),
            &win::space::qobject_t::unmanagedAdded,
            this,
            [this, space = ws](auto win_id) {
                // it's never initially ready but has synthetic 50ms delay
                auto u = space->windows_map.at(win_id);
                connect(u->qobject.get(), &win::window_qobject::windowShown, this, [this, u] {
                    slotUnmanagedShown(u);
                });
            });
    connect(ws->qobject.get(),
            &win::space::qobject_t::internalClientAdded,
            this,
            [this, space = ws](auto win_id) {
                auto client = space->windows_map.at(win_id);
                assert(client->render);
                assert(client->render->effect);
                setupAbstractClientConnections(client);
                Q_EMIT windowAdded(client->render->effect.get());
            });
    connect(ws->qobject.get(), &win::space::qobject_t::clientActivated, this, [this, space = ws] {
        auto window = space->active_client;
        assert(!window || window->render);
        assert(!window || window->render->effect);
        auto eff_win = window ? window->render->effect.get() : nullptr;
        Q_EMIT windowActivated(eff_win);
    });

    QObject::connect(ws->qobject.get(),
                     &win::space::qobject_t::remnant_created,
                     this,
                     [this, space = ws](auto win_id) {
                         auto win = space->windows_map.at(win_id);
                         add_remnant(win);
                     });

    connect(ws->qobject.get(),
            &win::space::qobject_t::window_deleted,
            this,
            [this, space = ws](auto win_id) {
                auto d = space->windows_map.at(win_id);
                assert(d->render);
                assert(d->render->effect);
                Q_EMIT windowDeleted(d->render->effect.get());
                elevated_windows.removeAll(d->render->effect.get());
            });
    connect(ws->session_manager.get(),
            &win::session_manager::stateChanged,
            this,
            &KWin::EffectsHandler::sessionStateChanged);
    connect(vds->qobject.get(),
            &win::virtual_desktop_manager_qobject::countChanged,
            this,
            &EffectsHandler::numberDesktopsChanged);
    QObject::connect(ws->input->platform.cursor.get(),
                     &input::cursor::mouse_changed,
                     this,
                     &EffectsHandler::mouseChanged);

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

    connect(ws->stacking_order->qobject.get(),
            &win::stacking_order_qobject::changed,
            this,
            &EffectsHandler::stackingOrderChanged);

#if KWIN_BUILD_TABBOX
    auto qt_tabbox = ws->tabbox->qobject.get();
    connect(qt_tabbox, &win::tabbox_qobject::tabbox_added, this, &EffectsHandler::tabBoxAdded);
    connect(qt_tabbox, &win::tabbox_qobject::tabbox_updated, this, &EffectsHandler::tabBoxUpdated);
    connect(qt_tabbox, &win::tabbox_qobject::tabbox_closed, this, &EffectsHandler::tabBoxClosed);
    connect(
        qt_tabbox, &win::tabbox_qobject::tabbox_key_event, this, &EffectsHandler::tabBoxKeyEvent);
#endif

    connect(ws->edges->qobject.get(),
            &win::screen_edger_qobject::approaching,
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

    auto make_property_filter = [this] {
        using filter = x11::property_notify_filter<effects_handler_wrap, win::space>;
        x11_property_notify
            = std::make_unique<filter>(*this, *m_compositor->space, kwinApp()->x11RootWindow());
    };

    connect(kwinApp(), &Application::x11ConnectionChanged, this, [this, make_property_filter] {
        registered_atoms.clear();
        for (auto it = m_propertiesForEffects.keyBegin(); it != m_propertiesForEffects.keyEnd();
             it++) {
            x11::add_support_property(*this, *it);
        }
        if (kwinApp()->x11Connection()) {
            make_property_filter();
        } else {
            x11_property_notify.reset();
        }
        Q_EMIT xcbConnectionChanged();
    });

    if (kwinApp()->x11Connection()) {
        make_property_filter();
    }

    // connect all clients
    for (auto& window : ws->windows) {
        // TODO: Can we merge this with the one for Wayland XdgShellClients below?
        if (!window->control) {
            continue;
        }
        auto x11_client = dynamic_cast<win::x11::window*>(window);
        if (!x11_client) {
            continue;
        }
        setupClientConnections(x11_client);
    }
    for (auto unmanaged : win::x11::get_unmanageds(*ws)) {
        setupUnmanagedConnections(unmanaged);
    }
    for (auto window : ws->windows) {
        if (auto internal = dynamic_cast<win::internal_window*>(window)) {
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
}

effects_handler_impl::~effects_handler_impl()
{
    singleton_interface::register_thumbnail = {};
}

effects_handler_wrap::~effects_handler_wrap()
{
    singleton_interface::effects = nullptr;
}

void effects_handler_wrap::unloadAllEffects()
{
    for (const EffectPair& pair : qAsConst(loaded_effects)) {
        destroyEffect(pair.second);
    }

    effect_order.clear();
    m_effectLoader->clear();

    effectsChanged();
}

void effects_handler_impl::setupAbstractClientConnections(Toplevel* window)
{
    auto qtwin = window->qobject.get();

    QObject::connect(qtwin,
                     &win::window_qobject::maximize_mode_changed,
                     this,
                     [this, window](auto mode) { slotClientMaximized(window, mode); });
    QObject::connect(
        qtwin, &win::window_qobject::clientStartUserMovedResized, this, [this, window] {
            Q_EMIT windowStartUserMovedResized(window->render->effect.get());
        });
    QObject::connect(qtwin,
                     &win::window_qobject::clientStepUserMovedResized,
                     this,
                     [this, window](QRect const& geometry) {
                         Q_EMIT windowStepUserMovedResized(window->render->effect.get(), geometry);
                     });
    QObject::connect(
        qtwin, &win::window_qobject::clientFinishUserMovedResized, this, [this, window] {
            Q_EMIT windowFinishUserMovedResized(window->render->effect.get());
        });
    QObject::connect(qtwin, &win::window_qobject::opacityChanged, this, [this, window](auto old) {
        slotOpacityChanged(window, old);
    });
    QObject::connect(
        qtwin, &win::window_qobject::clientMinimized, this, [this, window](auto animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                Q_EMIT windowMinimized(window->render->effect.get());
            }
        });
    QObject::connect(
        qtwin, &win::window_qobject::clientUnminimized, this, [this, window](auto animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                Q_EMIT windowUnminimized(window->render->effect.get());
            }
        });
    QObject::connect(qtwin, &win::window_qobject::modalChanged, this, [this, window] {
        slotClientModalityChanged(window);
    });
    QObject::connect(qtwin,
                     &win::window_qobject::frame_geometry_changed,
                     this,
                     [this, window](auto const& rect) { slotGeometryShapeChanged(window, rect); });
    QObject::connect(qtwin,
                     &win::window_qobject::frame_geometry_changed,
                     this,
                     [this, window](auto const& rect) { slotFrameGeometryChanged(window, rect); });
    QObject::connect(qtwin, &win::window_qobject::damaged, this, [this, window](auto const& rect) {
        slotWindowDamaged(window, rect);
    });
    QObject::connect(
        qtwin, &win::window_qobject::unresponsiveChanged, this, [this, window](bool unresponsive) {
            Q_EMIT windowUnresponsiveChanged(window->render->effect.get(), unresponsive);
        });
    QObject::connect(qtwin, &win::window_qobject::windowShown, this, [this, window] {
        Q_EMIT windowShown(window->render->effect.get());
    });
    QObject::connect(qtwin, &win::window_qobject::windowHidden, this, [this, window] {
        Q_EMIT windowHidden(window->render->effect.get());
    });
    QObject::connect(
        qtwin, &win::window_qobject::keepAboveChanged, this, [this, window](bool above) {
            Q_UNUSED(above)
            Q_EMIT windowKeepAboveChanged(window->render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::keepBelowChanged, this, [this, window](bool below) {
            Q_UNUSED(below)
            Q_EMIT windowKeepBelowChanged(window->render->effect.get());
        });
    QObject::connect(qtwin, &win::window_qobject::fullScreenChanged, this, [this, window]() {
        Q_EMIT windowFullScreenChanged(window->render->effect.get());
    });
    QObject::connect(qtwin, &win::window_qobject::visible_geometry_changed, this, [this, window]() {
        Q_EMIT windowExpandedGeometryChanged(window->render->effect.get());
    });
}

void effects_handler_impl::setupClientConnections(Toplevel* c)
{
    setupAbstractClientConnections(c);
    connect(c->qobject.get(),
            &win::window_qobject::paddingChanged,
            this,
            [this, c](auto const& old) { slotPaddingChanged(c, old); });
}

void effects_handler_impl::setupUnmanagedConnections(Toplevel* u)
{
    connect(u->qobject.get(), &win::window_qobject::opacityChanged, this, [this, u](auto old) {
        slotOpacityChanged(u, old);
    });
    connect(u->qobject.get(),
            &win::window_qobject::frame_geometry_changed,
            this,
            [this, u](auto const& old) { slotGeometryShapeChanged(u, old); });
    connect(u->qobject.get(),
            &win::window_qobject::frame_geometry_changed,
            this,
            [this, u](auto const& old) { slotFrameGeometryChanged(u, old); });
    connect(u->qobject.get(),
            &win::window_qobject::paddingChanged,
            this,
            [this, u](auto const& old) { slotPaddingChanged(u, old); });
    connect(u->qobject.get(), &win::window_qobject::damaged, this, [this, u](auto const& region) {
        slotWindowDamaged(u, region);
    });
    connect(u->qobject.get(), &win::window_qobject::visible_geometry_changed, this, [this, u]() {
        Q_EMIT windowExpandedGeometryChanged(u->render->effect.get());
    });
}

void effects_handler_wrap::reconfigure()
{
    m_effectLoader->queryAndLoadAll();
}

// the idea is that effects call this function again which calls the next one
void effects_handler_wrap::prePaintScreen(ScreenPrePaintData& data,
                                          std::chrono::milliseconds presentTime)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->prePaintScreen(data, presentTime);
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_wrap::paintScreen(int mask, const QRegion& region, ScreenPaintData& data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->paintScreen(mask, region, data);
        --m_currentPaintScreenIterator;
    } else {
        final_paint_screen(static_cast<render::paint_type>(mask), region, data);
    }
}

void effects_handler_impl::final_paint_screen(paint_type mask,
                                              QRegion const& region,
                                              ScreenPaintData& data)
{
    m_scene->finalPaintScreen(mask, region, data);
}

void effects_handler_wrap::paintDesktop(int desktop,
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
    paintScreen(mask, region, data);
    // restore the saved iterator
    m_currentPaintScreenIterator = savedIterator;
    m_desktopRendering = false;
}

void effects_handler_wrap::postPaintScreen()
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->postPaintScreen();
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void effects_handler_wrap::prePaintWindow(EffectWindow* w,
                                          WindowPrePaintData& data,
                                          std::chrono::milliseconds presentTime)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->prePaintWindow(w, data, presentTime);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

void effects_handler_wrap::paintWindow(EffectWindow* w,
                                       int mask,
                                       const QRegion& region,
                                       WindowPaintData& data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->paintWindow(w, mask, region, data);
        --m_currentPaintWindowIterator;
    } else {
        final_paint_window(w, static_cast<render::paint_type>(mask), region, data);
    }
}

void effects_handler_impl::final_paint_window(EffectWindow* window,
                                              paint_type mask,
                                              QRegion const& region,
                                              WindowPaintData& data)
{
    m_scene->finalPaintWindow(static_cast<effects_window_impl*>(window), mask, region, data);
}

void effects_handler_wrap::postPaintWindow(EffectWindow* w)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->postPaintWindow(w);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

Effect* effects_handler_wrap::provides(Effect::Feature ef)
{
    for (int i = 0; i < loaded_effects.size(); ++i)
        if (loaded_effects.at(i).second->provides(ef))
            return loaded_effects.at(i).second;
    return nullptr;
}

void effects_handler_wrap::drawWindow(EffectWindow* w,
                                      int mask,
                                      const QRegion& region,
                                      WindowPaintData& data)
{
    if (m_currentDrawWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentDrawWindowIterator++)->drawWindow(w, mask, region, data);
        --m_currentDrawWindowIterator;
    } else {
        final_draw_window(w, static_cast<render::paint_type>(mask), region, data);
    }
}

void effects_handler_impl::final_draw_window(EffectWindow* window,
                                             paint_type mask,
                                             QRegion const& region,
                                             WindowPaintData& data)
{
    m_scene->finalDrawWindow(static_cast<effects_window_impl*>(window), mask, region, data);
}

void effects_handler_wrap::buildQuads(EffectWindow* w, WindowQuadList& quadList)
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

bool effects_handler_wrap::hasDecorationShadows() const
{
    return false;
}

bool effects_handler_wrap::decorationsHaveAlpha() const
{
    return true;
}

// start another painting pass
void effects_handler_wrap::startPaint()
{
    m_activeEffects.clear();
    m_activeEffects.reserve(loaded_effects.count());
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->isActive()) {
            m_activeEffects << it->second;
        }
    }
    m_currentDrawWindowIterator = m_activeEffects.constBegin();
    m_currentPaintWindowIterator = m_activeEffects.constBegin();
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
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

    Q_EMIT windowOpacityChanged(
        t->render->effect.get(), oldOpacity, static_cast<qreal>(t->opacity()));
}

void effects_handler_impl::slotClientShown(KWin::Toplevel* t)
{
    assert(dynamic_cast<win::x11::window*>(t));
    disconnect(t->qobject.get(), &win::window_qobject::windowShown, this, nullptr);
    setupClientConnections(t);
    Q_EMIT windowAdded(t->render->effect.get());
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

void effects_handler_impl::add_remnant(Toplevel* remnant)
{
    assert(remnant);
    assert(remnant->render);
    Q_EMIT windowClosed(remnant->render->effect.get());
}

void effects_handler_impl::slotClientModalityChanged(KWin::Toplevel* window)
{
    Q_EMIT windowModalityChanged(window->render->effect.get());
}

void effects_handler_wrap::slotCurrentTabAboutToChange(EffectWindow* from, EffectWindow* to)
{
    Q_EMIT currentTabAboutToChange(from, to);
}

void effects_handler_wrap::slotTabAdded(EffectWindow* w, EffectWindow* to)
{
    Q_EMIT tabAdded(w, to);
}

void effects_handler_wrap::slotTabRemoved(EffectWindow* w, EffectWindow* leaderOfFormerGroup)
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

void effects_handler_wrap::setActiveFullScreenEffect(Effect* e)
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

Effect* effects_handler_wrap::activeFullScreenEffect() const
{
    return fullscreen_effect;
}

bool effects_handler_wrap::hasActiveFullScreenEffect() const
{
    return fullscreen_effect;
}

bool effects_handler_wrap::grabKeyboard(Effect* effect)
{
    if (keyboard_grab_effect != nullptr)
        return false;
    if (!doGrabKeyboard()) {
        return false;
    }
    keyboard_grab_effect = effect;
    return true;
}

bool effects_handler_wrap::doGrabKeyboard()
{
    return true;
}

void effects_handler_wrap::ungrabKeyboard()
{
    Q_ASSERT(keyboard_grab_effect != nullptr);
    doUngrabKeyboard();
    keyboard_grab_effect = nullptr;
}

void effects_handler_wrap::doUngrabKeyboard()
{
}

void effects_handler_wrap::grabbedKeyboardEvent(QKeyEvent* e)
{
    if (keyboard_grab_effect != nullptr)
        keyboard_grab_effect->grabbedKeyboardEvent(e);
}

void effects_handler_wrap::startMouseInterception(Effect* effect, Qt::CursorShape shape)
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

void effects_handler_wrap::stopMouseInterception(Effect* effect)
{
    if (!m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.removeAll(effect);
    if (m_grabbedMouseEffects.isEmpty()) {
        doStopMouseInterception();
    }
}

bool effects_handler_wrap::isMouseInterception() const
{
    return m_grabbedMouseEffects.count() > 0;
}

bool effects_handler_wrap::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchDown(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool effects_handler_wrap::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchMotion(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool effects_handler_wrap::touchUp(qint32 id, quint32 time)
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
    m_compositor->space->input->platform.registerShortcut(shortcut, action);
}

void effects_handler_impl::registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                                   Qt::MouseButton pointerButtons,
                                                   QAction* action)
{
    m_compositor->space->input->platform.registerPointerShortcut(modifiers, pointerButtons, action);
}

void effects_handler_impl::registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                                                PointerAxisDirection axis,
                                                QAction* action)
{
    m_compositor->space->input->platform.registerAxisShortcut(modifiers, axis, action);
}

void effects_handler_impl::registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action)
{
    m_compositor->space->input->platform.registerTouchpadSwipeShortcut(direction, action);
}

void* effects_handler_wrap::getProxy(QString name)
{
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
        if ((*it).first == name)
            return (*it).second->proxy();

    return nullptr;
}

void effects_handler_impl::startMousePolling()
{
    if (auto& cursor = m_compositor->space->input->platform.cursor) {
        cursor->start_mouse_polling();
    }
}

void effects_handler_impl::stopMousePolling()
{
    if (auto& cursor = m_compositor->space->input->platform.cursor) {
        cursor->stop_mouse_polling();
    }
}

bool effects_handler_wrap::hasKeyboardGrab() const
{
    return keyboard_grab_effect != nullptr;
}

void effects_handler_impl::desktopResized(const QSize& size)
{
    m_scene->handle_screen_geometry_change(size);
    Q_EMIT screenGeometryChanged(size);
}

xcb_atom_t effects_handler_impl::announceSupportProperty(const QByteArray& propertyName,
                                                         Effect* effect)
{
    return x11::announce_support_property(*this, effect, propertyName);
}

void effects_handler_impl::removeSupportProperty(const QByteArray& propertyName, Effect* effect)
{
    x11::remove_support_property(*this, effect, propertyName);
}

QByteArray effects_handler_impl::readRootProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return render::x11::read_window_property(kwinApp()->x11RootWindow(), atom, type, format);
}

void effects_handler_impl::activateWindow(EffectWindow* c)
{
    auto window = static_cast<effects_window_impl*>(c)->window();
    if (window && window->control) {
        win::force_activate_window(*m_compositor->space, window);
    }
}

EffectWindow* effects_handler_impl::activeWindow() const
{
    auto ac = m_compositor->space->active_client;
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
        win::move(
            window,
            win::adjust_window_position(*m_compositor->space, *window, pos, true, snapAdjust));
    } else {
        win::move(window, pos);
    }
}

void effects_handler_impl::windowToDesktop(EffectWindow* w, int desktop)
{
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (window && window->control && !win::is_desktop(window) && !win::is_dock(window)) {
        win::send_window_to_desktop(*m_compositor->space, window, desktop, true);
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
        if (x11Id > m_compositor->space->virtual_desktop_manager->count()) {
            continue;
        }
        auto d = m_compositor->space->virtual_desktop_manager->desktopForX11Id(x11Id);
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
    auto output = base::get_output(kwinApp()->get_base().get_outputs(), screen);
    auto window = static_cast<effects_window_impl*>(w)->window();

    if (output && window && window->control && !win::is_desktop(window) && !win::is_dock(window)) {
        win::send_to_screen(*m_compositor->space, window, *output);
    }
}

void effects_handler_impl::setShowingDesktop(bool showing)
{
    win::set_showing_desktop(*m_compositor->space, showing);
}

QString effects_handler_wrap::currentActivity() const
{
    return QString();
}

int effects_handler_impl::currentDesktop() const
{
    return m_compositor->space->virtual_desktop_manager->current();
}

int effects_handler_impl::numberOfDesktops() const
{
    return m_compositor->space->virtual_desktop_manager->count();
}

void effects_handler_impl::setCurrentDesktop(int desktop)
{
    m_compositor->space->virtual_desktop_manager->setCurrent(desktop);
}

void effects_handler_impl::setNumberOfDesktops(int desktops)
{
    m_compositor->space->virtual_desktop_manager->setCount(desktops);
}

QSize effects_handler_impl::desktopGridSize() const
{
    return m_compositor->space->virtual_desktop_manager->grid().size();
}

int effects_handler_wrap::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int effects_handler_wrap::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int effects_handler_wrap::workspaceWidth() const
{
    return desktopGridWidth() * kwinApp()->get_base().topology.size.width();
}

int effects_handler_wrap::workspaceHeight() const
{
    return desktopGridHeight() * kwinApp()->get_base().topology.size.height();
}

int effects_handler_impl::desktopAtCoords(QPoint coords) const
{
    if (auto vd = m_compositor->space->virtual_desktop_manager->grid().at(coords)) {
        return vd->x11DesktopNumber();
    }
    return 0;
}

QPoint effects_handler_impl::desktopGridCoords(int id) const
{
    return m_compositor->space->virtual_desktop_manager->grid().gridCoords(id);
}

QPoint effects_handler_impl::desktopCoords(int id) const
{
    auto coords = m_compositor->space->virtual_desktop_manager->grid().gridCoords(id);
    if (coords.x() == -1) {
        return QPoint(-1, -1);
    }
    auto const& space_size = kwinApp()->get_base().topology.size;
    return QPoint(coords.x() * space_size.width(), coords.y() * space_size.height());
}

int effects_handler_impl::desktopAbove(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_above>(
        *m_compositor->space->virtual_desktop_manager, desktop, wrap);
}

int effects_handler_impl::desktopToRight(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_right>(
        *m_compositor->space->virtual_desktop_manager, desktop, wrap);
}

int effects_handler_impl::desktopBelow(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_below>(
        *m_compositor->space->virtual_desktop_manager, desktop, wrap);
}

int effects_handler_impl::desktopToLeft(int desktop, bool wrap) const
{
    return win::getDesktop<win::virtual_desktop_left>(
        *m_compositor->space->virtual_desktop_manager, desktop, wrap);
}

QString effects_handler_impl::desktopName(int desktop) const
{
    return m_compositor->space->virtual_desktop_manager->name(desktop);
}

bool effects_handler_wrap::optionRollOverDesktops() const
{
    return kwinApp()->options->qobject->isRollOverDesktops();
}

double effects_handler_wrap::animationTimeFactor() const
{
    return kwinApp()->options->animationTimeFactor();
}

WindowQuadType effects_handler_wrap::newWindowQuadType()
{
    return WindowQuadType(next_window_quad_type++);
}

EffectWindow* effects_handler_impl::find_window_by_wid(WId id) const
{
    if (auto w = win::x11::find_controlled_window<win::x11::window>(
            *m_compositor->space, win::x11::predicate_match::window, id)) {
        return w->render->effect.get();
    }
    if (auto unmanaged = win::x11::find_unmanaged<win::x11::window>(*m_compositor->space, id)) {
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
    if (Toplevel* toplevel = m_compositor->space->findInternal(w)) {
        return toplevel->render->effect.get();
    }
    return nullptr;
}

EffectWindow* effects_handler_impl::find_window_by_uuid(const QUuid& id) const
{
    for (auto win : m_compositor->space->windows) {
        if (!win->remnant && win->internal_id == id) {
            return win->render->effect.get();
        }
    }
    return nullptr;
}

EffectWindowList effects_handler_impl::stackingOrder() const
{
    auto list = win::render_stack(*m_compositor->space->stacking_order);
    EffectWindowList ret;
    for (auto t : list) {
        if (auto eff_win = t->render->effect.get()) {
            ret.append(eff_win);
        }
    }
    return ret;
}

void effects_handler_wrap::setElevatedWindow(KWin::EffectWindow* w, bool set)
{
    elevated_windows.removeAll(w);
    if (set)
        elevated_windows.append(w);
}

void effects_handler_impl::setTabBoxWindow(EffectWindow* w)
{
#if KWIN_BUILD_TABBOX
    auto window = static_cast<effects_window_impl*>(w)->window();
    if (window->control) {
        m_compositor->space->tabbox->set_current_client(window);
    }
#else
    Q_UNUSED(w)
#endif
}

void effects_handler_impl::setTabBoxDesktop(int desktop)
{
#if KWIN_BUILD_TABBOX
    m_compositor->space->tabbox->set_current_desktop(desktop);
#else
    Q_UNUSED(desktop)
#endif
}

EffectWindowList effects_handler_impl::currentTabBoxWindowList() const
{
#if KWIN_BUILD_TABBOX
    const auto clients = m_compositor->space->tabbox->current_client_list();
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
#if KWIN_BUILD_TABBOX
    m_compositor->space->tabbox->reference();
#endif
}

void effects_handler_impl::unrefTabBox()
{
#if KWIN_BUILD_TABBOX
    m_compositor->space->tabbox->unreference();
#endif
}

void effects_handler_impl::closeTabBox()
{
#if KWIN_BUILD_TABBOX
    m_compositor->space->tabbox->close();
#endif
}

QList<int> effects_handler_impl::currentTabBoxDesktopList() const
{
#if KWIN_BUILD_TABBOX
    return m_compositor->space->tabbox->current_desktop_list();
#else
    return QList<int>();
#endif
}

int effects_handler_impl::currentTabBoxDesktop() const
{
#if KWIN_BUILD_TABBOX
    return m_compositor->space->tabbox->current_desktop();
#else
    return -1;
#endif
}

EffectWindow* effects_handler_impl::currentTabBoxWindow() const
{
#if KWIN_BUILD_TABBOX
    if (auto c = m_compositor->space->tabbox->current_client())
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
    m_compositor->addRepaint(QRegion(x, y, w, h));
}

int effects_handler_impl::activeScreen() const
{
    auto output = win::get_current_output(*m_compositor->space);
    if (!output) {
        return 0;
    }
    return base::get_output_index(kwinApp()->get_base().get_outputs(), *output);
}

int effects_handler_impl::numScreens() const
{
    return kwinApp()->get_base().get_outputs().size();
}

int effects_handler_impl::screenNumber(const QPoint& pos) const
{
    auto const& outputs = kwinApp()->get_base().get_outputs();
    auto output = base::get_nearest_output(outputs, pos);
    if (!output) {
        return 0;
    }
    return base::get_output_index(outputs, *output);
}

QRect effects_handler_impl::clientArea(clientAreaOption opt, int screen, int desktop) const
{
    auto output = base::get_output(kwinApp()->get_base().get_outputs(), screen);
    return win::space_window_area(*m_compositor->space, opt, output, desktop);
}

QRect effects_handler_impl::clientArea(clientAreaOption opt, const EffectWindow* c) const
{
    auto window = static_cast<effects_window_impl const*>(c)->window();
    auto space = m_compositor->space;

    if (window->control) {
        return win::space_window_area(*space, opt, window);
    } else {
        return win::space_window_area(*space,
                                      opt,
                                      window->frameGeometry().center(),
                                      space->virtual_desktop_manager->current());
    }
}

QRect effects_handler_impl::clientArea(clientAreaOption opt, const QPoint& p, int desktop) const
{
    return win::space_window_area(*m_compositor->space, opt, p, desktop);
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
    m_compositor->space->input->get_pointer()->setEffectsOverrideCursor(shape);
}

bool effects_handler_wrap::checkInputWindowEvent(QMouseEvent* e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (auto const& effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

bool effects_handler_wrap::checkInputWindowEvent(QWheelEvent* e)
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
            QObject::connect(m_compositor->space->input->platform.cursor.get(),
                             &input::cursor::image_changed,
                             this,
                             &EffectsHandler::cursorShapeChanged);
            m_compositor->space->input->platform.cursor->start_image_tracking();
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
            m_compositor->space->input->platform.cursor->stop_image_tracking();
            QObject::disconnect(m_compositor->space->input->platform.cursor.get(),
                                &input::cursor::image_changed,
                                this,
                                &EffectsHandler::cursorShapeChanged);
        }
    }
    EffectsHandler::disconnectNotify(signal);
}

void effects_handler_wrap::checkInputWindowStacking()
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return;
    }
    doCheckInputWindowStacking();
}

void effects_handler_wrap::doCheckInputWindowStacking()
{
}

QPoint effects_handler_impl::cursorPos() const
{
    return m_compositor->space->input->platform.cursor->pos();
}

void insert_border(std::unordered_map<ElectricBorder, uint32_t>& map,
                   ElectricBorder border,
                   uint32_t id)
{
    auto it = map.find(border);
    if (it == map.end()) {
        map.insert({border, id});
        return;
    }

    it->second = id;
}

void effects_handler_impl::reserveElectricBorder(ElectricBorder border, Effect* effect)
{
    auto id = m_compositor->space->edges->reserve(
        border, [effect](auto eb) { return effect->borderActivated(eb); });

    auto it = reserved_borders.find(effect);
    if (it == reserved_borders.end()) {
        it = reserved_borders.insert({effect, {}}).first;
    }

    insert_border(it->second, border, id);
}

void effects_handler_impl::unreserveElectricBorder(ElectricBorder border, Effect* effect)
{
    auto it = reserved_borders.find(effect);
    if (it == reserved_borders.end()) {
        return;
    }

    auto it2 = it->second.find(border);
    if (it2 == it->second.end()) {
        return;
    }

    m_compositor->space->edges->unreserve(border, it2->second);
}

void effects_handler_impl::registerTouchBorder(ElectricBorder border, QAction* action)
{
    m_compositor->space->edges->reserveTouch(border, action);
}

void effects_handler_impl::unregisterTouchBorder(ElectricBorder border, QAction* action)
{
    m_compositor->space->edges->unreserveTouch(border, action);
}

unsigned long effects_handler_impl::xrenderBufferPicture() const
{
    return m_scene->xrenderBufferPicture();
}

QPainter* effects_handler_impl::scenePainter()
{
    return m_scene->scenePainter();
}

void effects_handler_wrap::toggleEffect(const QString& name)
{
    if (isEffectLoaded(name))
        unloadEffect(name);
    else
        loadEffect(name);
}

QStringList effects_handler_wrap::loadedEffects() const
{
    QStringList listModules;
    listModules.reserve(loaded_effects.count());
    std::transform(loaded_effects.constBegin(),
                   loaded_effects.constEnd(),
                   std::back_inserter(listModules),
                   [](const EffectPair& pair) { return pair.first; });
    return listModules;
}

QStringList effects_handler_wrap::listOfEffects() const
{
    return m_effectLoader->listOfKnownEffects();
}

bool effects_handler_wrap::loadEffect(const QString& name)
{
    makeOpenGLContextCurrent();
    addRepaintFull();

    return m_effectLoader->loadEffect(name);
}

void effects_handler_wrap::unloadEffect(const QString& name)
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

    addRepaintFull();
}

void effects_handler_impl::unreserve_borders(Effect& effect)
{
    auto it = reserved_borders.find(&effect);
    if (it == reserved_borders.end()) {
        return;
    }

    // Might be at shutdown with edges object already gone.
    if (m_compositor->space->edges) {
        for (auto& [key, id] : it->second) {
            m_compositor->space->edges->unreserve(key, id);
        }
    }

    reserved_borders.erase(it);
}

void effects_handler_wrap::destroyEffect(Effect* effect)
{
    assert(effect);
    makeOpenGLContextCurrent();

    if (fullscreen_effect == effect) {
        setActiveFullScreenEffect(nullptr);
    }

    if (keyboard_grab_effect == effect) {
        ungrabKeyboard();
    }

    stopMouseInterception(effect);
    handle_effect_destroy(*effect);

    const QList<QByteArray> properties = m_propertiesForEffects.keys();
    for (const QByteArray& property : properties) {
        removeSupportProperty(property, effect);
    }

    delete effect;
}

void effects_handler_wrap::reconfigureEffect(const QString& name)
{
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
        if ((*it).first == name) {
            kwinApp()->config()->reparseConfiguration();
            makeOpenGLContextCurrent();
            (*it).second->reconfigure(Effect::ReconfigureAll);
            return;
        }
}

bool effects_handler_wrap::isEffectLoaded(const QString& name) const
{
    auto it = std::find_if(loaded_effects.constBegin(),
                           loaded_effects.constEnd(),
                           [&name](const EffectPair& pair) { return pair.first == name; });
    return it != loaded_effects.constEnd();
}

bool effects_handler_wrap::isEffectSupported(const QString& name)
{
    // If the effect is loaded, it is obviously supported.
    if (isEffectLoaded(name)) {
        return true;
    }

    // next checks might require a context
    makeOpenGLContextCurrent();

    return m_effectLoader->isEffectSupported(name);
}

QList<bool> effects_handler_wrap::areEffectsSupported(const QStringList& names)
{
    QList<bool> retList;
    retList.reserve(names.count());
    std::transform(names.constBegin(),
                   names.constEnd(),
                   std::back_inserter(retList),
                   [this](const QString& name) { return isEffectSupported(name); });
    return retList;
}

void effects_handler_wrap::reloadEffect(Effect* effect)
{
    QString effectName;
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
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

void effects_handler_wrap::effectsChanged()
{
    loaded_effects.clear();
    m_activeEffects.clear(); // it's possible to have a reconfigure and a quad rebuild between two
                             // paint cycles - bug #308201

    loaded_effects.reserve(effect_order.count());
    std::copy(
        effect_order.constBegin(), effect_order.constEnd(), std::back_inserter(loaded_effects));

    m_activeEffects.reserve(loaded_effects.count());
}

QList<EffectWindow*> effects_handler_wrap::elevatedWindows() const
{
    if (isScreenLocked()) {
        return {};
    }
    return elevated_windows;
}

QStringList effects_handler_wrap::activeEffects() const
{
    QStringList ret;
    for (auto it = loaded_effects.constBegin(), end = loaded_effects.constEnd(); it != end; ++it) {
        if (it->second->isActive()) {
            ret << it->first;
        }
    }
    return ret;
}

Wrapland::Server::Display* effects_handler_wrap::waylandDisplay() const
{
    return nullptr;
}

EffectFrame* effects_handler_wrap::effectFrame(EffectFrameStyle style,
                                               bool staticSize,
                                               const QPoint& position,
                                               Qt::Alignment alignment) const
{
    return new effect_frame_impl(
        const_cast<effects_handler_wrap&>(*this), style, staticSize, position, alignment);
}

QVariant effects_handler_impl::kwinOption(KWinOption kwopt)
{
    switch (kwopt) {
    case CloseButtonCorner: {
        // TODO: this could become per window and be derived from the actual position in the deco
        auto deco_settings = m_compositor->space->deco->settings();
        auto close_enum = KDecoration2::DecorationButtonType::Close;
        return deco_settings && deco_settings->decorationButtonsLeft().contains(close_enum)
            ? Qt::TopLeftCorner
            : Qt::TopRightCorner;
    }
    case SwitchDesktopOnScreenEdge:
        return m_compositor->space->edges->desktop_switching.always;
    case SwitchDesktopOnScreenEdgeMovingWindows:
        return m_compositor->space->edges->desktop_switching.when_moving_client;
    default:
        return QVariant(); // an invalid one
    }
}

QString effects_handler_wrap::supportInformation(const QString& name) const
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

bool effects_handler_wrap::isScreenLocked() const
{
    return kwinApp()->screen_locker_watcher->is_locked();
}

QString effects_handler_wrap::debug(const QString& name, const QString& parameter) const
{
    QString internalName = name.toLower();
    ;
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
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

xcb_connection_t* effects_handler_wrap::xcbConnection() const
{
    return connection();
}

xcb_window_t effects_handler_wrap::x11RootWindow() const
{
    return rootWindow();
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

void effects_handler_wrap::highlightWindows(const QVector<EffectWindow*>& windows)
{
    Effect* e = provides(Effect::HighlightWindows);
    if (!e) {
        return;
    }
    e->perform(Effect::HighlightWindows, QVariantList{QVariant::fromValue(windows)});
}

PlatformCursorImage effects_handler_impl::cursorImage() const
{
    return m_compositor->space->input->platform.cursor->platform_image();
}

void effects_handler_impl::hideCursor()
{
    m_compositor->space->input->platform.cursor->hide();
}

void effects_handler_impl::showCursor()
{
    m_compositor->space->input->platform.cursor->show();
}

void effects_handler_impl::startInteractiveWindowSelection(
    std::function<void(KWin::EffectWindow*)> callback)
{
    m_compositor->space->input->platform.start_interactive_window_selection(
        [callback](KWin::Toplevel* t) {
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
    m_compositor->space->input->platform.start_interactive_position_selection(callback);
}

void effects_handler_impl::showOnScreenMessage(const QString& message, const QString& iconName)
{
    win::osd_show(*m_compositor->space, message, iconName);
}

void effects_handler_impl::hideOnScreenMessage(OnScreenMessageHideFlags flags)
{
    win::osd_hide_flags internal_flags{};
    if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
        internal_flags |= win::osd_hide_flags::skip_close_animation;
    }
    win::osd_hide(*m_compositor->space, internal_flags);
}

KSharedConfigPtr effects_handler_wrap::config() const
{
    return kwinApp()->config();
}

KSharedConfigPtr effects_handler_wrap::inputConfig() const
{
    return kwinApp()->inputConfig();
}

Effect* effects_handler_wrap::findEffect(const QString& name) const
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
    m_scene->paintEffectQuickView(w);
}

SessionState effects_handler_impl::sessionState() const
{
    return m_compositor->space->session_manager->state();
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
    return m_compositor->space->input->platform.cursor->is_hidden();
}

QImage effects_handler_wrap::blit_from_framebuffer(QRect const& geometry, double scale) const
{
    if (!isOpenGLCompositing()) {
        return {};
    }

    QImage image;
    auto const nativeSize = geometry.size() * scale;

    if (GLRenderTarget::blitSupported() && !GLPlatform::instance()->isGLES()) {
        image = QImage(nativeSize.width(), nativeSize.height(), QImage::Format_ARGB32);

        GLTexture texture(GL_RGBA8, nativeSize.width(), nativeSize.height());
        GLRenderTarget target(texture);
        target.blitFromFramebuffer(mapToRenderTarget(geometry));

        // Copy content from framebuffer into image.
        texture.bind();
        glGetTexImage(
            GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLvoid*>(image.bits()));
        texture.unbind();
    } else {
        image = QImage(nativeSize.width(), nativeSize.height(), QImage::Format_RGBA8888);
        glReadPixels(0,
                     0,
                     nativeSize.width(),
                     nativeSize.height(),
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     static_cast<GLvoid*>(image.bits()));
    }

    image.setDevicePixelRatio(scale);
    return image;
}

bool effects_handler_wrap::invert_screen()
{
    if (auto inverter = provides(Effect::ScreenInversion)) {
        qCDebug(KWIN_CORE) << "inverting screen using Effect plugin";
        QMetaObject::invokeMethod(inverter, "toggleScreenInversion", Qt::DirectConnection);
        return true;
    }
    return false;
}

QRect effects_handler_impl::renderTargetRect() const
{
    return m_scene->renderTargetRect();
}

qreal effects_handler_impl::renderTargetScale() const
{
    return m_scene->renderTargetScale();
}

}
