/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "console.h"

#include "render/compositor.h"

#include "input/dbus/device.h"
#include "input/dbus/device_manager.h"
#include "input/event.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/platform.h"
#include "input/pointer.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/switch.h"

#include "main.h"
#include "scene.h"
#include "wayland_server.h"
#include "workspace.h"
#include <kwinglplatform.h>
#include <kwinglutils.h>

#include "win/control.h"
#include "win/internal_client.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include "ui_debug_console.h"

// Wrapland
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>
// frameworks
#include <KLocalizedString>
#include <NETWM>
// Qt
#include <QMetaProperty>
#include <QMetaType>
#include <QMouseEvent>
#include <QWindow>

// xkb
#include <xkbcommon/xkbcommon.h>

#include <functional>

namespace KWin::debug
{

static QString tableHeaderRow(const QString& title)
{
    return QStringLiteral("<tr><th colspan=\"2\">%1</th></tr>").arg(title);
}

template<typename T>
static QString tableRow(const QString& title, const T& argument)
{
    return QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(title).arg(argument);
}

static QString timestampRow(quint32 timestamp)
{
    return tableRow(i18n("Timestamp"), timestamp);
}

static QString timestampRowUsec(quint64 timestamp)
{
    return tableRow(i18n("Timestamp (µsec)"), timestamp);
}

static QString buttonToString(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return i18nc("A mouse button", "Left");
    case Qt::RightButton:
        return i18nc("A mouse button", "Right");
    case Qt::MiddleButton:
        return i18nc("A mouse button", "Middle");
    case Qt::BackButton:
        return i18nc("A mouse button", "Back");
    case Qt::ForwardButton:
        return i18nc("A mouse button", "Forward");
    case Qt::TaskButton:
        return i18nc("A mouse button", "Task");
    case Qt::ExtraButton4:
        return i18nc("A mouse button", "Extra Button 4");
    case Qt::ExtraButton5:
        return i18nc("A mouse button", "Extra Button 5");
    case Qt::ExtraButton6:
        return i18nc("A mouse button", "Extra Button 6");
    case Qt::ExtraButton7:
        return i18nc("A mouse button", "Extra Button 7");
    case Qt::ExtraButton8:
        return i18nc("A mouse button", "Extra Button 8");
    case Qt::ExtraButton9:
        return i18nc("A mouse button", "Extra Button 9");
    case Qt::ExtraButton10:
        return i18nc("A mouse button", "Extra Button 10");
    case Qt::ExtraButton11:
        return i18nc("A mouse button", "Extra Button 11");
    case Qt::ExtraButton12:
        return i18nc("A mouse button", "Extra Button 12");
    case Qt::ExtraButton13:
        return i18nc("A mouse button", "Extra Button 13");
    case Qt::ExtraButton14:
        return i18nc("A mouse button", "Extra Button 14");
    case Qt::ExtraButton15:
        return i18nc("A mouse button", "Extra Button 15");
    case Qt::ExtraButton16:
        return i18nc("A mouse button", "Extra Button 16");
    case Qt::ExtraButton17:
        return i18nc("A mouse button", "Extra Button 17");
    case Qt::ExtraButton18:
        return i18nc("A mouse button", "Extra Button 18");
    case Qt::ExtraButton19:
        return i18nc("A mouse button", "Extra Button 19");
    case Qt::ExtraButton20:
        return i18nc("A mouse button", "Extra Button 20");
    case Qt::ExtraButton21:
        return i18nc("A mouse button", "Extra Button 21");
    case Qt::ExtraButton22:
        return i18nc("A mouse button", "Extra Button 22");
    case Qt::ExtraButton23:
        return i18nc("A mouse button", "Extra Button 23");
    case Qt::ExtraButton24:
        return i18nc("A mouse button", "Extra Button 24");
    default:
        return QString();
    }
}

template<typename Device>
static QString deviceRow(Device* dev)
{
    auto ctrl = dev ? dev->control : nullptr;
    if (!ctrl) {
        return tableRow(i18n("Input Device"),
                        i18nc("The input device of the event is not known", "Unknown"));
    }
    return tableRow(i18n("Input Device"),
                    QStringLiteral("%1 (%2)")
                        .arg(ctrl->metadata.name.c_str())
                        .arg(ctrl->metadata.sys_name.c_str()));
}

static QString buttonsToString(Qt::MouseButtons buttons)
{
    QString ret;
    for (uint i = 1; i < Qt::ExtraButton24; i = i << 1) {
        if (buttons & i) {
            ret.append(buttonToString(Qt::MouseButton(uint(buttons) & i)));
            ret.append(QStringLiteral(" "));
        }
    };
    return ret.trimmed();
}

static const QString s_hr = QStringLiteral("<hr/>");
static const QString s_tableStart = QStringLiteral("<table>");
static const QString s_tableEnd = QStringLiteral("</table>");

console_filter::console_filter(QTextEdit* textEdit)
    : input::event_spy()
    , m_textEdit(textEdit)
{
    m_textEdit->document()->setMaximumBlockCount(1000);
}

console_filter::~console_filter() = default;

