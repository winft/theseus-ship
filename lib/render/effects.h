/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/screen_impl.h"
#include "effect/window_impl.h"
#include "effect_loader.h"
#include "options.h"
#include "singleton_interface.h"
#include "types.h"
#include "x11/effect.h"
#include "x11/property_notify_filter.h"

#include "win/activation.h"
#include "win/osd.h"
#include "win/screen_edges.h"
#include "win/session_manager.h"
#include "win/space_qobject.h"
#include "win/stacking_order.h"
#include "win/types.h"
#include "win/virtual_desktops.h"
#include "win/x11/stacking.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

#include "config-kwin.h"

#include <kwineffects/effect.h>
#include <kwineffects/effect_frame.h>
#include <kwineffects/effect_quick_view.h>
#include <kwineffects/effect_screen.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>

#include <Plasma/FrameSvg>
#include <QHash>
#include <QMouseEvent>
#include <memory>
#include <set>

namespace Wrapland::Server
{
class Display;
}

namespace KWin::render
{

/// Implements all QObject-specific functioanlity of EffectsHandler.
class KWIN_EXPORT effects_handler_wrap : public EffectsHandler
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")
    Q_PROPERTY(QStringList activeEffects READ activeEffects)
    Q_PROPERTY(QStringList loadedEffects READ loadedEffects)
    Q_PROPERTY(QStringList listOfEffects READ listOfEffects)
public:
    template<typename Scene>
    effects_handler_wrap(Scene& scene)
        : EffectsHandler(scene.compositingType())
        , loader(std::make_unique<effect_loader>(*this, scene.platform))
        , options{*scene.platform.options}
    {
        qRegisterMetaType<QVector<KWin::EffectWindow*>>();

        singleton_interface::effects = this;
        connect(loader.get(),
                &basic_effect_loader::effectLoaded,
                this,
                [this](Effect* effect, const QString& name) {
                    effect_order.insert(effect->requestedEffectChainPosition(),
                                        EffectPair(name, effect));
                    loaded_effects << EffectPair(name, effect);
                    effectsChanged();
                });

        create_adaptor();
        QDBusConnection dbus = QDBusConnection::sessionBus();
        dbus.registerObject(QStringLiteral("/Effects"), this);

        // init is important, otherwise causes crashes when quads are build before the first
        // painting pass start
        m_currentBuildQuadsIterator = m_activeEffects.constEnd();
    }

    ~effects_handler_wrap() override;

    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion& region, ScreenPaintData& data) override;
    void postPaintScreen() override;
    void prePaintWindow(EffectWindow* w,
                        WindowPrePaintData& data,
                        std::chrono::milliseconds presentTime) override;
    void
    paintWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;
    void postPaintWindow(EffectWindow* w) override;

    Effect* provides(Effect::Feature ef);

    void
    drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;

    void buildQuads(EffectWindow* w, WindowQuadList& quadList) override;

    QString currentActivity() const override;
    int desktopGridWidth() const override;
    int desktopGridHeight() const override;

    bool grabKeyboard(Effect* effect) override;
    void ungrabKeyboard() override;
    // not performing XGrabPointer
    void startMouseInterception(Effect* effect, Qt::CursorShape shape) override;
    void stopMouseInterception(Effect* effect) override;
    bool isMouseInterception() const;

    void setElevatedWindow(KWin::EffectWindow* w, bool set) override;

    void setActiveFullScreenEffect(Effect* e) override;
    Effect* activeFullScreenEffect() const override;
    bool hasActiveFullScreenEffect() const override;

    double animationTimeFactor() const override;
    WindowQuadType newWindowQuadType() override;

    bool checkInputWindowEvent(QMouseEvent* e);
    bool checkInputWindowEvent(QWheelEvent* e);
    void checkInputWindowStacking();

    void reconfigure() override;

    bool hasDecorationShadows() const override;
    bool decorationsHaveAlpha() const override;

    std::unique_ptr<EffectFrame> effectFrame(EffectFrameStyle style,
                                             bool staticSize,
                                             const QPoint& position,
                                             Qt::Alignment alignment) const override;

    // internal (used by kwin core or compositing code)
    void startPaint();
    void grabbedKeyboardEvent(QKeyEvent* e);
    bool hasKeyboardGrab() const;

    void reloadEffect(Effect* effect) override;
    QStringList loadedEffects() const;
    QStringList listOfEffects() const;
    void unloadAllEffects();

    QList<EffectWindow*> elevatedWindows() const;
    QStringList activeEffects() const;

    Wrapland::Server::Display* waylandDisplay() const override;

    bool touchDown(qint32 id, const QPointF& pos, quint32 time);
    bool touchMotion(qint32 id, const QPointF& pos, quint32 time);
    bool touchUp(qint32 id, quint32 time);

    void highlightWindows(const QVector<EffectWindow*>& windows);

    /**
     * Finds an effect with the given name.
     *
     * @param name The name of the effect.
     * @returns The effect with the given name @p name, or nullptr if there
     *     is no such effect loaded.
     */
    Effect* findEffect(const QString& name) const;

    QImage blit_from_framebuffer(QRect const& geometry, double scale) const override;
    bool invert_screen();

    using PropertyEffectMap = QHash<QByteArray, QList<Effect*>>;
    PropertyEffectMap m_propertiesForEffects;
    QHash<QByteArray, qulonglong> m_managedProperties;
    QHash<long, int> registered_atoms;
    std::unique_ptr<effect_loader> loader;

public Q_SLOTS:
    // slots for D-Bus interface
    Q_SCRIPTABLE void reconfigureEffect(const QString& name)
    {
        reconfigure_effect_impl(name);
    }
    Q_SCRIPTABLE bool loadEffect(const QString& name);
    Q_SCRIPTABLE void toggleEffect(const QString& name);
    Q_SCRIPTABLE void unloadEffect(const QString& name);
    Q_SCRIPTABLE bool isEffectLoaded(const QString& name) const override;
    Q_SCRIPTABLE bool isEffectSupported(const QString& name);
    Q_SCRIPTABLE QList<bool> areEffectsSupported(const QStringList& names);
    Q_SCRIPTABLE QString supportInformation(const QString& name) const;
    Q_SCRIPTABLE QString debug(const QString& name, const QString& parameter = QString()) const;

