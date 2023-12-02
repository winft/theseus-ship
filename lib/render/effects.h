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
#include <render/compositor.h>
#include <render/effect/setup_handler.h>

#include "win/activation.h"
#include "win/osd.h"
#include "win/space_qobject.h"
#include "win/stacking_order.h"
#include "win/types.h"
#include <win/subspace.h>

#include <render/effect/interface/effect.h>
#include <render/effect/interface/effect_frame.h>
#include <render/effect/interface/effect_screen.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/offscreen_quick_view.h>

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
        : loader(std::make_unique<effect_loader>(scene.platform))
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

    void prePaintScreen(effect::screen_prepaint_data& data) override;
    void paintScreen(effect::screen_paint_data& data) override;
    void postPaintScreen() override;
    void prePaintWindow(effect::window_prepaint_data& data) override;
    void paintWindow(effect::window_paint_data& data) override;
    void postPaintWindow(EffectWindow* w) override;

    Effect* provides(Effect::Feature ef);

    // TODO(romangg): Remove once we replaced the call from win/control.h.
    bool provides_comp(int feat)
    {
        return provides(static_cast<Effect::Feature>(feat));
    }

    void drawWindow(effect::window_paint_data& data) override;

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
    bool is_effect_active(QString const& plugin_id) const;

    QImage blit_from_framebuffer(effect::render_data& data,
                                 QRect const& geometry,
                                 double scale) const override;
    bool invert_screen();

    using PropertyEffectMap = QHash<QByteArray, QList<Effect*>>;
    PropertyEffectMap m_propertiesForEffects;
    QHash<QByteArray, qulonglong> m_managedProperties;
    QHash<long, int> registered_atoms;
    std::unique_ptr<effect_loader> loader;

Q_SIGNALS:
    void propertyNotify(KWin::EffectWindow* win, long atom);
    void xcbConnectionChanged();

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