void console_filter::button(input::button_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);
    text.append(s_tableStart);

    auto qt_button = buttonToString(input::button_to_qt_mouse_button(event.key));
    auto buttons = buttonsToString(kwinApp()->input->redirect->pointer()->buttons());
    switch (event.state) {
    case input::button_state::pressed:
        text.append(
            tableHeaderRow(i18nc("A mouse pointer button press event", "Pointer Button Press")));
        text.append(deviceRow(event.base.dev));
        text.append(timestamp);
        text.append(
            tableRow(i18nc("A button in a mouse press/release event", "Button"), qt_button));
        text.append(tableRow(i18nc("A button in a mouse press/release event", "Native Button code"),
                             event.key));
        text.append(tableRow(i18nc("All currently pressed buttons in a mouse press/release event",
                                   "Pressed Buttons"),
                             buttons));
        break;
    case input::button_state::released:
        text.append(tableHeaderRow(
            i18nc("A mouse pointer button release event", "Pointer Button Release")));
        text.append(deviceRow(event.base.dev));
        text.append(timestamp);
        text.append(
            tableRow(i18nc("A button in a mouse press/release event", "Button"), qt_button));
        text.append(tableRow(i18nc("A button in a mouse press/release event", "Native Button code"),
                             event.key));
        text.append(tableRow(i18nc("All currently pressed buttons in a mouse press/release event",
                                   "Pressed Buttons"),
                             buttons));
        break;
    }

    text.append(s_tableEnd);
    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::motion(input::motion_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A mouse pointer motion event", "Pointer Motion")));
    text.append(deviceRow(event.base.dev));
    text.append(timestamp);

    if (event.base.time_msec != 0) {
        text.append(timestampRowUsec(event.base.time_msec));
    }
    if (event.delta != QPointF()) {
        text.append(tableRow(i18nc("The relative mouse movement", "Delta"),
                             QStringLiteral("%1/%2").arg(event.delta.x()).arg(event.delta.y())));
    }
    if (event.unaccel_delta != QPointF()) {
        text.append(tableRow(
            i18nc("The relative mouse movement", "Delta (not accelerated)"),
            QStringLiteral("%1/%2").arg(event.unaccel_delta.x()).arg(event.unaccel_delta.y())));
    }

    auto pos = kwinApp()->input->redirect->globalPointer();
    text.append(tableRow(i18nc("The global mouse pointer position", "Global Position"),
                         QStringLiteral("%1/%2").arg(pos.x()).arg(pos.y())));

    text.append(s_tableEnd);
    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::axis(input::axis_event const& event)
{
    auto text = s_hr;
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A mouse pointer axis (wheel) event", "Pointer Axis")));
    text.append(deviceRow(event.base.dev));
    text.append(timestampRow(event.base.time_msec));

    text.append(tableRow(i18nc("The orientation of a pointer axis event", "Orientation"),
                         (event.orientation == input::axis_orientation::horizontal)
                             ? i18nc("An orientation of a pointer axis event", "Horizontal")
                             : i18nc("An orientation of a pointer axis event", "Vertical")));
    text.append(tableRow(i18nc("The angle delta of a pointer axis event", "Delta"), event.delta));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void add_common_key_data(input::key_event const& event, QString& text)
{
    text.append(timestampRow(event.base.time_msec));
    text.append(
        tableRow(i18nc("The code as read from the input device", "Scan code"), event.keycode));

    auto const key_meta_object = Qt::qt_getEnumMetaObject(Qt::Key());
    auto const enumerator = key_meta_object->enumerator(key_meta_object->indexOfEnumerator("Key"));
    text.append(tableRow(i18nc("Key according to Qt", "Qt::Key code"),
                         enumerator.valueToKey(input::key_to_qt_key(event.keycode))));

    auto const& xkb = kwinApp()->input->redirect->keyboard()->xkb();
    auto const keysym = xkb->toKeysym(event.keycode);
    text.append(tableRow(i18nc("The translated code to an Xkb symbol", "Xkb symbol"), keysym));
    text.append(
        tableRow(i18nc("The translated code interpreted as text", "Utf8"), xkb->toString(keysym)));

    auto to_string = [](Qt::KeyboardModifiers mods) {
        QString ret;

        if (mods.testFlag(Qt::ShiftModifier)) {
            ret.append(i18nc("A keyboard modifier", "Shift"));
            ret.append(QStringLiteral(" "));
        }
        if (mods.testFlag(Qt::ControlModifier)) {
            ret.append(i18nc("A keyboard modifier", "Control"));
            ret.append(QStringLiteral(" "));
        }
        if (mods.testFlag(Qt::AltModifier)) {
            ret.append(i18nc("A keyboard modifier", "Alt"));
            ret.append(QStringLiteral(" "));
        }
        if (mods.testFlag(Qt::MetaModifier)) {
            ret.append(i18nc("A keyboard modifier", "Meta"));
            ret.append(QStringLiteral(" "));
        }
        if (mods.testFlag(Qt::KeypadModifier)) {
            ret.append(i18nc("A keyboard modifier", "Keypad"));
            ret.append(QStringLiteral(" "));
        }
        if (mods.testFlag(Qt::GroupSwitchModifier)) {
            ret.append(i18nc("A keyboard modifier", "Group-switch"));
            ret.append(QStringLiteral(" "));
        }
        return ret;
    };

    text.append(tableRow(i18nc("The currently active modifiers", "Modifiers"),
                         to_string(kwinApp()->input->redirect->keyboard()->modifiers())));
    text.append(s_tableEnd);
}

void console_filter::key(input::key_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    switch (event.state) {
    case input::button_state::pressed:
        text.append(tableHeaderRow(i18nc("A key press event", "Key Press")));
        break;
    case input::button_state::released:
        text.append(tableHeaderRow(i18nc("A key release event", "Key Release")));
        break;
    }

    text.append(deviceRow(event.base.dev));
    add_common_key_data(event, text);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::key_repeat(input::key_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A key repeat event", "Key repeat")));
    add_common_key_data(event, text);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    QString text = s_hr;
    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A touch down event", "Touch down")));
    text.append(timestampRow(time));
    text.append(
        tableRow(i18nc("The id of the touch point in the touch event", "Point identifier"), id));
    text.append(tableRow(i18nc("The global position of the touch point", "Global position"),
                         QStringLiteral("%1/%2").arg(pos.x()).arg(pos.y())));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    QString text = s_hr;
    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A touch motion event", "Touch Motion")));
    text.append(timestampRow(time));
    text.append(
        tableRow(i18nc("The id of the touch point in the touch event", "Point identifier"), id));
    text.append(tableRow(i18nc("The global position of the touch point", "Global position"),
                         QStringLiteral("%1/%2").arg(pos.x()).arg(pos.y())));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::touchUp(qint32 id, quint32 time)
{
    QString text = s_hr;
    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A touch up event", "Touch Up")));
    text.append(timestampRow(time));
    text.append(
        tableRow(i18nc("The id of the touch point in the touch event", "Point identifier"), id));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::pinch_begin(input::pinch_begin_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A pinch gesture is started", "Pinch start")));
    text.append(timestamp);
    text.append(
        tableRow(i18nc("Number of fingers in this pinch gesture", "Finger count"), event.fingers));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::pinch_update(input::pinch_update_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A pinch gesture is updated", "Pinch update")));
    text.append(timestamp);
    text.append(tableRow(i18nc("Current scale in pinch gesture", "Scale"), event.scale));
    text.append(tableRow(i18nc("Current angle in pinch gesture", "Angle delta"), event.rotation));
    text.append(tableRow(i18nc("Current delta in pinch gesture", "Delta x"), event.delta.x()));
    text.append(tableRow(i18nc("Current delta in pinch gesture", "Delta y"), event.delta.y()));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::pinch_end(input::pinch_end_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);
    if (event.cancelled) {
        text.append(tableHeaderRow(i18nc("A pinch gesture got cancelled", "Pinch cancelled")));
    } else {
        text.append(tableHeaderRow(i18nc("A pinch gesture ended", "Pinch end")));
    }
    text.append(timestamp);
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::swipe_begin(input::swipe_begin_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A swipe gesture is started", "Swipe start")));
    text.append(timestamp);
    text.append(
        tableRow(i18nc("Number of fingers in this swipe gesture", "Finger count"), event.fingers));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::swipe_update(input::swipe_update_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);
    text.append(tableHeaderRow(i18nc("A swipe gesture is updated", "Swipe update")));
    text.append(timestamp);
    text.append(tableRow(i18nc("Current delta in swipe gesture", "Delta x"), event.delta.x()));
    text.append(tableRow(i18nc("Current delta in swipe gesture", "Delta y"), event.delta.y()));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::swipe_end(input::swipe_end_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);

    if (event.cancelled) {
        text.append(tableHeaderRow(i18nc("A swipe gesture got cancelled", "Swipe cancelled")));
    } else {
        text.append(tableHeaderRow(i18nc("A swipe gesture ended", "Swipe end")));
    }

    text.append(timestamp);
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::switchEvent(input::SwitchEvent* event)
{
    QString text = s_hr;
    text.append(s_tableStart);
    text.append(tableHeaderRow(
        i18nc("A hardware switch (e.g. notebook lid) got toggled", "Switch toggled")));
    text.append(timestampRow(event->timestamp()));
    if (event->timestampMicroseconds() != 0) {
        text.append(timestampRowUsec(event->timestampMicroseconds()));
    }
    text.append(deviceRow(event->device()));
    QString switchName;
    if (event->device()->control->is_lid_switch()) {
        switchName = i18nc("Name of a hardware switch", "Notebook lid");
    } else if (event->device()->control->is_tablet_mode_switch()) {
        switchName = i18nc("Name of a hardware switch", "Tablet mode");
    }
    text.append(tableRow(i18nc("A hardware switch", "Switch"), switchName));
    QString switchState;
    switch (event->state()) {
    case input::SwitchEvent::State::Off:
        switchState = i18nc("The hardware switch got turned off", "Off");
        break;
    case input::SwitchEvent::State::On:
        switchState = i18nc("The hardware switch got turned on", "On");
        break;
    default:
        Q_UNREACHABLE();
    }
    text.append(tableRow(i18nc("State of a hardware switch (on/off)", "State"), switchState));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::tabletToolEvent(QTabletEvent* event)
{
    QString typeString;
    {
        QDebug d(&typeString);
        d << event->type();
    }

    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Tool"))
        + tableRow(i18n("EventType"), typeString)
        + tableRow(i18n("Position"),
                   QStringLiteral("%1,%2").arg(event->pos().x()).arg(event->pos().y()))
        + tableRow(i18n("Tilt"), QStringLiteral("%1,%2").arg(event->xTilt()).arg(event->yTilt()))
        + tableRow(i18n("Rotation"), QString::number(event->rotation()))
        + tableRow(i18n("Pressure"), QString::number(event->pressure()))
        + tableRow(i18n("Buttons"), QString::number(event->buttons()))
        + tableRow(i18n("Modifiers"), QString::number(event->modifiers())) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::tabletToolButtonEvent(const QSet<uint>& pressedButtons)
{
    QString buttons;
    for (uint b : pressedButtons) {
        buttons += QString::number(b) + ' ';
    }
    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Tool Button"))
        + tableRow(i18n("Pressed Buttons"), buttons) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::tabletPadButtonEvent(const QSet<uint>& pressedButtons)
{
    QString buttons;
    for (uint b : pressedButtons) {
        buttons += QString::number(b) + ' ';
    }
    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Pad Button"))
        + tableRow(i18n("Pressed Buttons"), buttons) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::tabletPadStripEvent(int number, int position, bool isFinger)
{
    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Pad Strip"))
        + tableRow(i18n("Number"), number) + tableRow(i18n("Position"), position)
        + tableRow(i18n("isFinger"), isFinger) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void console_filter::tabletPadRingEvent(int number, int position, bool isFinger)
{
    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Pad Ring"))
        + tableRow(i18n("Number"), number) + tableRow(i18n("Position"), position)
        + tableRow(i18n("isFinger"), isFinger) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

console::console()
    : QWidget()
    , m_ui(new Ui::debug_console)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_ui->setupUi(this);
    m_ui->windowsView->setItemDelegate(new console_delegate(this));
    m_ui->windowsView->setModel(new console_model(this));
    m_ui->surfacesView->setModel(new surface_tree_model(this));
    if (waylandServer()) {
        m_ui->inputDevicesView->setModel(new input_device_model(this));
        m_ui->inputDevicesView->setItemDelegate(new console_delegate(this));
    }
    m_ui->quitButton->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
    m_ui->tabWidget->setTabIcon(0, QIcon::fromTheme(QStringLiteral("view-list-tree")));
    m_ui->tabWidget->setTabIcon(1, QIcon::fromTheme(QStringLiteral("view-list-tree")));

    if (kwinApp()->operationMode() == Application::OperationMode::OperationModeX11) {
        m_ui->tabWidget->setTabEnabled(1, false);
        m_ui->tabWidget->setTabEnabled(2, false);
    }
    if (!waylandServer()) {
        m_ui->tabWidget->setTabEnabled(3, false);
    }

    connect(m_ui->quitButton, &QAbstractButton::clicked, this, &console::deleteLater);
    connect(m_ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        // delay creation of input event filter until the tab is selected
        if (index == 2 && m_inputFilter.isNull()) {
            m_inputFilter.reset(new console_filter(m_ui->inputTextEdit));
            kwinApp()->input->redirect->installInputEventSpy(m_inputFilter.data());
        }
        if (index == 5) {
            updateKeyboardTab();
            connect(kwinApp()->input->redirect.get(),
                    &input::redirect::keyStateChanged,
                    this,
                    &console::updateKeyboardTab);
        }
    });

    // for X11
    setWindowFlags(Qt::X11BypassWindowManagerHint);

    initGLTab();
}

console::~console() = default;

void console::initGLTab()
{
    if (!effects || !effects->isOpenGLCompositing()) {
        m_ui->noOpenGLLabel->setVisible(true);
        m_ui->glInfoScrollArea->setVisible(false);
        return;
    }
    GLPlatform* gl = GLPlatform::instance();
    m_ui->noOpenGLLabel->setVisible(false);
    m_ui->glInfoScrollArea->setVisible(true);
    m_ui->glVendorStringLabel->setText(QString::fromLocal8Bit(gl->glVendorString()));
    m_ui->glRendererStringLabel->setText(QString::fromLocal8Bit(gl->glRendererString()));
    m_ui->glVersionStringLabel->setText(QString::fromLocal8Bit(gl->glVersionString()));
    m_ui->glslVersionStringLabel->setText(
        QString::fromLocal8Bit(gl->glShadingLanguageVersionString()));
    m_ui->glDriverLabel->setText(GLPlatform::driverToString(gl->driver()));
    m_ui->glGPULabel->setText(GLPlatform::chipClassToString(gl->chipClass()));
    m_ui->glVersionLabel->setText(GLPlatform::versionToString(gl->glVersion()));
    m_ui->glslLabel->setText(GLPlatform::versionToString(gl->glslVersion()));

    auto extensionsString = [](const auto& extensions) {
        QString text = QStringLiteral("<ul>");
        for (auto extension : extensions) {
            text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(extension)));
        }
        text.append(QStringLiteral("</ul>"));
        return text;
    };

    m_ui->platformExtensionsLabel->setText(
        extensionsString(render::compositor::self()->scene()->openGLPlatformInterfaceExtensions()));
    m_ui->openGLExtensionsLabel->setText(extensionsString(openGLExtensions()));
}

template<typename T>
QString keymapComponentToString(xkb_keymap* map,
                                const T& count,
                                std::function<const char*(xkb_keymap*, T)> f)
{
    QString text = QStringLiteral("<ul>");
    for (T i = 0; i < count; i++) {
        text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(f(map, i))));
    }
    text.append(QStringLiteral("</ul>"));
    return text;
}

template<typename T>
QString stateActiveComponents(xkb_state* state,
                              const T& count,
                              std::function<int(xkb_state*, T)> f,
                              std::function<const char*(xkb_keymap*, T)> name)
{
    QString text = QStringLiteral("<ul>");
    xkb_keymap* map = xkb_state_get_keymap(state);
    for (T i = 0; i < count; i++) {
        if (f(state, i) == 1) {
            text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(name(map, i))));
        }
    }
    text.append(QStringLiteral("</ul>"));
    return text;
}

void console::updateKeyboardTab()
{
    auto xkb = kwinApp()->input->redirect->keyboard()->xkb();
    xkb_keymap* map = xkb->keymap();
    xkb_state* state = xkb->state();
    m_ui->layoutsLabel->setText(keymapComponentToString<xkb_layout_index_t>(
        map, xkb_keymap_num_layouts(map), &xkb_keymap_layout_get_name));
    m_ui->currentLayoutLabel->setText(xkb_keymap_layout_get_name(map, xkb->currentLayout()));
    m_ui->modifiersLabel->setText(keymapComponentToString<xkb_mod_index_t>(
        map, xkb_keymap_num_mods(map), &xkb_keymap_mod_get_name));
    m_ui->ledsLabel->setText(keymapComponentToString<xkb_led_index_t>(
        map, xkb_keymap_num_leds(map), &xkb_keymap_led_get_name));
    m_ui->activeLedsLabel->setText(stateActiveComponents<xkb_led_index_t>(
        state, xkb_keymap_num_leds(map), &xkb_state_led_index_is_active, &xkb_keymap_led_get_name));

    using namespace std::placeholders;
    auto modActive = std::bind(xkb_state_mod_index_is_active, _1, _2, XKB_STATE_MODS_EFFECTIVE);
    m_ui->activeModifiersLabel->setText(stateActiveComponents<xkb_mod_index_t>(
        state, xkb_keymap_num_mods(map), modActive, &xkb_keymap_mod_get_name));
}

void console::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // delay the connection to the show event as in ctor the windowHandle returns null
    connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible) {
            // ignore
            return;
        }
        deleteLater();
    });
}

console_delegate::console_delegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

console_delegate::~console_delegate() = default;

QString console_delegate::displayText(const QVariant& value, const QLocale& locale) const
{
    switch (value.type()) {
    case QMetaType::QPoint: {
        const QPoint p = value.toPoint();
        return QStringLiteral("%1,%2").arg(p.x()).arg(p.y());
    }
    case QMetaType::QPointF: {
        const QPointF p = value.toPointF();
        return QStringLiteral("%1,%2").arg(p.x()).arg(p.y());
    }
    case QMetaType::QSize: {
        const QSize s = value.toSize();
        return QStringLiteral("%1x%2").arg(s.width()).arg(s.height());
    }
    case QMetaType::QSizeF: {
        const QSizeF s = value.toSizeF();
        return QStringLiteral("%1x%2").arg(s.width()).arg(s.height());
    }
    case QMetaType::QRect: {
        const QRect r = value.toRect();
        return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    default:
        break;
    };

    if (value.userType() == qMetaTypeId<Wrapland::Server::Surface*>()) {
        if (auto s = value.value<Wrapland::Server::Surface*>()) {
            return QStringLiteral("Wrapland::Server::Surface(0x%1)").arg(qulonglong(s), 0, 16);
        } else {
            return QStringLiteral("nullptr");
        }
    }

    if (value.userType() == qMetaTypeId<Qt::MouseButtons>()) {
        const auto buttons = value.value<Qt::MouseButtons>();
        if (buttons == Qt::NoButton) {
            return i18n("No Mouse Buttons");
        }
        QStringList list;
        if (buttons.testFlag(Qt::LeftButton)) {
            list << i18nc("Mouse Button", "left");
        }
        if (buttons.testFlag(Qt::RightButton)) {
            list << i18nc("Mouse Button", "right");
        }
        if (buttons.testFlag(Qt::MiddleButton)) {
            list << i18nc("Mouse Button", "middle");
        }
        if (buttons.testFlag(Qt::BackButton)) {
            list << i18nc("Mouse Button", "back");
        }
        if (buttons.testFlag(Qt::ForwardButton)) {
            list << i18nc("Mouse Button", "forward");
        }
        if (buttons.testFlag(Qt::ExtraButton1)) {
            list << i18nc("Mouse Button", "extra 1");
        }
        if (buttons.testFlag(Qt::ExtraButton2)) {
            list << i18nc("Mouse Button", "extra 2");
        }
        if (buttons.testFlag(Qt::ExtraButton3)) {
            list << i18nc("Mouse Button", "extra 3");
        }
        if (buttons.testFlag(Qt::ExtraButton4)) {
            list << i18nc("Mouse Button", "extra 4");
        }
        if (buttons.testFlag(Qt::ExtraButton5)) {
            list << i18nc("Mouse Button", "extra 5");
        }
        if (buttons.testFlag(Qt::ExtraButton6)) {
            list << i18nc("Mouse Button", "extra 6");
        }
        if (buttons.testFlag(Qt::ExtraButton7)) {
            list << i18nc("Mouse Button", "extra 7");
        }
        if (buttons.testFlag(Qt::ExtraButton8)) {
            list << i18nc("Mouse Button", "extra 8");
        }
        if (buttons.testFlag(Qt::ExtraButton9)) {
            list << i18nc("Mouse Button", "extra 9");
        }
        if (buttons.testFlag(Qt::ExtraButton10)) {
            list << i18nc("Mouse Button", "extra 10");
        }
        if (buttons.testFlag(Qt::ExtraButton11)) {
            list << i18nc("Mouse Button", "extra 11");
        }
        if (buttons.testFlag(Qt::ExtraButton12)) {
            list << i18nc("Mouse Button", "extra 12");
        }
        if (buttons.testFlag(Qt::ExtraButton13)) {
            list << i18nc("Mouse Button", "extra 13");
        }
        if (buttons.testFlag(Qt::ExtraButton14)) {
            list << i18nc("Mouse Button", "extra 14");
        }
        if (buttons.testFlag(Qt::ExtraButton15)) {
            list << i18nc("Mouse Button", "extra 15");
        }
        if (buttons.testFlag(Qt::ExtraButton16)) {
            list << i18nc("Mouse Button", "extra 16");
        }
        if (buttons.testFlag(Qt::ExtraButton17)) {
            list << i18nc("Mouse Button", "extra 17");
        }
        if (buttons.testFlag(Qt::ExtraButton18)) {
            list << i18nc("Mouse Button", "extra 18");
        }
        if (buttons.testFlag(Qt::ExtraButton19)) {
            list << i18nc("Mouse Button", "extra 19");
        }
        if (buttons.testFlag(Qt::ExtraButton20)) {
            list << i18nc("Mouse Button", "extra 20");
        }
        if (buttons.testFlag(Qt::ExtraButton21)) {
            list << i18nc("Mouse Button", "extra 21");
        }
        if (buttons.testFlag(Qt::ExtraButton22)) {
            list << i18nc("Mouse Button", "extra 22");
        }
        if (buttons.testFlag(Qt::ExtraButton23)) {
            list << i18nc("Mouse Button", "extra 23");
        }
        if (buttons.testFlag(Qt::ExtraButton24)) {
            list << i18nc("Mouse Button", "extra 24");
        }
        if (buttons.testFlag(Qt::TaskButton)) {
            list << i18nc("Mouse Button", "task");
        }
        return list.join(QStringLiteral(", "));
    }

    return QStyledItemDelegate::displayText(value, locale);
}

static const int s_x11ClientId = 1;
static const int s_x11UnmanagedId = 2;
static const int s_waylandClientId = 3;
static const int s_workspaceInternalId = 4;
static const quint32 s_propertyBitMask = 0xFFFF0000;
static const quint32 s_clientBitMask = 0x0000FFFF;
static const quint32 s_idDistance = 10000;

template<class T>
void console_model::add(int parentRow, QVector<T*>& clients, T* client)
{
    beginInsertRows(index(parentRow, 0, QModelIndex()), clients.count(), clients.count());
    clients.append(client);
    endInsertRows();
}

template<class T>
void console_model::remove(int parentRow, QVector<T*>& clients, T* client)
{
    const int remove = clients.indexOf(client);
    if (remove == -1) {
        return;
    }
    beginRemoveRows(index(parentRow, 0, QModelIndex()), remove, remove);
    clients.removeAt(remove);
    endRemoveRows();
}

console_model::console_model(QObject* parent)
    : QAbstractItemModel(parent)
{
    if (waylandServer()) {
        auto const clients = waylandServer()->windows;
        for (auto c : clients) {
            m_shellClients.append(c);
        }
        // TODO: that only includes windows getting shown, not those which are only created
        connect(waylandServer(), &WaylandServer::window_added, this, [this](auto win) {
            add(s_waylandClientId - 1, m_shellClients, win);
        });
        connect(waylandServer(), &WaylandServer::window_removed, this, [this](auto win) {
            remove(s_waylandClientId - 1, m_shellClients, win);
        });
    }
    for (auto const& client : workspace()->allClientList()) {
        auto x11_client = qobject_cast<win::x11::window*>(client);
        if (x11_client) {
            m_x11Clients.append(x11_client);
        }
    }
    connect(workspace(), &Workspace::clientAdded, this, [this](auto c) {
        add(s_x11ClientId - 1, m_x11Clients, c);
    });
    connect(workspace(), &Workspace::clientRemoved, this, [this](Toplevel* window) {
        auto c = qobject_cast<win::x11::window*>(window);
        if (!c) {
            return;
        }
        remove(s_x11ClientId - 1, m_x11Clients, c);
    });

    const auto unmangeds = workspace()->unmanagedList();
    for (auto u : unmangeds) {
        m_unmanageds.append(u);
    }
    connect(workspace(), &Workspace::unmanagedAdded, this, [this](Toplevel* u) {
        add(s_x11UnmanagedId - 1, m_unmanageds, u);
    });
    connect(workspace(), &Workspace::unmanagedRemoved, this, [this](Toplevel* u) {
        remove(s_x11UnmanagedId - 1, m_unmanageds, u);
    });
    for (auto const& window : workspace()->windows()) {
        if (auto internal = qobject_cast<win::InternalClient*>(window)) {
            m_internalClients.append(internal);
        }
    }
    connect(
        workspace(), &Workspace::internalClientAdded, this, [this](win::InternalClient* client) {
            add(s_workspaceInternalId - 1, m_internalClients, client);
        });
    connect(
        workspace(), &Workspace::internalClientRemoved, this, [this](win::InternalClient* client) {
            remove(s_workspaceInternalId - 1, m_internalClients, client);
        });
}

console_model::~console_model() = default;

int console_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 2;
}

int console_model::topLevelRowCount() const
{
    return kwinApp()->shouldUseWaylandForCompositing() ? 4 : 2;
}

template<class T>
int console_model::propertyCount(const QModelIndex& parent,
                                 T* (console_model::*filter)(const QModelIndex&) const) const
{
    if (T* t = (this->*filter)(parent)) {
        return t->metaObject()->propertyCount();
    }
    return 0;
}

int console_model::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return topLevelRowCount();
    }

    switch (parent.internalId()) {
    case s_x11ClientId:
        return m_x11Clients.count();
    case s_x11UnmanagedId:
        return m_unmanageds.count();
    case s_waylandClientId:
        return m_shellClients.count();
    case s_workspaceInternalId:
        return m_internalClients.count();
    default:
        break;
    }

    if (parent.internalId() & s_propertyBitMask) {
        // properties do not have children
        return 0;
    }

    if (parent.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        return propertyCount(parent, &console_model::x11Client);
    } else if (parent.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        return propertyCount(parent, &console_model::unmanaged);
    } else if (parent.internalId() < s_idDistance * (s_waylandClientId + 1)) {
        return propertyCount(parent, &console_model::shellClient);
    } else if (parent.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        return propertyCount(parent, &console_model::internalClient);
    }

    return 0;
}