protected:
    void effectsChanged();

    virtual void final_paint_screen(paint_type mask, QRegion const& region, ScreenPaintData& data)
        = 0;

    virtual void final_paint_window(EffectWindow* window,
                                    paint_type mask,
                                    QRegion const& region,
                                    WindowPaintData& data)
        = 0;
    virtual void final_draw_window(EffectWindow* window,
                                   paint_type mask,
                                   QRegion const& region,
                                   WindowPaintData& data)
        = 0;

    /**
     * Default implementation does nothing and returns @c true.
     */
    virtual bool doGrabKeyboard();
    /**
     * Default implementation does nothing.
     */
    virtual void doUngrabKeyboard();

    virtual void doStartMouseInterception(Qt::CursorShape shape) = 0;
    virtual void doStopMouseInterception() = 0;

    /**
     * Default implementation does nothing
     */
    virtual void doCheckInputWindowStacking();

    virtual void handle_effect_destroy(Effect& effect) = 0;
    virtual void reconfigure_effect_impl(QString const& name) = 0;

    Effect* keyboard_grab_effect{nullptr};
    Effect* fullscreen_effect{nullptr};
    QList<EffectWindow*> elevated_windows;
    QMultiMap<int, EffectPair> effect_order;
    int next_window_quad_type{EFFECT_QUAD_TYPE_START};

private:
    void create_adaptor();
    void destroyEffect(Effect* effect);

    typedef QVector<Effect*> EffectsList;
    typedef EffectsList::const_iterator EffectsIterator;

    EffectsList m_activeEffects;
    EffectsIterator m_currentDrawWindowIterator;
    EffectsIterator m_currentPaintWindowIterator;
    EffectsIterator m_currentPaintScreenIterator;
    EffectsIterator m_currentBuildQuadsIterator;
    QList<Effect*> m_grabbedMouseEffects;
    render::options& options;
};

template<typename Scene>
class effects_handler_impl : public effects_handler_wrap
{
public:
    using scene_t = Scene;
    using platform_t = typename scene_t::platform_t;
    using base_t = typename platform_t::base_t;
    using space_t = typename platform_t::space_t;
    using effect_window_t = typename scene_t::effect_window_t;

