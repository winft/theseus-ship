/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "input_filter.h"

#include "input/event.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/platform.h"
#include "input/pointer.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/switch.h"
#include "input/xkb/keyboard.h"
#include "main.h"

#include <KLocalizedString>
#include <QMetaEnum>
#include <QTextEdit>

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
    assert(dev);

    if (!dev->control) {
        return tableRow(i18n("Input Device"),
                        i18nc("The input device of the event is not known", "Unknown"));
    }

    return tableRow(i18n("Input Device"),
                    QStringLiteral("%1 (%2)")
                        .arg(dev->control->metadata.name.c_str())
                        .arg(dev->control->metadata.sys_name.c_str()));
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

input_filter::input_filter(input::redirect& redirect, QTextEdit* textEdit)
    : input::event_spy(redirect)
    , m_textEdit(textEdit)
{
    m_textEdit->document()->setMaximumBlockCount(1000);
}

static const QString s_hr = QStringLiteral("<hr/>");
static const QString s_tableStart = QStringLiteral("<table>");
static const QString s_tableEnd = QStringLiteral("</table>");

void input_filter::button(input::button_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);
    text.append(s_tableStart);

    auto qt_button = buttonToString(input::button_to_qt_mouse_button(event.key));
    auto buttons = buttonsToString(redirect.get_pointer()->buttons());
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

void input_filter::motion(input::motion_event const& event)
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

    auto pos = redirect.globalPointer();
    text.append(tableRow(i18nc("The global mouse pointer position", "Global Position"),
                         QStringLiteral("%1/%2").arg(pos.x()).arg(pos.y())));

    text.append(s_tableEnd);
    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::axis(input::axis_event const& event)
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

    auto const xkb = event.base.dev->xkb.get();
    auto const key_meta_object = Qt::qt_getEnumMetaObject(Qt::Key());
    auto const enumerator = key_meta_object->enumerator(key_meta_object->indexOfEnumerator("Key"));
    text.append(tableRow(i18nc("Key according to Qt", "Qt::Key code"),
                         enumerator.valueToKey(input::key_to_qt_key(event.keycode, xkb))));

    auto const keysym = xkb->to_keysym(event.keycode);
    text.append(tableRow(i18nc("The translated code to an Xkb symbol", "Xkb symbol"), keysym));
    text.append(tableRow(i18nc("The translated code interpreted as text", "Utf8"),
                         QString::fromStdString(xkb->to_string(keysym))));

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
                         to_string(xkb->qt_modifiers)));
    text.append(s_tableEnd);
}

void input_filter::key(input::key_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    switch (event.state) {
    case input::key_state::pressed:
        text.append(tableHeaderRow(i18nc("A key press event", "Key Press")));
        break;
    case input::key_state::released:
        text.append(tableHeaderRow(i18nc("A key release event", "Key Release")));
        break;
    }

    text.append(deviceRow(event.base.dev));
    add_common_key_data(event, text);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::key_repeat(input::key_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A key repeat event", "Key repeat")));

    text.append(deviceRow(event.base.dev));
    add_common_key_data(event, text);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::touch_down(input::touch_down_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A touch down event", "Touch down")));
    text.append(timestampRow(event.base.time_msec));

    text.append(tableRow(i18nc("The id of the touch point in the touch event", "Point identifier"),
                         event.id));
    text.append(tableRow(i18nc("The global position of the touch point", "Global position"),
                         QStringLiteral("%1/%2").arg(event.pos.x()).arg(event.pos.y())));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::touch_motion(input::touch_motion_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A touch motion event", "Touch Motion")));
    text.append(timestampRow(event.base.time_msec));

    text.append(tableRow(i18nc("The id of the touch point in the touch event", "Point identifier"),
                         event.id));
    text.append(tableRow(i18nc("The global position of the touch point", "Global position"),
                         QStringLiteral("%1/%2").arg(event.pos.x()).arg(event.pos.y())));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::touch_up(input::touch_up_event const& event)
{
    QString text = s_hr;
    text.append(s_tableStart);

    text.append(tableHeaderRow(i18nc("A touch up event", "Touch Up")));
    text.append(timestampRow(event.base.time_msec));

    text.append(tableRow(i18nc("The id of the touch point in the touch event", "Point identifier"),
                         event.id));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::pinch_begin(input::pinch_begin_event const& event)
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

void input_filter::pinch_update(input::pinch_update_event const& event)
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

void input_filter::pinch_end(input::pinch_end_event const& event)
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

void input_filter::swipe_begin(input::swipe_begin_event const& event)
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

void input_filter::swipe_update(input::swipe_update_event const& event)
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

void input_filter::swipe_end(input::swipe_end_event const& event)
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

void input_filter::switch_toggle(input::switch_toggle_event const& event)
{
    auto text = s_hr;
    auto const timestamp = timestampRow(event.base.time_msec);

    text.append(s_tableStart);
    text.append(timestamp);

    text.append(deviceRow(event.base.dev));

    QString switch_name;
    switch (event.type) {
    case input::switch_type::lid:
        switch_name = i18nc("Name of a hardware switch", "Notebook lid");
        break;
    case input::switch_type::tablet_mode:
        switch_name = i18nc("Name of a hardware switch", "Tablet mode");
    default:
        break;
    }
    text.append(tableRow(i18nc("A hardware switch", "Switch"), switch_name));

    QString switch_state;
    switch (event.state) {
    case input::switch_state::off:
        switch_state = i18nc("The hardware switch got turned off", "Off");
        break;
    case input::switch_state::on:
        switch_state = i18nc("The hardware switch got turned on", "On");
        break;
    case input::switch_state::toggle:
        switch_state = i18nc("A hardware switch (e.g. notebook lid) got toggled", "Switch toggled");
        break;
    default:
        Q_UNREACHABLE();
    }

    text.append(tableRow(i18nc("State of a hardware switch (on/off)", "State"), switch_state));
    text.append(s_tableEnd);

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::tabletToolEvent(QTabletEvent* event)
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

void input_filter::tabletToolButtonEvent(const QSet<uint>& pressedButtons)
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

void input_filter::tabletPadButtonEvent(const QSet<uint>& pressedButtons)
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

void input_filter::tabletPadStripEvent(int number, int position, bool isFinger)
{
    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Pad Strip"))
        + tableRow(i18n("Number"), number) + tableRow(i18n("Position"), position)
        + tableRow(i18n("isFinger"), isFinger) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

void input_filter::tabletPadRingEvent(int number, int position, bool isFinger)
{
    QString text = s_hr + s_tableStart + tableHeaderRow(i18n("Tablet Pad Ring"))
        + tableRow(i18n("Number"), number) + tableRow(i18n("Position"), position)
        + tableRow(i18n("isFinger"), isFinger) + s_tableEnd;

    m_textEdit->insertHtml(text);
    m_textEdit->ensureCursorVisible();
}

}
