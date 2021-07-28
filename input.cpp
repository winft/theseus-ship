/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 Roman Gilg <subdiff@gmail.com>
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
#include "input.h"

#include "abstract_wayland_output.h"
#include "decorations/decoratedclient.h"
#include "effects.h"
#include "gestures.h"
#include "globalshortcuts.h"
#include "input/filters/decoration_event.h"
#include "input/filters/drag_and_drop.h"
#include "input/filters/effects.h"
#include "input/filters/fake_tablet.h"
#include "input/filters/forward.h"
#include "input/filters/global_shortcut.h"
#include "input/filters/internal_window.h"
#include "input/filters/lock_screen.h"
#include "input/filters/move_resize.h"
#include "input/filters/popup.h"
#include "input/filters/screen_edge.h"
#ifdef KWIN_BUILD_TABBOX
#include "input/filters/tabbox.h"
#endif
#include "input/filters/terminate_server.h"
#include "input/filters/virtual_terminal.h"
#include "input/filters/window_action.h"
#include "input/filters/window_selector.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/platform.h"
#include "input/pointer.h"
#include "input/pointer_redirect.h"
#include "input/switch.h"
#include "input/tablet_redirect.h"
#include "input/touch.h"
#include "input/touch_redirect.h"
#include "input_event.h"
#include "input_event_spy.h"
#include "main.h"
#include "platform.h"
#include "screenedge.h"
#include "screens.h"
#include "seat/session.h"
#include "touch_hide_cursor_spy.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xwl/xwayland_interface.h"

#include "win/input.h"
#include "win/internal_client.h"
#include "win/move.h"
#include "win/stacking_order.h"

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/fake_input.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <KDecoration2/Decoration>
#include <KGlobalAccel>

// screenlocker
#include <KScreenLocker/KsldApp>
// Qt
#include <QKeyEvent>
#include <QWindow>

#include <xkbcommon/xkbcommon.h>