    effects_handler_impl(Scene& scene)
        : effects_handler_wrap(scene)
        , compositor{*scene.platform.compositor}
        , scene{scene}
    {
        QObject::connect(
            this, &effects_handler_impl::hasActiveFullScreenEffectChanged, this, [this] {
                Q_EMIT this->scene.platform.base.space->edges->qobject->checkBlocking();
            });

        auto ws = scene.platform.base.space.get();
        auto& vds = ws->virtual_desktop_manager;

        connect(ws->qobject.get(),
                &win::space_qobject::showingDesktopChanged,
                this,
                &effects_handler_wrap::showingDesktopChanged);
        connect(ws->qobject.get(),
                &win::space_qobject::currentDesktopChanged,
                this,
                [this, space = ws](int old) {
                    int const newDesktop = space->virtual_desktop_manager->current();
                    if (old == 0 || newDesktop == old) {
                        return;
                    }
                    EffectWindow* eff_win{nullptr};
                    if (auto& mov_res = space->move_resize_window) {
                        std::visit(overload{[&](auto&& win) {
                                       assert(win->render);
                                       assert(win->render->effect);
                                       eff_win = win->render->effect.get();
                                   }},
                                   *mov_res);
                    }
                    Q_EMIT desktopChanged(old, newDesktop, eff_win);
                });
        connect(ws->qobject.get(),
                &win::space_qobject::currentDesktopChanging,
                this,
                [this, space = ws](uint currentDesktop, QPointF offset) {
                    EffectWindow* eff_win{nullptr};
                    if (auto& mov_res = space->move_resize_window) {
                        std::visit(overload{[&](auto&& win) {
                                       assert(win->render);
                                       assert(win->render->effect);
                                       eff_win = win->render->effect.get();
                                   }},
                                   *mov_res);
                    }
                    Q_EMIT desktopChanging(currentDesktop, offset, eff_win);
                });
        connect(ws->qobject.get(),
                &win::space_qobject::currentDesktopChangingCancelled,
                this,
                [this]() { Q_EMIT desktopChangingCancelled(); });
        connect(ws->qobject.get(),
                &win::space_qobject::clientAdded,
                this,
                [this, space = ws](auto win_id) {
                    std::visit(overload{[this](auto&& win) {
                                   if (win->render_data.ready_for_painting) {
                                       slotClientShown(*win);
                                   } else {
                                       QObject::connect(win->qobject.get(),
                                                        &win::window_qobject::windowShown,
                                                        this,
                                                        [this, win] { slotClientShown(*win); });
                                   }
                               }},
                               space->windows_map.at(win_id));
                });
        connect(ws->qobject.get(),
                &win::space_qobject::unmanagedAdded,
                this,
                [this, space = ws](auto win_id) {
                    // it's never initially ready but has synthetic 50ms delay
                    std::visit(overload{[this](auto&& win) {
                                   connect(win->qobject.get(),
                                           &win::window_qobject::windowShown,
                                           this,
                                           [this, win] { slotUnmanagedShown(*win); });
                               }},
                               space->windows_map.at(win_id));
                });
        connect(ws->qobject.get(),
                &win::space_qobject::internalClientAdded,
                this,
                [this, space = ws](auto win_id) {
                    std::visit(overload{[this](auto&& win) {
                                   assert(win->render);
                                   assert(win->render->effect);
                                   setupAbstractClientConnections(*win);
                                   Q_EMIT windowAdded(win->render->effect.get());
                               }},
                               space->windows_map.at(win_id));
                });
        connect(ws->qobject.get(), &win::space_qobject::clientActivated, this, [this, space = ws] {
            EffectWindow* eff_win{nullptr};
            if (auto win = space->stacking.active) {
                std::visit(overload{[&](auto&& win) {
                               assert(win->render);
                               assert(win->render->effect);
                               eff_win = win->render->effect.get();
                           }},
                           *win);
            }
            Q_EMIT windowActivated(eff_win);
        });

        connect(ws->qobject.get(),
                &win::space_qobject::window_deleted,
                this,
                [this, space = ws](auto win_id) {
                    std::visit(overload{[this](auto&& win) {
                                   assert(win->render);
                                   assert(win->render->effect);
                                   Q_EMIT windowDeleted(win->render->effect.get());
                                   elevated_windows.removeAll(win->render->effect.get());
                               }},
                               space->windows_map.at(win_id));
                });
        connect(ws->session_manager.get(),
                &win::session_manager::stateChanged,
                this,
                &KWin::EffectsHandler::sessionStateChanged);
        connect(vds->qobject.get(),
                &win::virtual_desktop_manager_qobject::countChanged,
                this,
                &EffectsHandler::numberDesktopsChanged);
        connect(vds->qobject.get(),
                &win::virtual_desktop_manager_qobject::layoutChanged,
                this,
                [this](int width, int height) {
                    Q_EMIT desktopGridSizeChanged(QSize(width, height));
                    Q_EMIT desktopGridWidthChanged(width);
                    Q_EMIT desktopGridHeightChanged(height);
                });
        QObject::connect(ws->input->cursor.get(),
                         &std::remove_pointer_t<decltype(ws->input->cursor.get())>::mouse_changed,
                         this,
                         &EffectsHandler::mouseChanged);

        auto& base = scene.platform.base;
        QObject::connect(
            &base, &base_t::topology_changed, this, [this](auto old_topo, auto new_topo) {
                if (old_topo.size != new_topo.size) {
                    Q_EMIT virtualScreenSizeChanged();
                    Q_EMIT virtualScreenGeometryChanged();
                }
            });

        connect(ws->stacking.order.qobject.get(),
                &win::stacking_order_qobject::changed,
                this,
                &EffectsHandler::stackingOrderChanged);

#if KWIN_BUILD_TABBOX
        auto qt_tabbox = ws->tabbox->qobject.get();
        connect(qt_tabbox, &win::tabbox_qobject::tabbox_added, this, [this](auto mode) {
            Q_EMIT tabBoxAdded(static_cast<int>(mode));
        });
        connect(
            qt_tabbox, &win::tabbox_qobject::tabbox_updated, this, &EffectsHandler::tabBoxUpdated);
        connect(
            qt_tabbox, &win::tabbox_qobject::tabbox_closed, this, &EffectsHandler::tabBoxClosed);
        connect(qt_tabbox,
                &win::tabbox_qobject::tabbox_key_event,
                this,
                &EffectsHandler::tabBoxKeyEvent);
#endif

        connect(ws->edges->qobject.get(),
                &win::screen_edger_qobject::approaching,
                this,
                [this](auto border, auto factor, auto const& geometry) {
                    screenEdgeApproaching(static_cast<ElectricBorder>(border), factor, geometry);
                });

        auto screen_locker_watcher = ws->base.space->screen_locker_watcher.get();
        using screen_locker_watcher_t = std::remove_pointer_t<decltype(screen_locker_watcher)>;
        connect(screen_locker_watcher,
                &screen_locker_watcher_t::locked,
                this,
                &EffectsHandler::screenLockingChanged);
        connect(screen_locker_watcher,
                &screen_locker_watcher_t::about_to_lock,
                this,
                &EffectsHandler::screenAboutToLock);

        auto make_property_filter = [this] {
            using filter = x11::property_notify_filter<effects_handler_wrap, space_t>;
            auto& base = this->scene.platform.base;
            x11_property_notify
                = std::make_unique<filter>(*this, *base.space, base.x11_data.root_window);
        };

        connect(
            &scene.platform.base, &base::platform::x11_reset, this, [this, make_property_filter] {
                registered_atoms.clear();
                for (auto it = m_propertiesForEffects.keyBegin();
                     it != m_propertiesForEffects.keyEnd();
                     it++) {
                    x11::add_support_property(*this, *it);
                }
                if (this->scene.platform.base.x11_data.connection) {
                    make_property_filter();
                } else {
                    x11_property_notify.reset();
                }
                Q_EMIT xcbConnectionChanged();
            });

        if (scene.platform.base.x11_data.connection) {
            make_property_filter();
        }

        // connect all clients
        for (auto& win : ws->windows) {
            // TODO: Can we merge this with the one for Wayland XdgShellClients below?
            std::visit(overload{[&](typename space_t::x11_window* win) {
                                    if (win->control) {
                                        setupClientConnections(*win);
                                    }
                                },
                                [](auto&&) {}},
                       win);
        }
        for (auto win : win::x11::get_unmanageds(*ws)) {
            std::visit(overload{[&](auto&& win) { setupUnmanagedConnections(*win); }}, win);
        }

        if constexpr (requires { typename space_t::internal_window_t; }) {
            for (auto& win : ws->windows) {
                std::visit(overload{[this](typename space_t::internal_window_t* win) {
                                        setupAbstractClientConnections(*win);
                                    },
                                    [](auto&&) {}},
                           win);
            }
        }

        connect(&scene.platform.base,
                &base_t::output_added,
                this,
                &effects_handler_impl::slotOutputEnabled);
        connect(&scene.platform.base,
                &base_t::output_removed,
                this,
                &effects_handler_impl::slotOutputDisabled);

        auto const outputs = scene.platform.base.outputs;
        for (base::output* output : outputs) {
            slotOutputEnabled(output);
        }

        connect(scene.platform.base.input->shortcuts.get(),
                &decltype(scene.platform.base.input
                              ->shortcuts)::element_type::keyboard_shortcut_changed,
                this,
                &effects_handler_impl::globalShortcutChanged);
    }

    ~effects_handler_impl() override
    {
    }

    bool isScreenLocked() const override
    {
        return scene.platform.base.space->screen_locker_watcher->is_locked();
    }

    xcb_connection_t* xcbConnection() const override
    {
        return scene.platform.base.x11_data.connection;
    }

    xcb_window_t x11RootWindow() const override
    {
        return scene.platform.base.x11_data.root_window;
    }

    QPainter* scenePainter() override
    {
        return scene.scenePainter();
    }

    bool animationsSupported() const override
    {
        static const QByteArray forceEnvVar = qgetenv("KWIN_EFFECTS_FORCE_ANIMATIONS");
        if (!forceEnvVar.isEmpty()) {
            static const int forceValue = forceEnvVar.toInt();
            return forceValue == 1;
        }
        return scene.animationsSupported();
    }

    bool makeOpenGLContextCurrent() override
    {
        return scene.makeOpenGLContextCurrent();
    }

    void doneOpenGLContextCurrent() override
    {
        scene.doneOpenGLContextCurrent();
    }

    void addRepaintFull() override
    {
        full_repaint(*scene.platform.compositor);
    }