public:
    void effectsChanged();

    virtual void final_paint_screen(paint_type mask, effect::screen_paint_data& data) = 0;

    virtual void final_paint_window(effect::window_paint_data& data) = 0;
    virtual void final_draw_window(effect::window_paint_data& data) = 0;

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
    using type = effects_handler_impl<Scene>;
    using scene_t = Scene;
    using base_t = typename Scene::platform_t::base_t;
    using space_t = typename Scene::platform_t::space_t;
    using effect_window_t = typename scene_t::effect_window_t;

    effects_handler_impl(Scene& scene)
        : effects_handler_wrap(scene)
        , scene{scene}
    {
    }

    ~effects_handler_impl() override
    {
    }

    bool isOpenGLCompositing() const override
    {
        return scene.isOpenGl();
    }

    bool isScreenLocked() const override
    {
        auto const& mod = get_space().mod;
        if constexpr (requires(decltype(mod) mod) { mod.desktop; }) {
            return mod.desktop->screen_locker_watcher->is_locked();
        }
        return false;
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
        full_repaint(scene.platform);
    }

    void addRepaint(const QRect& r) override
    {
        scene.platform.addRepaint(r);
    }

    void addRepaint(const QRegion& r) override
    {
        scene.platform.addRepaint(r);
    }

    void addRepaint(int x, int y, int w, int h) override
    {
        scene.platform.addRepaint(QRegion(x, y, w, h));
    }

    void final_paint_screen(paint_type mask, effect::screen_paint_data& data) override
    {
        scene.finalPaintScreen(mask, data);
        Q_EMIT frameRendered(data);
    }

    void final_paint_window(effect::window_paint_data& data) override
    {
        scene.finalPaintWindow(data);
    }

    void final_draw_window(effect::window_paint_data& data) override
    {
        scene.finalDrawWindow(data);
    }

    void activateWindow(EffectWindow* c) override
    {
        auto window = static_cast<effect_window_t*>(c)->window.ref_win;
        assert(window.has_value());

        std::visit(overload{[this](auto&& win) {
                       if (win->control) {
                           win::force_activate_window(get_space(), *win);
                       }
                   }},
                   *window);
    }

    EffectWindow* activeWindow() const override
    {
        if (auto win = get_space().stacking.active) {
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
        scene.platform.base.mod.input->shortcuts->register_keyboard_shortcut(action, shortcut);
        scene.platform.base.mod.input->registerShortcut(
            shortcut.empty() ? QKeySequence() : shortcut.front(), action);
        return scene.platform.base.mod.input->shortcuts->get_keyboard_shortcut(action);
    }

    QList<QKeySequence> registerGlobalShortcutAndDefault(QList<QKeySequence> const& shortcut,
                                                         QAction* action) override
    {
        scene.platform.base.mod.input->shortcuts->register_keyboard_default_shortcut(action,
                                                                                     shortcut);
        return registerGlobalShortcut(shortcut, action);
    }

    void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButton pointerButtons,
                                 QAction* action) override
    {
        scene.platform.base.mod.input->shortcuts->registerPointerShortcut(
            action, modifiers, pointerButtons);
    }

    void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis,
                              QAction* action) override
    {
        scene.platform.base.mod.input->shortcuts->registerAxisShortcut(
            action, modifiers, static_cast<win::pointer_axis_direction>(axis));
    }

    void registerTouchpadSwipeShortcut(SwipeDirection direction,
                                       uint fingerCount,
                                       QAction* action,
                                       std::function<void(qreal)> progressCallback) override
    {
        scene.platform.base.mod.input->shortcuts->registerTouchpadSwipe(
            static_cast<win::swipe_direction>(direction), fingerCount, action, progressCallback);
    }

    void registerTouchpadPinchShortcut(PinchDirection direction,
                                       uint fingerCount,
                                       QAction* action,
                                       std::function<void(qreal)> progressCallback) override
    {
        scene.platform.base.mod.input->shortcuts->registerTouchpadPinch(
            static_cast<win::pinch_direction>(direction), fingerCount, action, progressCallback);
    }

    void registerTouchscreenSwipeShortcut(SwipeDirection direction,
                                          uint fingerCount,
                                          QAction* action,
                                          std::function<void(qreal)> progressCallback) override
    {
        scene.platform.base.mod.input->shortcuts->registerTouchscreenSwipe(
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
        return get_space().input->cursor->pos();
    }

    void defineCursor(Qt::CursorShape shape) override
    {
        get_space().input->pointer->setEffectsOverrideCursor(shape);
    }

    void connectNotify(const QMetaMethod& signal) override
    {
        if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
            if (!m_trackingCursorChanges) {
                auto cursor = get_space().input->cursor.get();
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
                auto cursor = get_space().input->cursor.get();
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
        auto img = get_space().input->cursor->platform_image();
        return {img.first, img.second};
    }

    bool isCursorHidden() const override
    {
        return get_space().input->cursor->is_hidden();
    }

    void hideCursor() override
    {
        get_space().input->cursor->hide();
    }

    void showCursor() override
    {
        get_space().input->cursor->show();
    }

    void startInteractiveWindowSelection(std::function<void(KWin::EffectWindow*)> callback) override
    {
        get_space().input->start_interactive_window_selection([callback](auto win) {
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
        get_space().input->start_interactive_position_selection(callback);
    }

    void showOnScreenMessage(const QString& message, const QString& iconName = QString()) override
    {
        win::osd_show(get_space(), message, iconName);
    }

    void hideOnScreenMessage(OnScreenMessageHideFlags flags = OnScreenMessageHideFlags()) override
    {
        win::osd_hide_flags internal_flags{};
        if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
            internal_flags |= win::osd_hide_flags::skip_close_animation;
        }
        win::osd_hide(get_space(), internal_flags);
    }

    QQmlEngine* qmlEngine() const override
    {
        return scene.platform.base.mod.space->qml_engine.get();
    }

    void renderOffscreenQuickView(OffscreenQuickView* view) const override
    {
        if (!view->isVisible()) {
            return;
        }
        scene.paintOffscreenQuickView(view);
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
                       win::move(
                           win,
                           win::adjust_window_position(get_space(), *win, pos, true, snapAdjust));
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
    }

    void windowToDesktops(EffectWindow* w, QVector<win::subspace*> const& subspaces) override
    {
        std::visit(overload{[&](auto&& win) {
                       if (!win->control || win::is_desktop(win) || win::is_dock(win)) {
                           return;
                       }

                       std::vector<win::subspace*> subs;
                       subs.reserve(subspaces.size());

                       for (auto subsp : subspaces) {
                           auto sub = &get_subspace_ref(subsp);
                           if (contains(subs, sub)) {
                               continue;
                           }

                           subs.push_back(sub);
                       }

                       win::set_subspaces(*win, subs);
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
                           win::send_to_screen(get_space(), win, *output);
                       }
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
    }

    void setShowingDesktop(bool showing) override
    {
        win::set_showing_desktop(get_space(), showing);
    }

    win::subspace* currentDesktop() const override
    {
        return get_space().subspace_manager->current;
    }

    QList<win::subspace*> desktops() const override
    {
        auto const& subs = get_space().subspace_manager->subspaces;
        QList<win::subspace*> ret;
        ret.reserve(subs.size());
        std::copy(subs.cbegin(), subs.cend(), std::back_inserter(ret));
        return ret;
    }

    void setCurrentDesktop(win::subspace* desktop) override
    {
        if (desktop) {
            win::subspaces_set_current(*get_space().subspace_manager, *desktop);
        }
    }

    QSize desktopGridSize() const override
    {
        return get_space().subspace_manager->grid.size();
    }

    int workspaceWidth() const override
    {
        return desktopGridWidth() * scene.platform.base.topology.size.width();
    }

    int workspaceHeight() const override
    {
        return desktopGridHeight() * scene.platform.base.topology.size.height();
    }

    win::subspace* desktopAtCoords(QPoint coords) const override
    {
        return get_space().subspace_manager->grid.at(coords);
    }

    QPoint desktopGridCoords(win::subspace* subsp) const override
    {
        return get_space().subspace_manager->grid.gridCoords(subsp);
    }

    QPoint desktopCoords(win::subspace* subsp) const override
    {
        auto& mgr = get_space().subspace_manager;
        auto coords = mgr->grid.gridCoords(subsp);

        if (coords.x() == -1) {
            return QPoint(-1, -1);
        }

        auto const& space_size = scene.platform.base.topology.size;
        return QPoint(coords.x() * space_size.width(), coords.y() * space_size.height());
    }

    win::subspace* desktopAbove(win::subspace* subsp, bool wrap = true) const override
    {
        return &win::subspaces_get_north_of(
            *get_space().subspace_manager, get_subspace_ref(subsp), wrap);
    }

    win::subspace* desktopToRight(win::subspace* subsp, bool wrap = true) const override
    {
        return &win::subspaces_get_east_of(
            *get_space().subspace_manager, get_subspace_ref(subsp), wrap);
    }

    win::subspace* desktopBelow(win::subspace* subsp, bool wrap = true) const override
    {
        return &win::subspaces_get_south_of(
            *get_space().subspace_manager, get_subspace_ref(subsp), wrap);
    }

    win::subspace* desktopToLeft(win::subspace* subsp, bool wrap = true) const override
    {
        return &win::subspaces_get_west_of(
            *get_space().subspace_manager, get_subspace_ref(subsp), wrap);
    }

    QString desktopName(win::subspace* subsp) const override
    {
        auto x11id = subsp ? subsp->x11DesktopNumber() : currentDesktop()->x11DesktopNumber();
        return win::subspace_manager_get_subspace_name(*get_space().subspace_manager, x11id);
    }

    EffectWindow* find_window_by_surface(Wrapland::Server::Surface* /*surface*/) const override
    {
        return nullptr;
    }

    EffectWindow* find_window_by_qwindow(QWindow* w) const override
    {
        if (auto toplevel = get_space().findInternal(w)) {
            return toplevel->render->effect.get();
        }
        return nullptr;
    }

    EffectWindow* find_window_by_uuid(const QUuid& id) const override
    {
        for (auto win : get_space().windows) {
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
        for (auto win : win::render_stack(get_space().stacking.order)) {
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
                           get_space().tabbox->set_current_client(win);
                       }
                   }},
                   *static_cast<effect_window_t*>(w)->window.ref_win);
#endif
    }

    EffectWindowList currentTabBoxWindowList() const override
    {
#if KWIN_BUILD_TABBOX
        const auto clients = get_space().tabbox->current_client_list();
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
        get_space().tabbox->reference();
#endif
    }

    void unrefTabBox() override
    {
#if KWIN_BUILD_TABBOX
        get_space().tabbox->unreference();
#endif
    }

    void closeTabBox() override
    {
#if KWIN_BUILD_TABBOX
        get_space().tabbox->close();
#endif
    }

    EffectWindow* currentTabBoxWindow() const override
    {
#if KWIN_BUILD_TABBOX
        if (auto win = get_space().tabbox->current_client()) {
            return std::visit(overload{[](auto&& win) { return win->render->effect.get(); }}, *win);
        }
#endif
        return nullptr;
    }

    EffectScreen* activeScreen() const override
    {
        auto output = win::get_current_output(get_space());
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

    QRect clientArea(clientAreaOption opt,
                     EffectScreen const* screen,
                     win::subspace* subsp) const override
    {
        typename base_t::output_t const* output = nullptr;
        if (screen) {
            auto screenImpl = static_cast<effect_screen_impl<base::output> const*>(screen);
            output = static_cast<typename base_t::output_t*>(screenImpl->platformOutput());
        }
        return win::space_window_area(get_space(),
                                      static_cast<win::area_option>(opt),
                                      output,
                                      get_subspace_ref(subsp).x11DesktopNumber());
    }

    QRect clientArea(clientAreaOption opt, const EffectWindow* eff_win) const override
    {
        return std::visit(overload{[&, this](auto&& win) {
                              if (win->control) {
                                  return win::space_window_area(
                                      get_space(), static_cast<win::area_option>(opt), win);
                              }

                              return win::space_window_area(
                                  get_space(),
                                  static_cast<win::area_option>(opt),
                                  win->geo.frame.center(),
                                  win::subspaces_get_current_x11id(*get_space().subspace_manager));
                          }},
                          *static_cast<effect_window_t const*>(eff_win)->window.ref_win);
    }

    QRect clientArea(clientAreaOption opt, const QPoint& p, win::subspace* subsp) const override
    {
        return win::space_window_area(get_space(),
                                      static_cast<win::area_option>(opt),
                                      p,
                                      get_subspace_ref(subsp).x11DesktopNumber());
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
        auto id = get_space().edges->reserve(
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

        get_space().edges->unreserve(static_cast<win::electric_border>(border), it2->second);
    }

    void registerTouchBorder(ElectricBorder border, QAction* action) override
    {
        get_space().edges->reserveTouch(static_cast<win::electric_border>(border), action);
    }

    void registerRealtimeTouchBorder(ElectricBorder border,
                                     QAction* action,
                                     EffectsHandler::TouchBorderCallback progressCallback) override
    {
        get_space().edges->reserveTouch(static_cast<win::electric_border>(border),
                                        action,
                                        [progressCallback, this](auto border,
                                                                 const QSizeF& deltaProgress,
                                                                 base::output* output) {
                                            progressCallback(static_cast<ElectricBorder>(border),
                                                             deltaProgress,
                                                             get_effect_screen(*this, *output));
                                        });
    }

    void unregisterTouchBorder(ElectricBorder border, QAction* action) override
    {
        get_space().edges->unreserveTouch(static_cast<win::electric_border>(border), action);
    }

    void unreserve_borders(Effect& effect)
    {
        auto it = reserved_borders.find(&effect);
        if (it == reserved_borders.end()) {
            return;
        }

        // Might be at shutdown with space already gone.
        if (scene.platform.base.mod.space && get_space().edges) {
            for (auto& [key, id] : it->second) {
                get_space().edges->unreserve(static_cast<win::electric_border>(key), id);
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
            auto deco_settings = get_space().deco->settings();
            auto close_enum = KDecoration2::DecorationButtonType::Close;
            return deco_settings && deco_settings->decorationButtonsLeft().contains(close_enum)
                ? Qt::TopLeftCorner
                : Qt::TopRightCorner;
        }
        case SwitchDesktopOnScreenEdge:
            return get_space().edges->desktop_switching.always;
        case SwitchDesktopOnScreenEdgeMovingWindows:
            return get_space().edges->desktop_switching.when_moving_client;
        default:
            return QVariant(); // an invalid one
        }
    }

    bool optionRollOverDesktops() const override
    {
        return get_space().options->qobject->isRollOverDesktops();
    }

    KSharedConfigPtr config() const override
    {
        return scene.platform.base.config.main;
    }

    KSharedConfigPtr inputConfig() const override
    {
        return scene.platform.base.mod.input->config.main;
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

    Scene& scene;

public:
    template<typename Win>
    void slotClientShown(Win& window)
    {
        disconnect(window.qobject.get(), &win::window_qobject::windowShown, this, nullptr);
        effect::setup_window_connections(window);
        Q_EMIT windowAdded(window.render->effect.get());
    }

    template<typename Win>
    void slotXdgShellClientShown(Win& window)
    {
        effect::setup_window_connections(window);
        Q_EMIT windowAdded(window.render->effect.get());
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

    auto& get_space() const
    {
        return *scene.platform.base.mod.space;
    }

    QList<EffectScreen*> m_effectScreens;
    int m_trackingCursorChanges{0};
    std::unordered_map<Effect*, std::unordered_map<ElectricBorder, uint32_t>> reserved_borders;

private:
    win::subspace& get_subspace_ref(win::subspace* subsp) const
    {
        if (subsp) {
            return *subsp;
        }
        return *get_space().subspace_manager->current;
    }
};
}
