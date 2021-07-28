/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

// TODO(romangg): should only be included when KWIN_BUILD_TABBOX is defined.
#include "filters/tabbox.h"

#include "filters/decoration_event.h"
#include "filters/drag_and_drop.h"
#include "filters/effects.h"
#include "filters/fake_tablet.h"
#include "filters/forward.h"
#include "filters/global_shortcut.h"
#include "filters/internal_window.h"
#include "filters/lock_screen.h"
#include "filters/move_resize.h"
#include "filters/popup.h"
#include "filters/screen_edge.h"
#include "filters/terminate_server.h"
#include "filters/virtual_terminal.h"
#include "filters/window_action.h"
#include "filters/window_selector.h"

#include "keyboard.h"
#include "platform.h"
#include "pointer.h"
#include "switch.h"
#include "touch.h"

#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "../platform.h"
#include "abstract_wayland_output.h"
#include "effects.h"
#include "globalshortcuts.h"
#include "main.h"
#include "screens.h"
#include "seat/session.h"
#include "toplevel.h"
#include "touch_hide_cursor_spy.h"
#include "wayland_server.h"
#include "win/geo.h"
#include "win/stacking_order.h"
#include "workspace.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <KConfigWatcher>
#include <KGlobalAccel>

namespace KWin::input
{

redirect::redirect()
    : QObject(nullptr)
    , m_keyboard(new keyboard_redirect(this))
    , m_pointer(new pointer_redirect(this))
    , m_tablet(new tablet_redirect(this))
    , m_touch(new touch_redirect(this))
    , m_shortcuts(new GlobalShortcutsManager(this))
    , m_inputConfigWatcher{KConfigWatcher::create(kwinApp()->inputConfig())}
{
    qRegisterMetaType<KWin::input::redirect::KeyboardKeyState>();
    qRegisterMetaType<KWin::input::redirect::PointerButtonState>();
    qRegisterMetaType<KWin::input::redirect::PointerAxis>();
    connect(kwinApp(), &Application::workspaceCreated, this, &redirect::setupWorkspace);
    reconfigure();
}

redirect::~redirect()
{
    qDeleteAll(m_filters);
    qDeleteAll(m_spies);
}

void redirect::installInputEventFilter(event_filter* filter)
{
    Q_ASSERT(!m_filters.contains(filter));
    m_filters << filter;
}

void redirect::prependInputEventFilter(event_filter* filter)
{
    Q_ASSERT(!m_filters.contains(filter));
    m_filters.prepend(filter);
}

void redirect::uninstallInputEventFilter(event_filter* filter)
{
    m_filters.removeOne(filter);
}

void redirect::installInputEventSpy(InputEventSpy* spy)
{
    m_spies << spy;
}

void redirect::uninstallInputEventSpy(InputEventSpy* spy)
{
    m_spies.removeOne(spy);
}

void redirect::init()
{
    m_shortcuts->init();
}

void redirect::setupWorkspace()
{
    if (waylandServer()) {
        using namespace Wrapland::Server;
        auto fakeInput = waylandServer()->display()->createFakeInput(this);

        connect(fakeInput, &FakeInput::deviceCreated, this, [this](FakeInputDevice* device) {
            connect(device,
                    &FakeInputDevice::authenticationRequested,
                    this,
                    [this, device](const QString& application, const QString& reason) {
                        Q_UNUSED(application)
                        Q_UNUSED(reason)
                        // TODO: make secure
                        device->setAuthentication(true);
                    });
            connect(device,
                    &FakeInputDevice::pointerMotionRequested,
                    this,
                    [this](const QSizeF& delta) {
                        // TODO: Fix time
                        m_pointer->processMotion(
                            globalPointer() + QPointF(delta.width(), delta.height()), 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::pointerMotionAbsoluteRequested,
                    this,
                    [this](const QPointF& pos) {
                        // TODO: Fix time
                        m_pointer->processMotion(pos, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::pointerButtonPressRequested,
                    this,
                    [this](quint32 button) {
                        // TODO: Fix time
                        m_pointer->processButton(button, redirect::PointerButtonPressed, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::pointerButtonReleaseRequested,
                    this,
                    [this](quint32 button) {
                        // TODO: Fix time
                        m_pointer->processButton(button, redirect::PointerButtonReleased, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::pointerAxisRequested,
                    this,
                    [this](Qt::Orientation orientation, qreal delta) {
                        // TODO: Fix time
                        redirect::PointerAxis axis;
                        switch (orientation) {
                        case Qt::Horizontal:
                            axis = redirect::PointerAxisHorizontal;
                            break;
                        case Qt::Vertical:
                            axis = redirect::PointerAxisVertical;
                            break;
                        default:
                            Q_UNREACHABLE();
                            break;
                        }
                        // TODO: Fix time
                        m_pointer->processAxis(
                            axis, delta, 0, redirect::PointerAxisSourceUnknown, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::touchDownRequested,
                    this,
                    [this](qint32 id, const QPointF& pos) {
                        // TODO: Fix time
                        m_touch->processDown(id, pos, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::touchMotionRequested,
                    this,
                    [this](qint32 id, const QPointF& pos) {
                        // TODO: Fix time
                        m_touch->processMotion(id, pos, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device, &FakeInputDevice::touchUpRequested, this, [this](qint32 id) {
                // TODO: Fix time
                m_touch->processUp(id, 0);
                waylandServer()->simulateUserActivity();
            });
            connect(device, &FakeInputDevice::touchCancelRequested, this, [this]() {
                m_touch->cancel();
            });
            connect(device, &FakeInputDevice::touchFrameRequested, this, [this]() {
                m_touch->frame();
            });
            connect(
                device, &FakeInputDevice::keyboardKeyPressRequested, this, [this](quint32 button) {
                    // TODO: Fix time
                    m_keyboard->processKey(button, redirect::KeyboardKeyPressed, 0);
                    waylandServer()->simulateUserActivity();
                });
            connect(device,
                    &FakeInputDevice::keyboardKeyReleaseRequested,
                    this,
                    [this](quint32 button) {
                        // TODO: Fix time
                        m_keyboard->processKey(button, redirect::KeyboardKeyReleased, 0);
                        waylandServer()->simulateUserActivity();
                    });
        });

        m_keyboard->init();
        m_pointer->init();
        m_touch->init();
        m_tablet->init();
    }
    setupInputFilters();
}

void redirect::setupInputFilters()
{
    const bool hasGlobalShortcutSupport
        = !waylandServer() || waylandServer()->hasGlobalShortcutSupport();
    if (kwinApp()->session()->hasSessionControl() && hasGlobalShortcutSupport) {
        installInputEventFilter(new virtual_terminal_filter);
    }
    if (waylandServer()) {
        installInputEventSpy(new TouchHideCursorSpy);
        if (hasGlobalShortcutSupport) {
            installInputEventFilter(new terminate_server_filter);
        }
        installInputEventFilter(new drag_and_drop_filter);
        installInputEventFilter(new lock_screen_filter);
        installInputEventFilter(new popup_filter);
        m_windowSelector = new window_selector_filter;
        installInputEventFilter(m_windowSelector);
    }
    if (hasGlobalShortcutSupport) {
        installInputEventFilter(new screen_edge_filter);
    }
    installInputEventFilter(new effects_filter);
    installInputEventFilter(new move_resize_filter);
#ifdef KWIN_BUILD_TABBOX
    installInputEventFilter(new tabbox_filter);
#endif
    if (hasGlobalShortcutSupport) {
        installInputEventFilter(new global_shortcut_filter);
    }
    installInputEventFilter(new decoration_event_filter);
    installInputEventFilter(new internal_window_filter);
    if (waylandServer()) {
        installInputEventFilter(new window_action_filter);
        installInputEventFilter(new forward_filter);
        installInputEventFilter(new fake_tablet_filter);
    }
}

void redirect::handleInputConfigChanged(const KConfigGroup& group)
{
    if (group.name() == QLatin1String("Keyboard")) {
        reconfigure();
    }
}

void redirect::reconfigure()
{
    if (!waylandServer()) {
        return;
    }
    auto inputConfig = m_inputConfigWatcher->config();
    const auto config = inputConfig->group(QStringLiteral("Keyboard"));
    const int delay = config.readEntry("RepeatDelay", 660);
    const int rate = config.readEntry("RepeatRate", 25);
    const QString repeatMode = config.readEntry("KeyRepeat", "repeat");
    // when the clients will repeat the character or turn repeat key events into an accent character
    // selection, we want to tell the clients that we are indeed repeating keys.
    const bool enabled
        = repeatMode == QLatin1String("accent") || repeatMode == QLatin1String("repeat");

    waylandServer()->seat()->setKeyRepeatInfo(enabled ? rate : 0, delay);
}

static Wrapland::Server::Seat* findSeat()
{
    auto server = waylandServer();
    if (!server) {
        return nullptr;
    }
    return server->seat();
}

void redirect::set_platform(input::platform* platform)
{
    this->platform = platform;

    assert(waylandServer());
    waylandServer()->display()->createRelativePointerManager(waylandServer()->display());

    platform->config = kwinApp()->inputConfig();

    connect(platform, &platform::pointer_added, this, [this](auto pointer) {
        connect(pointer, &pointer::button_changed, m_pointer, [this](auto const& event) {
            m_pointer->processButton(
                event.key, (PointerButtonState)event.state, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &pointer::motion, m_pointer, [this](auto const& event) {
            m_pointer->processMotion(globalPointer() + QPointF(event.delta.x(), event.delta.y()),
                                     QSizeF(event.delta.x(), event.delta.y()),
                                     QSizeF(event.unaccel_delta.x(), event.unaccel_delta.y()),
                                     event.base.time_msec,
                                     0,
                                     event.base.dev);
        });
        connect(pointer, &pointer::motion_absolute, m_pointer, [this](auto const& event) {
            auto const screens_size = screens()->size();
            auto const pos = QPointF(screens_size.width() * event.pos.x(),
                                     screens_size.height() * event.pos.y());
            m_pointer->processMotion(pos, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &pointer::axis_changed, m_pointer, [this](auto const& event) {
            m_pointer->processAxis((PointerAxis)event.orientation,
                                   event.delta,
                                   event.delta_discrete,
                                   (PointerAxisSource)event.source,
                                   event.base.time_msec,
                                   nullptr);
        });

        connect(pointer, &pointer::pinch_begin, m_pointer, [this](auto const& event) {
            m_pointer->processPinchGestureBegin(
                event.fingers, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &pointer::pinch_update, m_pointer, [this](auto const& event) {
            m_pointer->processPinchGestureUpdate(event.scale,
                                                 event.rotation,
                                                 QSize(event.delta.x(), event.delta.y()),
                                                 event.base.time_msec,
                                                 event.base.dev);
        });
        connect(pointer, &pointer::pinch_end, m_pointer, [this](auto const& event) {
            if (event.cancelled) {
                m_pointer->processPinchGestureCancelled(event.base.time_msec, event.base.dev);
            } else {
                m_pointer->processPinchGestureEnd(event.base.time_msec, event.base.dev);
            }
        });

        connect(pointer, &pointer::swipe_begin, m_pointer, [this](auto const& event) {
            m_pointer->processSwipeGestureBegin(
                event.fingers, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &pointer::swipe_update, m_pointer, [this](auto const& event) {
            m_pointer->processSwipeGestureUpdate(
                QSize(event.delta.x(), event.delta.y()), event.base.time_msec, event.base.dev);
        });
        connect(pointer, &pointer::swipe_end, m_pointer, [this](auto const& event) {
            if (event.cancelled) {
                m_pointer->processSwipeGestureCancelled(event.base.time_msec, event.base.dev);
            } else {
                m_pointer->processSwipeGestureEnd(event.base.time_msec, event.base.dev);
            }
        });

        if (auto seat = findSeat()) {
            seat->setHasPointer(true);
        }
    });

    connect(platform, &platform::pointer_removed, this, [this, platform]() {
        if (auto seat = findSeat(); seat && platform->pointers.empty()) {
            seat->setHasPointer(false);
        }
    });

    connect(platform, &platform::switch_added, this, [this](auto switch_device) {
        connect(switch_device, &switch_device::toggle, this, [this](auto const& event) {
            if (event.type == switch_type::tablet_mode) {
                Q_EMIT hasTabletModeSwitchChanged(event.state == switch_state::on);
            }
        });
    });

    connect(platform, &platform::touch_added, this, [this](auto touch) {
        auto get_abs_pos = [](auto const& event) {
            auto out = event.base.dev->output;
            if (!out) {
                auto const& outs = kwinApp()->platform()->enabledOutputs();
                if (outs.empty()) {
                    return QPointF();
                }
                out = static_cast<AbstractWaylandOutput*>(outs.front());
            }
            auto const& geo = out->geometry();
            return QPointF(geo.x() + geo.width() * event.pos.x(),
                           geo.y() + geo.height() * event.pos.y());
        };

        connect(touch, &touch::down, m_touch, [this, get_abs_pos](auto const& event) {
            auto const pos = get_abs_pos(event);
            m_touch->processDown(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
            m_touch->frame();
#endif
        });
        connect(touch, &touch::up, m_touch, [this](auto const& event) {
            m_touch->processUp(event.id, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
            m_touch->frame();
#endif
        });
        connect(touch, &touch::motion, m_touch, [this, get_abs_pos](auto const& event) {
            auto const pos = get_abs_pos(event);
            m_touch->processMotion(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
            m_touch->frame();
#endif
        });
        connect(touch, &touch::cancel, m_touch, [this]([[maybe_unused]] auto const& event) {
            m_touch->cancel();
        });
#if HAVE_WLR_TOUCH_FRAME
        connect(touch, &touch::frame, m_touch, [this] { m_touch->frame(); });
#endif

        if (auto seat = findSeat()) {
            seat->setHasTouch(true);
        }
    });

    connect(platform, &platform::touch_removed, this, [this, platform]() {
        if (auto seat = findSeat(); seat && platform->touchs.empty()) {
            seat->setHasTouch(false);
        }
    });

    connect(platform, &platform::keyboard_added, this, [this](auto keyboard) {
        connect(keyboard, &keyboard::key_changed, m_keyboard, [this](auto const& event) {
            m_keyboard->processKey(
                event.keycode, (KeyboardKeyState)event.state, event.base.time_msec, event.base.dev);
        });
        connect(keyboard, &keyboard::modifiers_changed, m_keyboard, [this](auto const& event) {
            m_keyboard->processModifiers(event.depressed, event.latched, event.locked, event.group);
        });
        if (auto seat = findSeat()) {
            seat->setHasKeyboard(true);
        }
    });

    connect(platform, &platform::keyboard_removed, this, [this, platform]() {
        if (auto seat = findSeat(); seat && platform->keyboards.empty()) {
            seat->setHasKeyboard(false);
        }
    });

    platform->update_keyboard_leds(m_keyboard->xkb()->leds());
    waylandServer()->updateKeyState(m_keyboard->xkb()->leds());
    connect(m_keyboard,
            &keyboard_redirect::ledsChanged,
            waylandServer(),
            &WaylandServer::updateKeyState);
    connect(m_keyboard, &keyboard_redirect::ledsChanged, platform, &platform::update_keyboard_leds);

    reconfigure();
    setupTouchpadShortcuts();
}

static const QString s_touchpadComponent = QStringLiteral("kcm_touchpad");

void redirect::setupTouchpadShortcuts()
{
    if (!platform) {
        return;
    }
    QAction* touchpadToggleAction = new QAction(this);
    QAction* touchpadOnAction = new QAction(this);
    QAction* touchpadOffAction = new QAction(this);

    touchpadToggleAction->setObjectName(QStringLiteral("Toggle Touchpad"));
    touchpadToggleAction->setProperty("componentName", s_touchpadComponent);
    touchpadOnAction->setObjectName(QStringLiteral("Enable Touchpad"));
    touchpadOnAction->setProperty("componentName", s_touchpadComponent);
    touchpadOffAction->setObjectName(QStringLiteral("Disable Touchpad"));
    touchpadOffAction->setProperty("componentName", s_touchpadComponent);
    KGlobalAccel::self()->setDefaultShortcut(touchpadToggleAction,
                                             QList<QKeySequence>{Qt::Key_TouchpadToggle});
    KGlobalAccel::self()->setShortcut(touchpadToggleAction,
                                      QList<QKeySequence>{Qt::Key_TouchpadToggle});
    KGlobalAccel::self()->setDefaultShortcut(touchpadOnAction,
                                             QList<QKeySequence>{Qt::Key_TouchpadOn});
    KGlobalAccel::self()->setShortcut(touchpadOnAction, QList<QKeySequence>{Qt::Key_TouchpadOn});
    KGlobalAccel::self()->setDefaultShortcut(touchpadOffAction,
                                             QList<QKeySequence>{Qt::Key_TouchpadOff});
    KGlobalAccel::self()->setShortcut(touchpadOffAction, QList<QKeySequence>{Qt::Key_TouchpadOff});

    registerShortcut(Qt::Key_TouchpadToggle, touchpadToggleAction);
    registerShortcut(Qt::Key_TouchpadOn, touchpadOnAction);
    registerShortcut(Qt::Key_TouchpadOff, touchpadOffAction);

    connect(touchpadToggleAction, &QAction::triggered, platform, &platform::toggle_touchpads);
    connect(touchpadOnAction, &QAction::triggered, platform, &platform::enable_touchpads);
    connect(touchpadOffAction, &QAction::triggered, platform, &platform::disable_touchpads);
}

bool redirect::hasTabletModeSwitch()
{
    if (platform) {
        return std::any_of(platform->switches.cbegin(), platform->switches.cend(), [](auto dev) {
            return dev->control->is_tablet_mode_switch();
        });
    }
    return false;
}

void redirect::processPointerMotion(const QPointF& pos, uint32_t time)
{
    m_pointer->processMotion(pos, time);
}

void redirect::processPointerButton(uint32_t button,
                                    redirect::PointerButtonState state,
                                    uint32_t time)
{
    m_pointer->processButton(button, state, time);
}

void redirect::processPointerAxis(redirect::PointerAxis axis,
                                  qreal delta,
                                  qint32 discreteDelta,
                                  PointerAxisSource source,
                                  uint32_t time)
{
    m_pointer->processAxis(axis, delta, discreteDelta, source, time);
}

void redirect::processKeyboardKey(uint32_t key, redirect::KeyboardKeyState state, uint32_t time)
{
    m_keyboard->processKey(key, state, time);
}

void redirect::processKeyboardModifiers(uint32_t modsDepressed,
                                        uint32_t modsLatched,
                                        uint32_t modsLocked,
                                        uint32_t group)
{
    m_keyboard->processModifiers(modsDepressed, modsLatched, modsLocked, group);
}

void redirect::processKeymapChange(int fd, uint32_t size)
{
    m_keyboard->processKeymapChange(fd, size);
}

void redirect::processTouchDown(qint32 id, const QPointF& pos, quint32 time)
{
    m_touch->processDown(id, pos, time);
}

void redirect::processTouchUp(qint32 id, quint32 time)
{
    m_touch->processUp(id, time);
}

void redirect::processTouchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    m_touch->processMotion(id, pos, time);
}

void redirect::cancelTouch()
{
    m_touch->cancel();
}

void redirect::touchFrame()
{
    m_touch->frame();
}

Qt::MouseButtons redirect::qtButtonStates() const
{
    return m_pointer->buttons();
}

static bool acceptsInput(Toplevel* t, const QPoint& pos)
{
    if (!t->surface()) {
        // Only wl_surfaces provide means of limiting the input region. So just accept otherwise.
        return true;
    }
    if (t->surface()->inputIsInfinite()) {
        return true;
    }

    auto const input_region = t->surface()->input();
    auto const localPoint = pos - win::frame_to_client_pos(t, t->pos());

    return input_region.contains(localPoint);
}

Toplevel* redirect::findToplevel(const QPoint& pos)
{
    if (!Workspace::self()) {
        return nullptr;
    }
    const bool isScreenLocked = waylandServer() && waylandServer()->isScreenLocked();
    // TODO: check whether the unmanaged wants input events at all
    if (!isScreenLocked) {
        // if an effect overrides the cursor we don't have a window to focus
        if (effects && static_cast<EffectsHandlerImpl*>(effects)->isMouseInterception()) {
            return nullptr;
        }
        auto const& unmanaged = Workspace::self()->unmanagedList();
        for (auto const& u : unmanaged) {
            if (win::input_geometry(u).contains(pos) && acceptsInput(u, pos)) {
                return u;
            }
        }
    }
    return findManagedToplevel(pos);
}

Toplevel* redirect::findManagedToplevel(const QPoint& pos)
{
    if (!Workspace::self()) {
        return nullptr;
    }
    const bool isScreenLocked = waylandServer() && waylandServer()->isScreenLocked();
    auto const& stacking = workspace()->stacking_order->sorted();
    if (stacking.empty()) {
        return nullptr;
    }
    auto it = stacking.end();
    do {
        --it;
        auto window = *it;
        if (window->isDeleted()) {
            // a deleted window doesn't get mouse events
            continue;
        }
        if (window->control) {
            if (!window->isOnCurrentActivity() || !window->isOnCurrentDesktop()
                || window->control->minimized()) {
                continue;
            }
        }
        if (window->isHiddenInternal()) {
            continue;
        }
        if (!window->readyForPainting()) {
            continue;
        }
        if (isScreenLocked) {
            if (!window->isLockScreen() && !window->isInputMethod()) {
                continue;
            }
        }
        if (win::input_geometry(window).contains(pos) && acceptsInput(window, pos)) {
            return window;
        }
    } while (it != stacking.begin());
    return nullptr;
}

Qt::KeyboardModifiers redirect::keyboardModifiers() const
{
    return m_keyboard->modifiers();
}

Qt::KeyboardModifiers redirect::modifiersRelevantForGlobalShortcuts() const
{
    return m_keyboard->modifiersRelevantForGlobalShortcuts();
}

void redirect::registerShortcut(const QKeySequence& shortcut, QAction* action)
{
    Q_UNUSED(shortcut)
    kwinApp()->platform()->setupActionForGlobalAccel(action);
}

void redirect::registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                       Qt::MouseButton pointerButtons,
                                       QAction* action)
{
    m_shortcuts->registerPointerShortcut(action, modifiers, pointerButtons);
}

void redirect::registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                                    PointerAxisDirection axis,
                                    QAction* action)
{
    m_shortcuts->registerAxisShortcut(action, modifiers, axis);
}

void redirect::registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action)
{
    m_shortcuts->registerTouchpadSwipe(action, direction);
}

void redirect::registerGlobalAccel(KGlobalAccelInterface* interface)
{
    m_shortcuts->setKGlobalAccelInterface(interface);
}

void redirect::warpPointer(const QPointF& pos)
{
    m_pointer->warp(pos);
}

bool redirect::supportsPointerWarping() const
{
    return m_pointer->supportsWarping();
}

QPointF redirect::globalPointer() const
{
    return m_pointer->pos();
}

void redirect::startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                               const QByteArray& cursorName)
{
    if (!m_windowSelector || m_windowSelector->isActive()) {
        callback(nullptr);
        return;
    }
    m_windowSelector->start(callback);
    m_pointer->setWindowSelectionCursor(cursorName);
}

void redirect::startInteractivePositionSelection(std::function<void(const QPoint&)> callback)
{
    if (!m_windowSelector || m_windowSelector->isActive()) {
        callback(QPoint(-1, -1));
        return;
    }
    m_windowSelector->start(callback);
    m_pointer->setWindowSelectionCursor(QByteArray());
}

bool redirect::isSelectingWindow() const
{
    return m_windowSelector ? m_windowSelector->isActive() : false;
}

}