template<class T>
QModelIndex
console_model::indexForClient(int row, int column, const QVector<T*>& clients, int id) const
{
    if (column != 0) {
        return QModelIndex();
    }
    if (row >= clients.count()) {
        return QModelIndex();
    }
    return createIndex(row, column, s_idDistance * id + row);
}

template<class T>
QModelIndex console_model::indexForProperty(int row,
                                            int column,
                                            const QModelIndex& parent,
                                            T* (console_model::*filter)(const QModelIndex&)
                                                const) const
{
    if (T* t = (this->*filter)(parent)) {
        if (row >= t->metaObject()->propertyCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, quint32(row + 1) << 16 | parent.internalId());
    }
    return QModelIndex();
}

QModelIndex console_model::index(int row, int column, const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        // index for a top level item
        if (column != 0 || row >= topLevelRowCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, row + 1);
    }
    if (column >= 2) {
        // max of 2 columns
        return QModelIndex();
    }
    // index for a client (second level)
    switch (parent.internalId()) {
    case s_x11ClientId:
        return indexForClient(row, column, m_x11Clients, s_x11ClientId);
    case s_x11UnmanagedId:
        return indexForClient(row, column, m_unmanageds, s_x11UnmanagedId);
    case s_waylandClientId:
        return indexForClient(row, column, m_shellClients, s_waylandClientId);
    case s_workspaceInternalId:
        return indexForClient(row, column, m_internalClients, s_workspaceInternalId);
    default:
        break;
    }

    // index for a property (third level)
    if (parent.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        return indexForProperty(row, column, parent, &console_model::x11Client);
    } else if (parent.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        return indexForProperty(row, column, parent, &console_model::unmanaged);
    } else if (parent.internalId() < s_idDistance * (s_waylandClientId + 1)) {
        return indexForProperty(row, column, parent, &console_model::shellClient);
    } else if (parent.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        return indexForProperty(row, column, parent, &console_model::internalClient);
    }

    return QModelIndex();
}