    void addRepaint(const QRect& r) override
    {
        scene.platform.compositor->addRepaint(r);
    }

    void addRepaint(const QRegion& r) override
    {
        scene.platform.compositor->addRepaint(r);
    }

    void addRepaint(int x, int y, int w, int h) override
    {
        scene.platform.compositor->addRepaint(QRegion(x, y, w, h));
    }

    void final_paint_screen(paint_type mask, QRegion const& region, ScreenPaintData& data) override
    {
        scene.finalPaintScreen(mask, region, data);
        Q_EMIT frameRendered();
    }

    void final_paint_window(EffectWindow* window,
                            paint_type mask,
                            QRegion const& region,
                            WindowPaintData& data) override
    {
        scene.finalPaintWindow(static_cast<effect_window_t*>(window), mask, region, data);
    }

    void final_draw_window(EffectWindow* window,
                           paint_type mask,
                           QRegion const& region,
                           WindowPaintData& data) override
    {
        scene.finalDrawWindow(static_cast<effect_window_t*>(window), mask, region, data);
    }

    void activateWindow(EffectWindow* c) override
    {
        auto window = static_cast<effect_window_t*>(c)->window.ref_win;
        assert(window.has_value());

        std::visit(overload{[this](auto&& win) {
                       if (win->control) {
                           win::force_activate_window(*scene.platform.base.space, *win);
                       }
                   }},
                   *window);
    }

    EffectWindow* activeWindow() const override
    {
        if (auto win = scene.platform.base.space->stacking.active) {
            return std::visit(overload{[](auto&& win) { return win->render->effect.get(); }}, *win);
        }
        return nullptr;
    }

    void desktopResized(const QSize& size)
    {
        scene.handle_screen_geometry_change(size);
        Q_EMIT screenGeometryChanged(size);
    }

    QList<QKeySequence> registerGlobalShortcut(QList<QKeySequence> const& shortcut,
                                               QAction* action) override
    {
        scene.platform.base.input->shortcuts->register_keyboard_shortcut(action, shortcut);
        scene.platform.base.input->registerShortcut(
            shortcut.empty() ? QKeySequence() : shortcut.front(), action);
        return scene.platform.base.input->shortcuts->get_keyboard_shortcut(action);
    }

    QList<QKeySequence> registerGlobalShortcutAndDefault(QList<QKeySequence> const& shortcut,
                                                         QAction* action) override
    {
        scene.platform.base.input->shortcuts->register_keyboard_default_shortcut(action, shortcut);
        return registerGlobalShortcut(shortcut, action);
    }