namespace KWin
{

static const QString s_touchpadComponent = QStringLiteral("kcm_touchpad");

InputRedirection::InputRedirection()
    : QObject(nullptr)
    , m_keyboard(new input::keyboard_redirect(this))
    , m_pointer(new input::pointer_redirect(this))
    , m_tablet(new input::tablet_redirect(this))
    , m_touch(new input::touch_redirect(this))
    , m_shortcuts(new GlobalShortcutsManager(this))
    , m_inputConfigWatcher{KConfigWatcher::create(kwinApp()->inputConfig())}
{
    qRegisterMetaType<KWin::InputRedirection::KeyboardKeyState>();
    qRegisterMetaType<KWin::InputRedirection::PointerButtonState>();
    qRegisterMetaType<KWin::InputRedirection::PointerAxis>();
    connect(kwinApp(), &Application::workspaceCreated, this, &InputRedirection::setupWorkspace);
    reconfigure();
}

InputRedirection::~InputRedirection()
{
    qDeleteAll(m_filters);
    qDeleteAll(m_spies);
}

void InputRedirection::installInputEventFilter(input::event_filter* filter)
{
    Q_ASSERT(!m_filters.contains(filter));
    m_filters << filter;
}

void InputRedirection::prependInputEventFilter(input::event_filter* filter)
{
    Q_ASSERT(!m_filters.contains(filter));
    m_filters.prepend(filter);
}

void InputRedirection::uninstallInputEventFilter(input::event_filter* filter)
{
    m_filters.removeOne(filter);
}

void InputRedirection::installInputEventSpy(InputEventSpy* spy)
{
    m_spies << spy;
}

void InputRedirection::uninstallInputEventSpy(InputEventSpy* spy)
{
    m_spies.removeOne(spy);
}

void InputRedirection::init()
{
    m_shortcuts->init();
}

void InputRedirection::setupWorkspace()
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
                        m_pointer->processButton(button, InputRedirection::PointerButtonPressed, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::pointerButtonReleaseRequested,
                    this,
                    [this](quint32 button) {
                        // TODO: Fix time
                        m_pointer->processButton(
                            button, InputRedirection::PointerButtonReleased, 0);
                        waylandServer()->simulateUserActivity();
                    });
            connect(device,
                    &FakeInputDevice::pointerAxisRequested,
                    this,
                    [this](Qt::Orientation orientation, qreal delta) {
                        // TODO: Fix time
                        InputRedirection::PointerAxis axis;
                        switch (orientation) {
                        case Qt::Horizontal:
                            axis = InputRedirection::PointerAxisHorizontal;
                            break;
                        case Qt::Vertical:
                            axis = InputRedirection::PointerAxisVertical;
                            break;
                        default:
                            Q_UNREACHABLE();
                            break;
                        }
                        // TODO: Fix time
                        m_pointer->processAxis(
                            axis, delta, 0, InputRedirection::PointerAxisSourceUnknown, 0);
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
                    m_keyboard->processKey(button, InputRedirection::KeyboardKeyPressed, 0);
                    waylandServer()->simulateUserActivity();
                });
            connect(device,
                    &FakeInputDevice::keyboardKeyReleaseRequested,
                    this,
                    [this](quint32 button) {
                        // TODO: Fix time
                        m_keyboard->processKey(button, InputRedirection::KeyboardKeyReleased, 0);
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

void InputRedirection::setupInputFilters()
{
    const bool hasGlobalShortcutSupport
        = !waylandServer() || waylandServer()->hasGlobalShortcutSupport();
    if (kwinApp()->session()->hasSessionControl() && hasGlobalShortcutSupport) {
        installInputEventFilter(new input::virtual_terminal_filter);
    }
    if (waylandServer()) {
        installInputEventSpy(new TouchHideCursorSpy);
        if (hasGlobalShortcutSupport) {
            installInputEventFilter(new input::terminate_server_filter);
        }
        installInputEventFilter(new input::drag_and_drop_filter);
        installInputEventFilter(new input::lock_screen_filter);
        installInputEventFilter(new input::popup_filter);
        m_windowSelector = new input::window_selector_filter;
        installInputEventFilter(m_windowSelector);
    }
    if (hasGlobalShortcutSupport) {
        installInputEventFilter(new input::screen_edge_filter);
    }
    installInputEventFilter(new input::effects_filter);
    installInputEventFilter(new input::move_resize_filter);
#ifdef KWIN_BUILD_TABBOX
    installInputEventFilter(new input::tabbox_filter);
#endif
    if (hasGlobalShortcutSupport) {
        installInputEventFilter(new input::global_shortcut_filter);
    }
    installInputEventFilter(new input::decoration_event_filter);
    installInputEventFilter(new input::internal_window_filter);
    if (waylandServer()) {
        installInputEventFilter(new input::window_action_filter);
        installInputEventFilter(new input::forward_filter);
        installInputEventFilter(new input::fake_tablet_filter);
    }
}

void InputRedirection::handleInputConfigChanged(const KConfigGroup& group)
{
    if (group.name() == QLatin1String("Keyboard")) {
        reconfigure();
    }
}

void InputRedirection::reconfigure()
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

void InputRedirection::set_platform(input::platform* platform)
{
    this->platform = platform;

    assert(waylandServer());
    waylandServer()->display()->createRelativePointerManager(waylandServer()->display());

    platform->config = kwinApp()->inputConfig();

    connect(platform, &input::platform::pointer_added, this, [this](auto pointer) {
        connect(pointer, &input::pointer::button_changed, m_pointer, [this](auto const& event) {
            m_pointer->processButton(
                event.key, (PointerButtonState)event.state, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &input::pointer::motion, m_pointer, [this](auto const& event) {
            m_pointer->processMotion(globalPointer() + QPointF(event.delta.x(), event.delta.y()),
                                     QSizeF(event.delta.x(), event.delta.y()),
                                     QSizeF(event.unaccel_delta.x(), event.unaccel_delta.y()),
                                     event.base.time_msec,
                                     0,
                                     event.base.dev);
        });
        connect(pointer, &input::pointer::motion_absolute, m_pointer, [this](auto const& event) {
            auto const screens_size = screens()->size();
            auto const pos = QPointF(screens_size.width() * event.pos.x(),
                                     screens_size.height() * event.pos.y());
            m_pointer->processMotion(pos, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &input::pointer::axis_changed, m_pointer, [this](auto const& event) {
            m_pointer->processAxis((PointerAxis)event.orientation,
                                   event.delta,
                                   event.delta_discrete,
                                   (PointerAxisSource)event.source,
                                   event.base.time_msec,
                                   nullptr);
        });

        connect(pointer, &input::pointer::pinch_begin, m_pointer, [this](auto const& event) {
            m_pointer->processPinchGestureBegin(
                event.fingers, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &input::pointer::pinch_update, m_pointer, [this](auto const& event) {
            m_pointer->processPinchGestureUpdate(event.scale,
                                                 event.rotation,
                                                 QSize(event.delta.x(), event.delta.y()),
                                                 event.base.time_msec,
                                                 event.base.dev);
        });
        connect(pointer, &input::pointer::pinch_end, m_pointer, [this](auto const& event) {
            if (event.cancelled) {
                m_pointer->processPinchGestureCancelled(event.base.time_msec, event.base.dev);
            } else {
                m_pointer->processPinchGestureEnd(event.base.time_msec, event.base.dev);
            }
        });

        connect(pointer, &input::pointer::swipe_begin, m_pointer, [this](auto const& event) {
            m_pointer->processSwipeGestureBegin(
                event.fingers, event.base.time_msec, event.base.dev);
        });
        connect(pointer, &input::pointer::swipe_update, m_pointer, [this](auto const& event) {
            m_pointer->processSwipeGestureUpdate(
                QSize(event.delta.x(), event.delta.y()), event.base.time_msec, event.base.dev);
        });
        connect(pointer, &input::pointer::swipe_end, m_pointer, [this](auto const& event) {
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

    connect(platform, &input::platform::pointer_removed, this, [this, platform]() {
        if (auto seat = findSeat(); seat && platform->pointers.empty()) {
            seat->setHasPointer(false);
        }
    });

    connect(platform, &input::platform::switch_added, this, [this](auto switch_device) {
        connect(switch_device, &input::switch_device::toggle, this, [this](auto const& event) {
            if (event.type == input::switch_type::tablet_mode) {
                Q_EMIT hasTabletModeSwitchChanged(event.state == input::switch_state::on);
            }
        });
    });

    connect(platform, &input::platform::touch_added, this, [this](auto touch) {
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

        connect(touch, &input::touch::down, m_touch, [this, get_abs_pos](auto const& event) {
            auto const pos = get_abs_pos(event);
            m_touch->processDown(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
            m_touch->frame();
#endif
        });
        connect(touch, &input::touch::up, m_touch, [this](auto const& event) {
            m_touch->processUp(event.id, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
            m_touch->frame();
#endif
        });
        connect(touch, &input::touch::motion, m_touch, [this, get_abs_pos](auto const& event) {
            auto const pos = get_abs_pos(event);
            m_touch->processMotion(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
            m_touch->frame();
#endif
        });
        connect(touch, &input::touch::cancel, m_touch, [this]([[maybe_unused]] auto const& event) {
            m_touch->cancel();
        });
#if HAVE_WLR_TOUCH_FRAME
        connect(touch, &input::touch::frame, m_touch, [this, touch]() {
            m_touch->frame();
        });
#endif

        if (auto seat = findSeat()) {
            seat->setHasTouch(true);
        }
    });

    connect(platform, &input::platform::touch_removed, this, [this, platform]() {
        if (auto seat = findSeat(); seat && platform->touchs.empty()) {
            seat->setHasTouch(false);
        }
    });

    connect(platform, &input::platform::keyboard_added, this, [this](auto keyboard) {
        connect(keyboard, &input::keyboard::key_changed, m_keyboard, [this](auto const& event) {
            m_keyboard->processKey(
                event.keycode, (KeyboardKeyState)event.state, event.base.time_msec, event.base.dev);
        });
        connect(
            keyboard, &input::keyboard::modifiers_changed, m_keyboard, [this](auto const& event) {
                m_keyboard->processModifiers(
                    event.depressed, event.latched, event.locked, event.group);
            });
        if (auto seat = findSeat()) {
            seat->setHasKeyboard(true);
        }
    });

    connect(platform, &input::platform::keyboard_removed, this, [this, platform]() {
        if (auto seat = findSeat(); seat && platform->keyboards.empty()) {
            seat->setHasKeyboard(false);
        }
    });

    platform->update_keyboard_leds(m_keyboard->xkb()->leds());
    waylandServer()->updateKeyState(m_keyboard->xkb()->leds());
    connect(m_keyboard,
            &input::keyboard_redirect::ledsChanged,
            waylandServer(),
            &WaylandServer::updateKeyState);
    connect(m_keyboard,
            &input::keyboard_redirect::ledsChanged,
            platform,
            &input::platform::update_keyboard_leds);

    reconfigure();
    setupTouchpadShortcuts();
}

void InputRedirection::setupTouchpadShortcuts()
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

    connect(
        touchpadToggleAction, &QAction::triggered, platform, &input::platform::toggle_touchpads);
    connect(touchpadOnAction, &QAction::triggered, platform, &input::platform::enable_touchpads);
    connect(touchpadOffAction, &QAction::triggered, platform, &input::platform::disable_touchpads);
}

bool InputRedirection::hasTabletModeSwitch()
{
    if (platform) {
        return std::any_of(platform->switches.cbegin(), platform->switches.cend(), [](auto dev) {
            return dev->control->is_tablet_mode_switch();
        });
    }
    return false;
}

void InputRedirection::processPointerMotion(const QPointF& pos, uint32_t time)
{
    m_pointer->processMotion(pos, time);
}

void InputRedirection::processPointerButton(uint32_t button,
                                            InputRedirection::PointerButtonState state,
                                            uint32_t time)
{
    m_pointer->processButton(button, state, time);
}

void InputRedirection::processPointerAxis(InputRedirection::PointerAxis axis,
                                          qreal delta,
                                          qint32 discreteDelta,
                                          PointerAxisSource source,
                                          uint32_t time)
{
    m_pointer->processAxis(axis, delta, discreteDelta, source, time);
}

void InputRedirection::processKeyboardKey(uint32_t key,
                                          InputRedirection::KeyboardKeyState state,
                                          uint32_t time)
{
    m_keyboard->processKey(key, state, time);
}

void InputRedirection::processKeyboardModifiers(uint32_t modsDepressed,
                                                uint32_t modsLatched,
                                                uint32_t modsLocked,
                                                uint32_t group)
{
    m_keyboard->processModifiers(modsDepressed, modsLatched, modsLocked, group);
}

void InputRedirection::processKeymapChange(int fd, uint32_t size)
{
    m_keyboard->processKeymapChange(fd, size);
}

void InputRedirection::processTouchDown(qint32 id, const QPointF& pos, quint32 time)
{
    m_touch->processDown(id, pos, time);
}

void InputRedirection::processTouchUp(qint32 id, quint32 time)
{
    m_touch->processUp(id, time);
}

void InputRedirection::processTouchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    m_touch->processMotion(id, pos, time);
}

void InputRedirection::cancelTouch()
{
    m_touch->cancel();
}

void InputRedirection::touchFrame()
{
    m_touch->frame();
}

Qt::MouseButtons InputRedirection::qtButtonStates() const
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

Toplevel* InputRedirection::findToplevel(const QPoint& pos)
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

Toplevel* InputRedirection::findManagedToplevel(const QPoint& pos)
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

Qt::KeyboardModifiers InputRedirection::keyboardModifiers() const
{
    return m_keyboard->modifiers();
}

Qt::KeyboardModifiers InputRedirection::modifiersRelevantForGlobalShortcuts() const
{
    return m_keyboard->modifiersRelevantForGlobalShortcuts();
}

void InputRedirection::registerShortcut(const QKeySequence& shortcut, QAction* action)
{
    Q_UNUSED(shortcut)
    kwinApp()->platform()->setupActionForGlobalAccel(action);
}

void InputRedirection::registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                               Qt::MouseButton pointerButtons,
                                               QAction* action)
{
    m_shortcuts->registerPointerShortcut(action, modifiers, pointerButtons);
}

void InputRedirection::registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                                            PointerAxisDirection axis,
                                            QAction* action)
{
    m_shortcuts->registerAxisShortcut(action, modifiers, axis);
}

void InputRedirection::registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action)
{
    m_shortcuts->registerTouchpadSwipe(action, direction);
}

void InputRedirection::registerGlobalAccel(KGlobalAccelInterface* interface)
{
    m_shortcuts->setKGlobalAccelInterface(interface);
}

void InputRedirection::warpPointer(const QPointF& pos)
{
    m_pointer->warp(pos);
}

bool InputRedirection::supportsPointerWarping() const
{
    return m_pointer->supportsWarping();
}

QPointF InputRedirection::globalPointer() const
{
    return m_pointer->pos();
}

void InputRedirection::startInteractiveWindowSelection(
    std::function<void(KWin::Toplevel*)> callback,
    const QByteArray& cursorName)
{
    if (!m_windowSelector || m_windowSelector->isActive()) {
        callback(nullptr);
        return;
    }
    m_windowSelector->start(callback);
    m_pointer->setWindowSelectionCursor(cursorName);
}

void InputRedirection::startInteractivePositionSelection(
    std::function<void(const QPoint&)> callback)
{
    if (!m_windowSelector || m_windowSelector->isActive()) {
        callback(QPoint(-1, -1));
        return;
    }
    m_windowSelector->start(callback);
    m_pointer->setWindowSelectionCursor(QByteArray());
}

bool InputRedirection::isSelectingWindow() const
{
    return m_windowSelector ? m_windowSelector->isActive() : false;
}

} // namespace