QModelIndex console_model::parent(const QModelIndex& child) const
{
    if (child.internalId() <= s_workspaceInternalId) {
        return QModelIndex();
    }
    if (child.internalId() & s_propertyBitMask) {
        // a property
        const quint32 parentId = child.internalId() & s_clientBitMask;
        if (parentId < s_idDistance * (s_x11ClientId + 1)) {
            return createIndex(parentId - (s_idDistance * s_x11ClientId), 0, parentId);
        } else if (parentId < s_idDistance * (s_x11UnmanagedId + 1)) {
            return createIndex(parentId - (s_idDistance * s_x11UnmanagedId), 0, parentId);
        } else if (parentId < s_idDistance * (s_waylandClientId + 1)) {
            return createIndex(parentId - (s_idDistance * s_waylandClientId), 0, parentId);
        } else if (parentId < s_idDistance * (s_workspaceInternalId + 1)) {
            return createIndex(parentId - (s_idDistance * s_workspaceInternalId), 0, parentId);
        }
        return QModelIndex();
    }
    if (child.internalId() < s_idDistance * (s_x11ClientId + 1)) {
        return createIndex(s_x11ClientId - 1, 0, s_x11ClientId);
    } else if (child.internalId() < s_idDistance * (s_x11UnmanagedId + 1)) {
        return createIndex(s_x11UnmanagedId - 1, 0, s_x11UnmanagedId);
    } else if (child.internalId() < s_idDistance * (s_waylandClientId + 1)) {
        return createIndex(s_waylandClientId - 1, 0, s_waylandClientId);
    } else if (child.internalId() < s_idDistance * (s_workspaceInternalId + 1)) {
        return createIndex(s_workspaceInternalId - 1, 0, s_workspaceInternalId);
    }
    return QModelIndex();
}