    void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButton pointerButtons,
                                 QAction* action) override
    {
        scene.platform.base.input->shortcuts->registerPointerShortcut(
            action, modifiers, pointerButtons);
    }

    void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis,
                              QAction* action) override
    {
        scene.platform.base.input->shortcuts->registerAxisShortcut(
            action, modifiers, static_cast<win::pointer_axis_direction>(axis));
    }

    void registerTouchpadSwipeShortcut(SwipeDirection direction,
                                       uint fingerCount,
                                       QAction* action,
                                       std::function<void(qreal)> progressCallback) override
    {
        scene.platform.base.input->shortcuts->registerTouchpadSwipe(
            static_cast<win::swipe_direction>(direction), fingerCount, action, progressCallback);
    }

    void registerTouchpadPinchShortcut(PinchDirection direction,
                                       uint fingerCount,
                                       QAction* action,
                                       std::function<void(qreal)> progressCallback) override
    {
        scene.platform.base.input->shortcuts->registerTouchpadPinch(
            static_cast<win::pinch_direction>(direction), fingerCount, action, progressCallback);
    }

    void registerTouchscreenSwipeShortcut(SwipeDirection direction,
                                          uint fingerCount,
                                          QAction* action,
                                          std::function<void(qreal)> progressCallback) override
    {
        scene.platform.base.input->shortcuts->registerTouchscreenSwipe(
            action, progressCallback, static_cast<win::swipe_direction>(direction), fingerCount);
    }

    void startMousePolling() override
    {
        // Don't need to start/stop polling manually anymore nowadays. On X11 we use XInput to
        // receive data throughout, on Wayland we are doing it anyway as the Wayland server.
    }

    void stopMousePolling() override
    {
    }

    QPoint cursorPos() const override
    {
        return scene.platform.base.space->input->cursor->pos();
    }

    void defineCursor(Qt::CursorShape shape) override
    {
        scene.platform.base.space->input->pointer->setEffectsOverrideCursor(shape);
    }

    void connectNotify(const QMetaMethod& signal) override
    {
        if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
            if (!m_trackingCursorChanges) {
                auto cursor = scene.platform.base.space->input->cursor.get();
                using cursor_t = std::remove_pointer_t<decltype(cursor)>;
                QObject::connect(
                    cursor, &cursor_t::image_changed, this, &EffectsHandler::cursorShapeChanged);
                cursor->start_image_tracking();
            }
            ++m_trackingCursorChanges;
        }
        EffectsHandler::connectNotify(signal);
    }

    void disconnectNotify(const QMetaMethod& signal) override
    {
        if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
            Q_ASSERT(m_trackingCursorChanges > 0);
            if (!--m_trackingCursorChanges) {
                auto cursor = scene.platform.base.space->input->cursor.get();
                using cursor_t = std::remove_pointer_t<decltype(cursor)>;
                cursor->stop_image_tracking();
                QObject::disconnect(
                    cursor, &cursor_t::image_changed, this, &EffectsHandler::cursorShapeChanged);
            }
        }
        EffectsHandler::disconnectNotify(signal);
    }

    effect::cursor_image cursorImage() const override
    {
        auto img = scene.platform.base.space->input->cursor->platform_image();
        return {img.first, img.second};
    }

    bool isCursorHidden() const override
    {
        return scene.platform.base.space->input->cursor->is_hidden();
    }

    void hideCursor() override
    {
        scene.platform.base.space->input->cursor->hide();
    }

    void showCursor() override
    {
        scene.platform.base.space->input->cursor->show();
    }

    void startInteractiveWindowSelection(std::function<void(KWin::EffectWindow*)> callback) override
    {
        scene.platform.base.space->input->start_interactive_window_selection([callback](auto win) {
            if (!win) {
                callback(nullptr);
                return;
            }

            std::visit(overload{[&](auto&& win) {
                           assert(win->render);
                           assert(win->render->effect);
                           callback(win->render->effect.get());
                       }},
                       *win);
        });
    }

    void startInteractivePositionSelection(std::function<void(const QPoint&)> callback) override
    {
        scene.platform.base.space->input->start_interactive_position_selection(callback);
    }

    void showOnScreenMessage(const QString& message, const QString& iconName = QString()) override
    {
        win::osd_show(*scene.platform.base.space, message, iconName);
    }

    void hideOnScreenMessage(OnScreenMessageHideFlags flags = OnScreenMessageHideFlags()) override
    {
        win::osd_hide_flags internal_flags{};
        if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
            internal_flags |= win::osd_hide_flags::skip_close_animation;
        }
        win::osd_hide(*scene.platform.base.space, internal_flags);
    }

    QRect renderTargetRect() const override
    {
        return scene.m_renderTargetRect;
    }

    qreal renderTargetScale() const override
    {
        return scene.m_renderTargetScale;
    }

    QQmlEngine* qmlEngine() const override
    {
        auto& script = scene.platform.base.script;
        return script ? script->qml_engine : nullptr;
    }

    void renderEffectQuickView(EffectQuickView* effectQuickView) const override
    {
        if (!effectQuickView->isVisible()) {
            return;
        }
        scene.paintEffectQuickView(effectQuickView);
    }

    void moveWindow(EffectWindow* w,
                    const QPoint& pos,
                    bool snap = false,
                    double snapAdjust = 1.0) override
    {
        std::visit(overload{[&](auto&& win) {
                       if (!win->isMovable()) {
                           return;
                       }
                       if (!snap) {
                           win::move(win, pos);
                           return;
                       }
                       win::move(win,
                                 win::adjust_window_position(
                                     *scene.platform.base.space, *win, pos, true, snapAdjust));
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
    }

    void windowToDesktop(EffectWindow* w, int desktop) override
    {
        std::visit(overload{[&, this](auto&& win) {
                       if (win->control && !win::is_desktop(win) && !win::is_dock(win)) {
                           win::send_window_to_desktop(
                               *scene.platform.base.space, win, desktop, true);
                       }
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
    }

    void windowToDesktops(EffectWindow* w, const QVector<uint>& desktopIds) override
    {
        std::visit(
            overload{[&](auto&& win) {
                if (!win->control || win::is_desktop(win) || win::is_dock(win)) {
                    return;
                }
                QVector<win::virtual_desktop*> desktops;
                desktops.reserve(desktopIds.count());
                for (uint x11Id : desktopIds) {
                    if (x11Id > scene.platform.base.space->virtual_desktop_manager->count()) {
                        continue;
                    }
                    auto d = scene.platform.base.space->virtual_desktop_manager->desktopForX11Id(
                        x11Id);
                    Q_ASSERT(d);
                    if (desktops.contains(d)) {
                        continue;
                    }
                    desktops << d;
                }
                win::set_desktops(win, desktops);
            }},
            *static_cast<effect_window_t*>(w)->window.ref_win);
    }

    void windowToScreen(EffectWindow* w, EffectScreen* screen) override
    {
        auto screenImpl = static_cast<effect_screen_impl<base::output> const*>(screen);
        auto output = static_cast<typename base_t::output_t*>(screenImpl->platformOutput());
        if (!output) {
            return;
        }

        std::visit(overload{[&, this](auto&& win) {
                       if (win->control && !win::is_desktop(win) && !win::is_dock(win)) {
                           win::send_to_screen(*scene.platform.base.space, win, *output);
                       }
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
    }

    void setShowingDesktop(bool showing) override
    {
        win::set_showing_desktop(*scene.platform.base.space, showing);
    }

    int currentDesktop() const override
    {
        return scene.platform.base.space->virtual_desktop_manager->current();
    }

    int numberOfDesktops() const override
    {
        return scene.platform.base.space->virtual_desktop_manager->count();
    }

    void setCurrentDesktop(int desktop) override
    {
        scene.platform.base.space->virtual_desktop_manager->setCurrent(desktop);
    }

    void setNumberOfDesktops(int desktops) override
    {
        scene.platform.base.space->virtual_desktop_manager->setCount(desktops);
    }

    QSize desktopGridSize() const override
    {
        return scene.platform.base.space->virtual_desktop_manager->grid().size();
    }

    int workspaceWidth() const override
    {
        return desktopGridWidth() * scene.platform.base.topology.size.width();
    }

    int workspaceHeight() const override
    {
        return desktopGridHeight() * scene.platform.base.topology.size.height();
    }

    int desktopAtCoords(QPoint coords) const override
    {
        if (auto vd = scene.platform.base.space->virtual_desktop_manager->grid().at(coords)) {
            return vd->x11DesktopNumber();
        }
        return 0;
    }

    QPoint desktopGridCoords(int id) const override
    {
        return scene.platform.base.space->virtual_desktop_manager->grid().gridCoords(id);
    }

    QPoint desktopCoords(int id) const override
    {
        auto coords = scene.platform.base.space->virtual_desktop_manager->grid().gridCoords(id);
        if (coords.x() == -1) {
            return QPoint(-1, -1);
        }
        auto const& space_size = scene.platform.base.topology.size;
        return QPoint(coords.x() * space_size.width(), coords.y() * space_size.height());
    }

    int desktopAbove(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_above>(
            *scene.platform.base.space->virtual_desktop_manager, desktop, wrap);
    }

    int desktopToRight(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_right>(
            *scene.platform.base.space->virtual_desktop_manager, desktop, wrap);
    }

    int desktopBelow(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_below>(
            *scene.platform.base.space->virtual_desktop_manager, desktop, wrap);
    }

    int desktopToLeft(int desktop = 0, bool wrap = true) const override
    {
        return win::getDesktop<win::virtual_desktop_left>(
            *scene.platform.base.space->virtual_desktop_manager, desktop, wrap);
    }

    QString desktopName(int desktop) const override
    {
        return scene.platform.base.space->virtual_desktop_manager->name(desktop);
    }

    EffectWindow* find_window_by_wid(WId id) const override
    {
        if (auto w = win::x11::find_controlled_window<typename space_t::x11_window>(
                *scene.platform.base.space, win::x11::predicate_match::window, id)) {
            return w->render->effect.get();
        }
        if (auto unmanaged = win::x11::find_unmanaged<typename space_t::x11_window>(
                *scene.platform.base.space, id)) {
            return unmanaged->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* /*surface*/) const override
    {
        return nullptr;
    }

    EffectWindow* find_window_by_qwindow(QWindow* w) const override
    {
        if (auto toplevel = scene.platform.base.space->findInternal(w)) {
            return toplevel->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_uuid(const QUuid& id) const override
    {
        for (auto win : scene.platform.base.space->windows) {
            if (auto eff_win = std::visit(overload{[&](auto&& win) -> EffectWindow* {
                                              if (!win->remnant && win->meta.internal_id == id) {
                                                  return win->render->effect.get();
                                              }
                                              return nullptr;
                                          }},
                                          win)) {
                return eff_win;
            }
        }
        return nullptr;
    }

    EffectWindowList stackingOrder() const override
    {
        EffectWindowList ret;
        for (auto win : win::render_stack(scene.platform.base.space->stacking.order)) {
            std::visit(overload{[&](auto&& win) {
                           if (auto eff_win = win->render->effect.get()) {
                               ret.append(eff_win);
                           }
                       }},
                       win);
        }
        return ret;
    }

    void setTabBoxWindow([[maybe_unused]] EffectWindow* w) override
    {
#if KWIN_BUILD_TABBOX
        std::visit(overload{[&, this](auto&& win) {
                       if (win->control) {
                           scene.platform.base.space->tabbox->set_current_client(win);
                       }
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
#endif
    }

    void setTabBoxDesktop([[maybe_unused]] int desktop) override
    {
#if KWIN_BUILD_TABBOX
        scene.platform.base.space->tabbox->set_current_desktop(desktop);
#endif
    }

    EffectWindowList currentTabBoxWindowList() const override
    {
#if KWIN_BUILD_TABBOX
        const auto clients = scene.platform.base.space->tabbox->current_client_list();
        EffectWindowList ret;
        ret.reserve(clients.size());
        std::transform(
            std::cbegin(clients), std::cend(clients), std::back_inserter(ret), [](auto win) {
                return std::visit(overload{[](auto&& win) { return win->render->effect.get(); }},
                                  win);
            });
        return ret;
#else
        return EffectWindowList();
#endif
    }

    void refTabBox() override
    {
#if KWIN_BUILD_TABBOX
        scene.platform.base.space->tabbox->reference();
#endif
    }

    void unrefTabBox() override
    {
#if KWIN_BUILD_TABBOX
        scene.platform.base.space->tabbox->unreference();
#endif
    }

    void closeTabBox() override
    {
#if KWIN_BUILD_TABBOX
        scene.platform.base.space->tabbox->close();
#endif
    }

    QList<int> currentTabBoxDesktopList() const override
    {
#if KWIN_BUILD_TABBOX
        return scene.platform.base.space->tabbox->current_desktop_list();
#else
        return QList<int>();
#endif
    }

    int currentTabBoxDesktop() const override
    {
#if KWIN_BUILD_TABBOX
        return scene.platform.base.space->tabbox->current_desktop();
#else
        return -1;
#endif
    }

    EffectWindow* currentTabBoxWindow() const override
    {
#if KWIN_BUILD_TABBOX
        if (auto win = scene.platform.base.space->tabbox->current_client()) {
            return std::visit(overload{[](auto&& win) { return win->render->effect.get(); }}, *win);
        }
#endif
        return nullptr;
    }

    EffectScreen* activeScreen() const override
    {
        auto output = win::get_current_output(*scene.platform.base.space);
        if (!output) {
            return nullptr;
        }
        return get_effect_screen(*this, *output);
    }

    QList<EffectScreen*> screens() const override
    {
        return m_effectScreens;
    }

    EffectScreen* screenAt(const QPoint& point) const override
    {
        auto const& outputs = scene.platform.base.outputs;
        auto output = base::get_nearest_output(outputs, point);
        if (!output) {
            return nullptr;
        }
        return get_effect_screen(*this, *output);
    }

    EffectScreen* findScreen(const QString& name) const override
    {
        for (EffectScreen* screen : qAsConst(m_effectScreens)) {
            if (screen->name() == name) {
                return screen;
            }
        }
        return nullptr;
    }

    EffectScreen* findScreen(int screenId) const override
    {
        return m_effectScreens.value(screenId);
    }

    QRect clientArea(clientAreaOption opt, EffectScreen const* screen, int desktop) const override
    {
        typename base_t::output_t const* output = nullptr;
        if (screen) {
            auto screenImpl = static_cast<effect_screen_impl<base::output> const*>(screen);
            output = static_cast<typename base_t::output_t*>(screenImpl->platformOutput());
        }
        return win::space_window_area(*scene.platform.base.space, opt, output, desktop);
    }

    QRect clientArea(clientAreaOption opt, const EffectWindow* eff_win) const override
    {
        return std::visit(overload{[&, this](auto&& win) {
                              if (win->control) {
                                  return win::space_window_area(
                                      *scene.platform.base.space, opt, win);
                              }

                              return win::space_window_area(
                                  *scene.platform.base.space,
                                  opt,
                                  win->geo.frame.center(),
                                  scene.platform.base.space->virtual_desktop_manager->current());
                          }},
                          *static_cast<effect_window_t const*>(eff_win)->window.ref_win);
    }

    QRect clientArea(clientAreaOption opt, const QPoint& p, int desktop) const override
    {
        return win::space_window_area(*scene.platform.base.space, opt, p, desktop);
    }

    QSize virtualScreenSize() const override
    {
        return scene.platform.base.topology.size;
    }

    QRect virtualScreenGeometry() const override
    {
        return QRect({}, scene.platform.base.topology.size);
    }

    void reserveElectricBorder(ElectricBorder border, Effect* effect) override
    {
        auto id = scene.platform.base.space->edges->reserve(
            static_cast<win::electric_border>(border),
            [effect](auto eb) { return effect->borderActivated(static_cast<ElectricBorder>(eb)); });

        auto it = reserved_borders.find(effect);
        if (it == reserved_borders.end()) {
            it = reserved_borders.insert({effect, {}}).first;
        }

        auto insert_border = [](auto& map, ElectricBorder border, uint32_t id) {
            auto it = map.find(border);
            if (it == map.end()) {
                map.insert({border, id});
                return;
            }

            it->second = id;
        };

        insert_border(it->second, border, id);
    }

    void unreserveElectricBorder(ElectricBorder border, Effect* effect) override
    {
        auto it = reserved_borders.find(effect);
        if (it == reserved_borders.end()) {
            return;
        }

        auto it2 = it->second.find(border);
        if (it2 == it->second.end()) {
            return;
        }

        scene.platform.base.space->edges->unreserve(static_cast<win::electric_border>(border),
                                                    it2->second);
    }

    void registerTouchBorder(ElectricBorder border, QAction* action) override
    {
        scene.platform.base.space->edges->reserveTouch(static_cast<win::electric_border>(border),
                                                       action);
    }

    void registerRealtimeTouchBorder(ElectricBorder border,
                                     QAction* action,
                                     EffectsHandler::TouchBorderCallback progressCallback) override
    {
        scene.platform.base.space->edges->reserveTouch(
            static_cast<win::electric_border>(border),
            action,
            [progressCallback,
             this](auto border, const QSizeF& deltaProgress, base::output* output) {
                progressCallback(static_cast<ElectricBorder>(border),
                                 deltaProgress,
                                 get_effect_screen(*this, *output));
            });
    }

    void unregisterTouchBorder(ElectricBorder border, QAction* action) override
    {
        scene.platform.base.space->edges->unreserveTouch(static_cast<win::electric_border>(border),
                                                         action);
    }

    void unreserve_borders(Effect& effect)
    {
        auto it = reserved_borders.find(&effect);
        if (it == reserved_borders.end()) {
            return;
        }

        // Might be at shutdown with space already gone.
        if (scene.platform.base.space && scene.platform.base.space->edges) {
            for (auto& [key, id] : it->second) {
                scene.platform.base.space->edges->unreserve(static_cast<win::electric_border>(key),
                                                            id);
            }
        }

        reserved_borders.erase(it);
    }

    QVariant kwinOption(KWinOption kwopt) override
    {
        switch (kwopt) {
        case CloseButtonCorner: {
            // TODO: this could become per window and be derived from the actual position in the
            // deco
            auto deco_settings = scene.platform.base.space->deco->settings();
            auto close_enum = KDecoration2::DecorationButtonType::Close;
            return deco_settings && deco_settings->decorationButtonsLeft().contains(close_enum)
                ? Qt::TopLeftCorner
                : Qt::TopRightCorner;
        }
        case SwitchDesktopOnScreenEdge:
            return scene.platform.base.space->edges->desktop_switching.always;
        case SwitchDesktopOnScreenEdgeMovingWindows:
            return scene.platform.base.space->edges->desktop_switching.when_moving_client;
        default:
            return QVariant(); // an invalid one
        }
    }

    bool optionRollOverDesktops() const override
    {
        return scene.platform.base.space->options->qobject->isRollOverDesktops();
    }

    SessionState sessionState() const override
    {
        return static_cast<SessionState>(scene.platform.base.space->session_manager->state());
    }

    QByteArray readRootProperty(long atom, long type, int format) const override
    {
        auto const& data = scene.platform.base.x11_data;
        if (!data.connection) {
            return QByteArray();
        }
        return render::x11::read_window_property(
            data.connection, data.root_window, atom, type, format);
    }

    xcb_atom_t announceSupportProperty(const QByteArray& propertyName, Effect* effect) override
    {
        return x11::announce_support_property(*this, effect, propertyName);
    }

    void removeSupportProperty(const QByteArray& propertyName, Effect* effect) override
    {
        x11::remove_support_property(*this, effect, propertyName);
    }

    KSharedConfigPtr config() const override
    {
        return scene.platform.base.config.main;
    }

    KSharedConfigPtr inputConfig() const override
    {
        return scene.platform.base.input->config.main;
    }

    void reconfigure_effect_impl(QString const& name) override
    {
        for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it)
            if ((*it).first == name) {
                scene.platform.base.config.main->reparseConfiguration();
                makeOpenGLContextCurrent();
                (*it).second->reconfigure(Effect::ReconfigureAll);
                return;
            }
    }

    typename platform_t::compositor_t& compositor;
    Scene& scene;

protected:
    template<typename Win>
    void setupAbstractClientConnections(Win& window)
    {
        auto qtwin = window.qobject.get();

        QObject::connect(qtwin, &win::window_qobject::desktopsChanged, this, [this, &window] {
            Q_EMIT windowDesktopsChanged(window.render->effect.get());
        });
        QObject::connect(qtwin,
                         &win::window_qobject::maximize_mode_changed,
                         this,
                         [this, &window](auto mode) { slotClientMaximized(window, mode); });
        QObject::connect(
            qtwin, &win::window_qobject::clientStartUserMovedResized, this, [this, &window] {
                Q_EMIT windowStartUserMovedResized(window.render->effect.get());
            });
        QObject::connect(qtwin,
                         &win::window_qobject::clientStepUserMovedResized,
                         this,
                         [this, &window](QRect const& geometry) {
                             Q_EMIT windowStepUserMovedResized(window.render->effect.get(),
                                                               geometry);
                         });
        QObject::connect(
            qtwin, &win::window_qobject::clientFinishUserMovedResized, this, [this, &window] {
                Q_EMIT windowFinishUserMovedResized(window.render->effect.get());
            });
        QObject::connect(qtwin,
                         &win::window_qobject::opacityChanged,
                         this,
                         [this, &window](auto old) { slotOpacityChanged(window, old); });
        QObject::connect(
            qtwin, &win::window_qobject::clientMinimized, this, [this, &window](auto animate) {
                // TODO: notify effects even if it should not animate?
                if (animate) {
                    Q_EMIT windowMinimized(window.render->effect.get());
                }
            });
        QObject::connect(
            qtwin, &win::window_qobject::clientUnminimized, this, [this, &window](auto animate) {
                // TODO: notify effects even if it should not animate?
                if (animate) {
                    Q_EMIT windowUnminimized(window.render->effect.get());
                }
            });
        QObject::connect(qtwin, &win::window_qobject::modalChanged, this, [this, &window] {
            slotClientModalityChanged(window);
        });
        QObject::connect(
            qtwin,
            &win::window_qobject::frame_geometry_changed,
            this,
            [this, &window](auto const& rect) { slotGeometryShapeChanged(window, rect); });
        QObject::connect(
            qtwin,
            &win::window_qobject::frame_geometry_changed,
            this,
            [this, &window](auto const& rect) { slotFrameGeometryChanged(window, rect); });
        QObject::connect(qtwin,
                         &win::window_qobject::damaged,
                         this,
                         [this, &window](auto const& rect) { slotWindowDamaged(window, rect); });
        QObject::connect(qtwin,
                         &win::window_qobject::unresponsiveChanged,
                         this,
                         [this, &window](bool unresponsive) {
                             Q_EMIT windowUnresponsiveChanged(window.render->effect.get(),
                                                              unresponsive);
                         });
        QObject::connect(qtwin, &win::window_qobject::windowShown, this, [this, &window] {
            Q_EMIT windowShown(window.render->effect.get());
        });
        QObject::connect(qtwin, &win::window_qobject::windowHidden, this, [this, &window] {
            Q_EMIT windowHidden(window.render->effect.get());
        });
        QObject::connect(
            qtwin, &win::window_qobject::keepAboveChanged, this, [this, &window](bool above) {
                Q_UNUSED(above)
                Q_EMIT windowKeepAboveChanged(window.render->effect.get());
            });
        QObject::connect(
            qtwin, &win::window_qobject::keepBelowChanged, this, [this, &window](bool below) {
                Q_UNUSED(below)
                Q_EMIT windowKeepBelowChanged(window.render->effect.get());
            });
        QObject::connect(qtwin, &win::window_qobject::fullScreenChanged, this, [this, &window]() {
            Q_EMIT windowFullScreenChanged(window.render->effect.get());
        });
        QObject::connect(
            qtwin, &win::window_qobject::visible_geometry_changed, this, [this, &window]() {
                Q_EMIT windowExpandedGeometryChanged(window.render->effect.get());
            });
    }

    // For X11 windows
    template<typename Win>
    void setupClientConnections(Win& window)
    {
        setupAbstractClientConnections(window);
        connect(window.qobject.get(),
                &win::window_qobject::paddingChanged,
                this,
                [this, &window](auto const& old) { slotPaddingChanged(window, old); });
    }

    template<typename Win>
    void setupUnmanagedConnections(Win& window)
    {
        connect(window.qobject.get(),
                &win::window_qobject::opacityChanged,
                this,
                [this, &window](auto old) { slotOpacityChanged(window, old); });
        connect(window.qobject.get(),
                &win::window_qobject::frame_geometry_changed,
                this,
                [this, &window](auto const& old) { slotGeometryShapeChanged(window, old); });
        connect(window.qobject.get(),
                &win::window_qobject::frame_geometry_changed,
                this,
                [this, &window](auto const& old) { slotFrameGeometryChanged(window, old); });
        connect(window.qobject.get(),
                &win::window_qobject::paddingChanged,
                this,
                [this, &window](auto const& old) { slotPaddingChanged(window, old); });
        connect(window.qobject.get(),
                &win::window_qobject::damaged,
                this,
                [this, &window](auto const& region) { slotWindowDamaged(window, region); });
        connect(window.qobject.get(),
                &win::window_qobject::visible_geometry_changed,
                this,
                [this, &window]() {
                    Q_EMIT windowExpandedGeometryChanged(window.render->effect.get());
                });
    }

    template<typename Win>
    void slotClientShown(Win& window)
    {
        disconnect(window.qobject.get(), &win::window_qobject::windowShown, this, nullptr);
        setupClientConnections(window);
        Q_EMIT windowAdded(window.render->effect.get());
    }

    template<typename Win>
    void slotXdgShellClientShown(Win& window)
    {
        setupAbstractClientConnections(window);
        Q_EMIT windowAdded(window.render->effect.get());
    }

    template<typename Win>
    void slotUnmanagedShown(Win& window)
    { // regardless, unmanaged windows are -yet?- not synced anyway
        assert(!window.control);
        setupUnmanagedConnections(window);
        Q_EMIT windowAdded(window.render->effect.get());
    }

    template<typename Win>
    void slotClientMaximized(Win& window, win::maximize_mode maxMode)
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

        auto ew = window.render->effect.get();
        assert(ew);
        Q_EMIT windowMaximizedStateChanged(ew, horizontal, vertical);
    }

    template<typename Win>
    void slotOpacityChanged(Win& window, qreal oldOpacity)
    {
        assert(window.render->effect);

        if (window.opacity() == oldOpacity) {
            return;
        }

        Q_EMIT windowOpacityChanged(
            window.render->effect.get(), oldOpacity, static_cast<qreal>(window.opacity()));
    }

    template<typename Win>
    void slotClientModalityChanged(Win& window)
    {
        Q_EMIT windowModalityChanged(window.render->effect.get());
    }

    template<typename Win>
    void slotGeometryShapeChanged(Win& window, const QRect& old)
    {
        assert(window.render);
        assert(window.render->effect);

        if (window.control && (win::is_move(&window) || win::is_resize(&window))) {
            // For that we have windowStepUserMovedResized.
            return;
        }

        Q_EMIT windowGeometryShapeChanged(window.render->effect.get(), old);
    }

    template<typename Win>
    void slotFrameGeometryChanged(Win& window, const QRect& oldGeometry)
    {
        assert(window.render);
        assert(window.render->effect);
        Q_EMIT windowFrameGeometryChanged(window.render->effect.get(), oldGeometry);
    }

    template<typename Win>
    void slotPaddingChanged(Win& window, const QRect& old)
    {
        assert(window.render);
        assert(window.render->effect);
        Q_EMIT windowPaddingChanged(window.render->effect.get(), old);
    }

    template<typename Win>
    void slotWindowDamaged(Win& window, const QRegion& r)
    {
        assert(window.render);
        assert(window.render->effect);
        Q_EMIT windowDamaged(window.render->effect.get(), r);
    }

    void slotOutputEnabled(base::output* output)
    {
        auto screen = new effect_screen_impl<base::output>(output, this);
        m_effectScreens.append(screen);
        Q_EMIT screenAdded(screen);
    }

    void slotOutputDisabled(base::output* output)
    {
        auto screen = get_effect_screen(*this, *output);
        m_effectScreens.removeOne(screen);
        Q_EMIT screenRemoved(screen);
        delete screen;
    }

    QList<EffectScreen*> m_effectScreens;
    int m_trackingCursorChanges{0};
    std::unique_ptr<x11::property_notify_filter<effects_handler_wrap, space_t>> x11_property_notify;
    std::unordered_map<Effect*, std::unordered_map<ElectricBorder, uint32_t>> reserved_borders;
};
}