QVariant console_model::propertyData(QObject* object, const QModelIndex& index, int role) const
{
    Q_UNUSED(role)
    const auto property = object->metaObject()->property(index.row());
    if (index.column() == 0) {
        return property.name();
    } else {
        const QVariant value = property.read(object);
        if (qstrcmp(property.name(), "windowType") == 0) {
            switch (value.toInt()) {
            case NET::Normal:
                return QStringLiteral("NET::Normal");
            case NET::Desktop:
                return QStringLiteral("NET::Desktop");
            case NET::Dock:
                return QStringLiteral("NET::Dock");
            case NET::Toolbar:
                return QStringLiteral("NET::Toolbar");
            case NET::Menu:
                return QStringLiteral("NET::Menu");
            case NET::Dialog:
                return QStringLiteral("NET::Dialog");
            case NET::Override:
                return QStringLiteral("NET::Override");
            case NET::TopMenu:
                return QStringLiteral("NET::TopMenu");
            case NET::Utility:
                return QStringLiteral("NET::Utility");
            case NET::Splash:
                return QStringLiteral("NET::Splash");
            case NET::DropdownMenu:
                return QStringLiteral("NET::DropdownMenu");
            case NET::PopupMenu:
                return QStringLiteral("NET::PopupMenu");
            case NET::Tooltip:
                return QStringLiteral("NET::Tooltip");
            case NET::Notification:
                return QStringLiteral("NET::Notification");
            case NET::ComboBox:
                return QStringLiteral("NET::ComboBox");
            case NET::DNDIcon:
                return QStringLiteral("NET::DNDIcon");
            case NET::OnScreenDisplay:
                return QStringLiteral("NET::OnScreenDisplay");
            case NET::CriticalNotification:
                return QStringLiteral("NET::CriticalNotification");
            case NET::Unknown:
            default:
                return QStringLiteral("NET::Unknown");
            }
        }
        return value;
    }
    return QVariant();
}

template<class T>
QVariant
console_model::clientData(const QModelIndex& index, int role, const QVector<T*> clients) const
{
    if (index.row() >= clients.count()) {
        return QVariant();
    }
    auto c = clients.at(index.row());
    if (role == Qt::DisplayRole) {
        return QStringLiteral("%1: %2")
            .arg(static_cast<Toplevel*>(c)->xcb_window())
            .arg(win::caption(c));
    } else if (role == Qt::DecorationRole) {
        return c->control->icon();
    }
    return QVariant();
}

QVariant console_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (!index.parent().isValid()) {
        // one of the top levels
        if (index.column() != 0 || role != Qt::DisplayRole) {
            return QVariant();
        }
        switch (index.internalId()) {
        case s_x11ClientId:
            return i18n("X11 Client Windows");
        case s_x11UnmanagedId:
            return i18n("X11 Unmanaged Windows");
        case s_waylandClientId:
            return i18n("Wayland Windows");
        case s_workspaceInternalId:
            return i18n("Internal Windows");
        default:
            return QVariant();
        }
    }
    if (index.internalId() & s_propertyBitMask) {
        if (index.column() >= 2 || role != Qt::DisplayRole) {
            return QVariant();
        }
        if (auto c = shellClient(index)) {
            return propertyData(c, index, role);
        } else if (win::InternalClient* c = internalClient(index)) {
            return propertyData(c, index, role);
        } else if (auto c = x11Client(index)) {
            return propertyData(c, index, role);
        } else if (auto u = unmanaged(index)) {
            return propertyData(u, index, role);
        }
    } else {
        if (index.column() != 0) {
            return QVariant();
        }
        switch (index.parent().internalId()) {
        case s_x11ClientId:
            return clientData(index, role, m_x11Clients);
        case s_x11UnmanagedId: {
            if (index.row() >= m_unmanageds.count()) {
                return QVariant();
            }
            auto u = m_unmanageds.at(index.row());
            if (role == Qt::DisplayRole) {
                return u->xcb_window();
            }
            break;
        }
        case s_waylandClientId:
            return clientData(index, role, m_shellClients);
        case s_workspaceInternalId:
            return clientData(index, role, m_internalClients);
        default:
            break;
        }
    }

    return QVariant();
}

template<class T>
static T* clientForIndex(const QModelIndex& index, const QVector<T*>& clients, int id)
{
    const qint32 row = (index.internalId() & s_clientBitMask) - (s_idDistance * id);
    if (row < 0 || row >= clients.count()) {
        return nullptr;
    }
    return clients.at(row);
}

win::wayland::window* console_model::shellClient(const QModelIndex& index) const
{
    return clientForIndex(index, m_shellClients, s_waylandClientId);
}

win::InternalClient* console_model::internalClient(const QModelIndex& index) const
{
    return clientForIndex(index, m_internalClients, s_workspaceInternalId);
}

win::x11::window* console_model::x11Client(const QModelIndex& index) const
{
    return clientForIndex(index, m_x11Clients, s_x11ClientId);
}

Toplevel* console_model::unmanaged(const QModelIndex& index) const
{
    return clientForIndex(index, m_unmanageds, s_x11UnmanagedId);
}

/////////////////////////////////////// surface_tree_model
surface_tree_model::surface_tree_model(QObject* parent)
    : QAbstractItemModel(parent)
{
    // TODO: it would be nice to not have to reset the model on each change
    auto reset = [this] {
        beginResetModel();
        endResetModel();
    };

    const auto unmangeds = workspace()->unmanagedList();
    for (auto u : unmangeds) {
        if (!u->surface()) {
            continue;
        }
        connect(u->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
    }
    for (auto c : workspace()->allClientList()) {
        if (!c->surface()) {
            continue;
        }
        connect(c->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
    }
    if (waylandServer()) {
        connect(waylandServer(), &WaylandServer::window_added, this, [this, reset](auto win) {
            connect(win->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
            reset();
        });
    }
    connect(workspace(), &Workspace::clientAdded, this, [this, reset](auto c) {
        if (c->surface()) {
            connect(c->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
        }
        reset();
    });
    connect(workspace(), &Workspace::clientRemoved, this, reset);
    connect(workspace(), &Workspace::unmanagedAdded, this, [this, reset](Toplevel* window) {
        if (window->surface()) {
            connect(
                window->surface(), &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
        }
        reset();
    });
    connect(workspace(), &Workspace::unmanagedRemoved, this, reset);
}

surface_tree_model::~surface_tree_model() = default;

int surface_tree_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

int surface_tree_model::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        if (auto surface = static_cast<Wrapland::Server::Surface*>(parent.internalPointer())) {
            const auto& children = surface->state().children;
            return children.size();
        }
        return 0;
    }
    // toplevel are all windows
    return workspace()->allClientList().size() + workspace()->unmanagedList().size();
}

QModelIndex surface_tree_model::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0) {
        // invalid column
        return QModelIndex();
    }

    auto row_u = static_cast<size_t>(row);

    if (parent.isValid()) {
        if (auto surface = static_cast<Wrapland::Server::Surface*>(parent.internalPointer())) {
            const auto& children = surface->state().children;
            if (row_u < children.size()) {
                return createIndex(row_u, column, children.at(row_u)->surface());
            }
        }
        return QModelIndex();
    }
    // a window
    const auto& allClients = workspace()->allClientList();
    if (row_u < allClients.size()) {
        // references a client
        return createIndex(row_u, column, allClients.at(row_u)->surface());
    }
    int reference = allClients.size();
    const auto& unmanaged = workspace()->unmanagedList();
    if (row_u < reference + unmanaged.size()) {
        return createIndex(row_u, column, unmanaged.at(row_u - reference)->surface());
    }
    reference += unmanaged.size();
    // not found
    return QModelIndex();
}

QModelIndex surface_tree_model::parent(const QModelIndex& child) const
{
    if (auto surface = static_cast<Wrapland::Server::Surface*>(child.internalPointer())) {
        const auto& subsurface = surface->subsurface();
        if (!subsurface) {
            // doesn't reference a subsurface, this is a top-level window
            return QModelIndex();
        }
        auto parent = subsurface->parentSurface();
        if (!parent) {
            // something is wrong
            return QModelIndex();
        }
        // is the parent a subsurface itself?
        if (parent->subsurface()) {
            auto grandParent = parent->subsurface()->parentSurface();
            if (!grandParent) {
                // something is wrong
                return QModelIndex();
            }
            const auto& children = grandParent->state().children;
            for (size_t row = 0; row < children.size(); row++) {
                if (children.at(row) == parent->subsurface()) {
                    return createIndex(row, 0, parent);
                }
            }
            return QModelIndex();
        }
        // not a subsurface, thus it's a true window
        size_t row = 0;
        const auto& allClients = workspace()->allClientList();
        for (; row < allClients.size(); row++) {
            if (allClients.at(row)->surface() == parent) {
                return createIndex(row, 0, parent);
            }
        }
        row = allClients.size();
        const auto& unmanaged = workspace()->unmanagedList();
        for (size_t i = 0; i < unmanaged.size(); i++) {
            if (unmanaged.at(i)->surface() == parent) {
                return createIndex(row + i, 0, parent);
            }
        }
        row += unmanaged.size();
    }
    return QModelIndex();
}

QVariant surface_tree_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (auto surface = static_cast<Wrapland::Server::Surface*>(index.internalPointer())) {
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            return QStringLiteral("%1 (%2)")
                .arg(QString::fromStdString(surface->client()->executablePath()))
                .arg(surface->client()->processId());
        } else if (role == Qt::DecorationRole) {
            if (auto buffer = surface->state().buffer) {
                if (buffer->shmBuffer()) {
                    return buffer->shmImage()->createQImage().scaled(QSize(64, 64),
                                                                     Qt::KeepAspectRatio);
                }
            }
        }
    }
    return QVariant();
}

input_device_model::input_device_model(QObject* parent)
    : QAbstractItemModel(parent)
{
    auto& platform = kwinApp()->input->redirect->platform;
    for (auto& dev : platform->dbus->devices) {
        m_devices.push_back(dev);
    }
    for (auto& dev : m_devices) {
        setupDeviceConnections(dev);
    }

    connect(platform->dbus.get(),
            &input::dbus::device_manager::deviceAdded,
            this,
            [this](auto const& sys_name) {
                for (auto& dev : kwinApp()->input->redirect->platform->dbus->devices) {
                    if (dev->sysName() != sys_name) {
                        continue;
                    }
                    beginInsertRows(QModelIndex(), m_devices.count(), m_devices.count());
                    m_devices << dev;
                    setupDeviceConnections(dev);
                    endInsertRows();
                    return;
                }
            });
    connect(platform->dbus.get(),
            &input::dbus::device_manager::deviceRemoved,
            this,
            [this](auto const& sys_name) {
                int index{-1};
                for (int i = 0; i < m_devices.size(); i++) {
                    if (m_devices.at(i)->sysName() == sys_name) {
                        index = i;
                        break;
                    }
                }
                if (index == -1) {
                    return;
                }
                beginRemoveRows(QModelIndex(), index, index);
                m_devices.removeAt(index);
                endRemoveRows();
            });
}

input_device_model::~input_device_model() = default;

int input_device_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 2;
}

QVariant input_device_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (!index.parent().isValid() && index.column() == 0) {
        if (index.row() >= m_devices.count()) {
            return QVariant();
        }
        if (role == Qt::DisplayRole) {
            return m_devices.at(index.row())->name();
        }
    }
    if (index.parent().isValid()) {
        if (role == Qt::DisplayRole) {
            const auto device = m_devices.at(index.parent().row());
            const auto property = device->metaObject()->property(index.row());
            if (index.column() == 0) {
                return property.name();
            } else if (index.column() == 1) {
                return device->property(property.name());
            }
        }
    }
    return QVariant();
}

QModelIndex input_device_model::index(int row, int column, const QModelIndex& parent) const
{
    if (column >= 2) {
        return QModelIndex();
    }
    if (parent.isValid()) {
        if (parent.internalId() & s_propertyBitMask) {
            return QModelIndex();
        }
        if (row >= m_devices.at(parent.row())->metaObject()->propertyCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, quint32(row + 1) << 16 | parent.internalId());
    }
    if (row >= m_devices.count()) {
        return QModelIndex();
    }
    return createIndex(row, column, row + 1);
}

int input_device_model::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return m_devices.count();
    }
    if (parent.internalId() & s_propertyBitMask) {
        return 0;
    }

    return m_devices.at(parent.row())->metaObject()->propertyCount();
}

QModelIndex input_device_model::parent(const QModelIndex& child) const
{
    if (child.internalId() & s_propertyBitMask) {
        const quintptr parentId = child.internalId() & s_clientBitMask;
        return createIndex(parentId - 1, 0, parentId);
    }
    return QModelIndex();
}

void input_device_model::setupDeviceConnections(input::dbus::device* device)
{
    connect(device->dev, &input::control::device::enabled_changed, this, [this, device] {
        const QModelIndex parent = index(m_devices.indexOf(device), 0, QModelIndex());
        const QModelIndex child
            = index(device->metaObject()->indexOfProperty("enabled"), 1, parent);
        emit dataChanged(child, child, QVector<int>{Qt::DisplayRole});
    });
    if (auto& ctrl = device->pointer_ctrl) {
        connect(ctrl, &input::control::pointer::left_handed_changed, this, [this, device] {
            const QModelIndex parent = index(m_devices.indexOf(device), 0, QModelIndex());
            const QModelIndex child
                = index(device->metaObject()->indexOfProperty("leftHanded"), 1, parent);
            emit dataChanged(child, child, QVector<int>{Qt::DisplayRole});
        });
        connect(ctrl, &input::control::pointer::acceleration_changed, this, [this, device] {
            const QModelIndex parent = index(m_devices.indexOf(device), 0, QModelIndex());
            const QModelIndex child
                = index(device->metaObject()->indexOfProperty("pointerAcceleration"), 1, parent);
            emit dataChanged(child, child, QVector<int>{Qt::DisplayRole});
        });
    };
}
}
