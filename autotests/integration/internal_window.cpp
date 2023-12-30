/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KWindowSystem>
#include <QPainter>
#include <QRasterWindow>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Server/surface.h>
#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

using namespace Wrapland::Client;

namespace
{

class HelperWindow : public QRasterWindow
{
    Q_OBJECT
public:
    HelperWindow();
    ~HelperWindow() override;

    QPoint latestGlobalMousePos() const
    {
        return m_latestGlobalMousePos;
    }
    Qt::MouseButtons pressedButtons() const
    {
        return m_pressedButtons;
    }

Q_SIGNALS:
    void entered();
    void left();
    void mouseMoved(const QPoint& global);
    void mousePressed();
    void mouseReleased();
    void wheel();
    void keyPressed();
    void keyReleased();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    QPoint m_latestGlobalMousePos;
    Qt::MouseButtons m_pressedButtons = Qt::MouseButtons();
};

HelperWindow::HelperWindow()
    : QRasterWindow(nullptr)
{
    setFlags(Qt::FramelessWindowHint);
}

HelperWindow::~HelperWindow() = default;

void HelperWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), Qt::red);
}

bool HelperWindow::event(QEvent* event)
{
    if (event->type() == QEvent::Enter) {
        Q_EMIT entered();
    }
    if (event->type() == QEvent::Leave) {
        Q_EMIT left();
    }
    return QRasterWindow::event(event);
}

void HelperWindow::mouseMoveEvent(QMouseEvent* event)
{
    m_latestGlobalMousePos = event->globalPos();
    Q_EMIT mouseMoved(event->globalPos());
}

void HelperWindow::mousePressEvent(QMouseEvent* event)
{
    m_latestGlobalMousePos = event->globalPos();
    m_pressedButtons = event->buttons();
    Q_EMIT mousePressed();
}

void HelperWindow::mouseReleaseEvent(QMouseEvent* event)
{
    m_latestGlobalMousePos = event->globalPos();
    m_pressedButtons = event->buttons();
    Q_EMIT mouseReleased();
}

void HelperWindow::wheelEvent(QWheelEvent* event)
{
    Q_UNUSED(event)
    Q_EMIT wheel();
}

void HelperWindow::keyPressEvent(QKeyEvent* event)
{
    Q_UNUSED(event)
    Q_EMIT keyPressed();
}

void HelperWindow::keyReleaseEvent(QKeyEvent* event)
{
    Q_UNUSED(event)
    Q_EMIT keyReleased();
}

}

namespace KWin::detail::test
{

TEST_CASE("internal window", "[win]")
{
#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("internal-window", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    cursor()->set_pos(QPoint(1280, 512));
    setup_wayland_connection(global_selection::seat);
    QVERIFY(wait_for_wayland_keyboard());

    auto get_internal_window_from_id = [&](uint32_t id) {
        return get_internal_window(setup.base->mod.space->windows_map.at(id));
    };

    SECTION("enter leave")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        QVERIFY(!setup.base->mod.space->findInternal(nullptr));
        QVERIFY(!setup.base->mod.space->findInternal(&win));
        win.setGeometry(0, 0, 100, 100);
        win.show();

        QTRY_COMPARE(clientAddedSpy.count(), 1);
        QVERIFY(!setup.base->mod.space->stacking.active);
        auto c = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(c);
        QVERIFY(c->isInternal());
        QVERIFY(!win::decoration(c));
        QCOMPARE(setup.base->mod.space->findInternal(&win), c);
        QCOMPARE(c->geo.frame, QRect(0, 0, 100, 100));
        QVERIFY(c->isShown());
        QVERIFY(
            contains(win::render_stack(setup.base->mod.space->stacking.order), space::window_t(c)));

        QSignalSpy enterSpy(&win, &HelperWindow::entered);
        QVERIFY(enterSpy.isValid());
        QSignalSpy leaveSpy(&win, &HelperWindow::left);
        QVERIFY(leaveSpy.isValid());
        QSignalSpy moveSpy(&win, &HelperWindow::mouseMoved);
        QVERIFY(moveSpy.isValid());

        quint32 timestamp = 1;
        pointer_motion_absolute(QPoint(50, 50), timestamp++);
        QTRY_COMPARE(moveSpy.count(), 1);

        pointer_motion_absolute(QPoint(60, 50), timestamp++);
        QTRY_COMPARE(moveSpy.count(), 2);
        QCOMPARE(moveSpy[1].first().toPoint(), QPoint(60, 50));

        pointer_motion_absolute(QPoint(101, 50), timestamp++);
        QTRY_COMPARE(leaveSpy.count(), 1);

        // set a mask on the window
        win.setMask(QRegion(10, 20, 30, 40));
        // outside the mask we should not get an enter
        pointer_motion_absolute(QPoint(5, 5), timestamp++);
        QVERIFY(!enterSpy.wait(100));
        QCOMPARE(enterSpy.count(), 1);
        // inside the mask we should still get an enter
        pointer_motion_absolute(QPoint(25, 27), timestamp++);
        QTRY_COMPARE(enterSpy.count(), 2);
    }

    SECTION("pointer press release")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QSignalSpy pressSpy(&win, &HelperWindow::mousePressed);
        QVERIFY(pressSpy.isValid());
        QSignalSpy releaseSpy(&win, &HelperWindow::mouseReleased);
        QVERIFY(releaseSpy.isValid());

        QTRY_COMPARE(clientAddedSpy.count(), 1);

        quint32 timestamp = 1;
        pointer_motion_absolute(QPoint(50, 50), timestamp++);

        pointer_button_pressed(BTN_LEFT, timestamp++);
        QTRY_COMPARE(pressSpy.count(), 1);
        pointer_button_released(BTN_LEFT, timestamp++);
        QTRY_COMPARE(releaseSpy.count(), 1);
    }

    SECTION("pointer axis")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QSignalSpy wheelSpy(&win, &HelperWindow::wheel);
        QVERIFY(wheelSpy.isValid());
        QTRY_COMPARE(clientAddedSpy.count(), 1);

        quint32 timestamp = 1;
        pointer_motion_absolute(QPoint(50, 50), timestamp++);

        pointer_axis_vertical(5.0, timestamp++, 0);
        QTRY_COMPARE(wheelSpy.count(), 1);
        pointer_axis_horizontal(5.0, timestamp++, 0);
        QTRY_COMPARE(wheelSpy.count(), 2);
    }

    SECTION("keyboard")
    {
        auto cursor_pos = GENERATE(
            // on window
            QPoint(50, 50),
            // outside window
            QPoint(250, 250));

        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());

        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();

        QSignalSpy pressSpy(&win, &HelperWindow::keyPressed);
        QVERIFY(pressSpy.isValid());
        QSignalSpy releaseSpy(&win, &HelperWindow::keyReleased);
        QVERIFY(releaseSpy.isValid());
        QTRY_COMPARE(clientAddedSpy.count(), 1);

        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(internalClient->isInternal());
        QVERIFY(internalClient->render_data.ready_for_painting);

        quint32 timestamp = 1;
        pointer_motion_absolute(cursor_pos, timestamp++);

        keyboard_key_pressed(KEY_A, timestamp++);
        QTRY_COMPARE(pressSpy.count(), 1);
        QCOMPARE(releaseSpy.count(), 0);
        keyboard_key_released(KEY_A, timestamp++);
        QTRY_COMPARE(releaseSpy.count(), 1);
        QCOMPARE(pressSpy.count(), 1);
    }

    SECTION("keyboard show without activating")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setProperty("_q_showWithoutActivating", true);
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QSignalSpy pressSpy(&win, &HelperWindow::keyPressed);
        QVERIFY(pressSpy.isValid());
        QSignalSpy releaseSpy(&win, &HelperWindow::keyReleased);
        QVERIFY(releaseSpy.isValid());
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(internalClient->isInternal());
        QVERIFY(internalClient->render_data.ready_for_painting);

        quint32 timestamp = 1;
        const QPoint cursorPos = QPoint(50, 50);
        pointer_motion_absolute(cursorPos, timestamp++);

        keyboard_key_pressed(KEY_A, timestamp++);
        QCOMPARE(pressSpy.count(), 0);
        QVERIFY(!pressSpy.wait(100));
        QCOMPARE(releaseSpy.count(), 0);
        keyboard_key_released(KEY_A, timestamp++);
        QCOMPARE(releaseSpy.count(), 0);
        QVERIFY(!releaseSpy.wait(100));
        QCOMPARE(pressSpy.count(), 0);
    }

    SECTION("keyboard triggers leave")
    {
        // this test verifies that a leave event is sent to a client when an internal window
        // gets a key event
        std::unique_ptr<Keyboard> keyboard(get_client().interfaces.seat->createKeyboard());
        QVERIFY(keyboard);
        QVERIFY(keyboard->isValid());
        QSignalSpy enteredSpy(keyboard.get(), &Keyboard::entered);
        QVERIFY(enteredSpy.isValid());
        QSignalSpy leftSpy(keyboard.get(), &Keyboard::left);
        QVERIFY(leftSpy.isValid());
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));

        // now let's render
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);
        QVERIFY(c->control->active);

        if (enteredSpy.isEmpty()) {
            QVERIFY(enteredSpy.wait());
        }
        QCOMPARE(enteredSpy.count(), 1);

        // create internal window
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QSignalSpy pressSpy(&win, &HelperWindow::keyPressed);
        QVERIFY(pressSpy.isValid());
        QSignalSpy releaseSpy(&win, &HelperWindow::keyReleased);
        QVERIFY(releaseSpy.isValid());
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(internalClient->isInternal());
        QVERIFY(internalClient->render_data.ready_for_painting);

        QVERIFY(leftSpy.isEmpty());
        QVERIFY(!leftSpy.wait(100));

        // now let's trigger a key, which should result in a leave
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_A, timestamp++);
        QVERIFY(leftSpy.wait());
        QCOMPARE(pressSpy.count(), 1);

        keyboard_key_released(KEY_A, timestamp++);
        QTRY_COMPARE(releaseSpy.count(), 1);

        // after hiding the internal window, next key press should trigger an enter
        win.hide();
        keyboard_key_pressed(KEY_A, timestamp++);
        QVERIFY(enteredSpy.wait());
        keyboard_key_released(KEY_A, timestamp++);

        // Destroy the test client.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(c));
    }

    SECTION("touch")
    {
        // touch events for internal windows are emulated through mouse events
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);

        QSignalSpy pressSpy(&win, &HelperWindow::mousePressed);
        QVERIFY(pressSpy.isValid());
        QSignalSpy releaseSpy(&win, &HelperWindow::mouseReleased);
        QVERIFY(releaseSpy.isValid());
        QSignalSpy moveSpy(&win, &HelperWindow::mouseMoved);
        QVERIFY(moveSpy.isValid());

        quint32 timestamp = 1;
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons());
        touch_down(0, QPointF(50, 50), timestamp++);
        QCOMPARE(pressSpy.count(), 1);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(50, 50));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

        // further touch down should not trigger
        touch_down(1, QPointF(75, 75), timestamp++);
        QCOMPARE(pressSpy.count(), 1);
        touch_up(1, timestamp++);
        QCOMPARE(releaseSpy.count(), 0);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(50, 50));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

        // another press
        touch_down(1, QPointF(10, 10), timestamp++);
        QCOMPARE(pressSpy.count(), 1);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(50, 50));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

        // simulate the move
        QCOMPARE(moveSpy.count(), 0);
        touch_motion(0, QPointF(80, 90), timestamp++);
        QCOMPARE(moveSpy.count(), 1);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

        // move on other ID should not do anything
        touch_motion(1, QPointF(20, 30), timestamp++);
        QCOMPARE(moveSpy.count(), 1);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons(Qt::LeftButton));

        // now up our main point
        touch_up(0, timestamp++);
        QCOMPARE(releaseSpy.count(), 1);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons());

        // and up the additional point
        touch_up(1, timestamp++);
        QCOMPARE(releaseSpy.count(), 1);
        QCOMPARE(moveSpy.count(), 1);
        QCOMPARE(win.latestGlobalMousePos(), QPoint(80, 90));
        QCOMPARE(win.pressedButtons(), Qt::MouseButtons());
    }

    SECTION("opacity")
    {
        // this test verifies that opacity is properly synced from QWindow to InternalClient
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setOpacity(0.5);
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(internalClient->isInternal());
        QCOMPARE(internalClient->opacity(), 0.5);

        QSignalSpy opacityChangedSpy(internalClient->qobject.get(),
                                     &win::window_qobject::opacityChanged);
        QVERIFY(opacityChangedSpy.isValid());
        win.setOpacity(0.75);
        QCOMPARE(opacityChangedSpy.count(), 1);
        QCOMPARE(internalClient->opacity(), 0.75);
    }

    SECTION("move")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setOpacity(0.5);
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QCOMPARE(internalClient->geo.frame, QRect(0, 0, 100, 100));

        // normal move should be synced
        win::move(internalClient, QPoint(5, 10));
        QCOMPARE(internalClient->geo.frame, QRect(5, 10, 100, 100));
        QTRY_COMPARE(win.geometry(), QRect(5, 10, 100, 100));
        // another move should also be synced
        win::move(internalClient, QPoint(10, 20));
        QCOMPARE(internalClient->geo.frame, QRect(10, 20, 100, 100));
        QTRY_COMPARE(win.geometry(), QRect(10, 20, 100, 100));

        // now move with a Geometry update blocker
        {
            win::geometry_updates_blocker blocker(internalClient);
            win::move(internalClient, QPoint(5, 10));
            // not synced!
            QCOMPARE(win.geometry(), QRect(10, 20, 100, 100));
        }
        // after destroying the blocker it should be synced
        QTRY_COMPARE(win.geometry(), QRect(5, 10, 100, 100));
    }

    SECTION("skip close animation")
    {
        auto initial_set = GENERATE(true, false);

        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());

        HelperWindow win;
        win.setOpacity(0.5);
        win.setGeometry(0, 0, 100, 100);
        win.setProperty("KWIN_SKIP_CLOSE_ANIMATION", initial_set);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);

        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QCOMPARE(internalClient->skip_close_animation, initial_set);

        QSignalSpy skipCloseChangedSpy(internalClient->qobject.get(),
                                       &win::window_qobject::skipCloseAnimationChanged);
        QVERIFY(skipCloseChangedSpy.isValid());

        win.setProperty("KWIN_SKIP_CLOSE_ANIMATION", !initial_set);
        QCOMPARE(skipCloseChangedSpy.count(), 1);
        QCOMPARE(internalClient->skip_close_animation, !initial_set);
        win.setProperty("KWIN_SKIP_CLOSE_ANIMATION", initial_set);
        QCOMPARE(skipCloseChangedSpy.count(), 2);
        QCOMPARE(internalClient->skip_close_animation, initial_set);
    }

    SECTION("modifier click unrestricted move")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.setFlags(win.flags() & ~Qt::FramelessWindowHint);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(win::decoration(internalClient));

        auto group = setup.base->config.main->group(QStringLiteral("MouseBindings"));
        group.writeEntry("CommandAllKey", "Meta");
        group.writeEntry("CommandAll1", "Move");
        group.writeEntry("CommandAll2", "Move");
        group.writeEntry("CommandAll3", "Move");
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);
        QCOMPARE(setup.base->mod.space->options->qobject->commandAllModifier(), Qt::MetaModifier);
        QCOMPARE(setup.base->mod.space->options->qobject->commandAll1(),
                 win::mouse_cmd::unrestricted_move);
        QCOMPARE(setup.base->mod.space->options->qobject->commandAll2(),
                 win::mouse_cmd::unrestricted_move);
        QCOMPARE(setup.base->mod.space->options->qobject->commandAll3(),
                 win::mouse_cmd::unrestricted_move);

        // move cursor on window
        cursor()->set_pos(internalClient->geo.frame.center());

        // simulate modifier+click
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        QVERIFY(!win::is_move(internalClient));
        pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(win::is_move(internalClient));
        // release modifier should not change it
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
        QVERIFY(win::is_move(internalClient));
        // but releasing the key should end move/resize
        pointer_button_released(BTN_LEFT, timestamp++);
        QVERIFY(!win::is_move(internalClient));
    }

    SECTION("modifier scroll")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.setFlags(win.flags() & ~Qt::FramelessWindowHint);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(win::decoration(internalClient));

        auto group = setup.base->config.main->group(QStringLiteral("MouseBindings"));
        group.writeEntry("CommandAllKey", "Meta");
        group.writeEntry("CommandAllWheel", "change opacity");
        group.sync();
        win::space_reconfigure(*setup.base->mod.space);

        // move cursor on window
        cursor()->set_pos(internalClient->geo.frame.center());

        // set the opacity to 0.5
        internalClient->setOpacity(0.5);
        QCOMPARE(internalClient->opacity(), 0.5);
        quint32 timestamp = 1;
        keyboard_key_pressed(KEY_LEFTMETA, timestamp++);
        pointer_axis_vertical(-5, timestamp++, 0);
        QCOMPARE(internalClient->opacity(), 0.6);
        pointer_axis_vertical(5, timestamp++, 0);
        QCOMPARE(internalClient->opacity(), 0.5);
        keyboard_key_released(KEY_LEFTMETA, timestamp++);
    }

    SECTION("popup")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.setFlags(win.flags() | Qt::Popup);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QCOMPARE(win::is_popup(internalClient), true);
    }

    SECTION("scale")
    {
        setup.set_outputs(
            {output(QRect(0, 0, 1280, 1024), 2), output(QRect(1280 / 2, 0, 1280, 1024), 2)});

        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.setFlags(win.flags() | Qt::Popup);
        win.show();
        QCOMPARE(win.devicePixelRatio(), 2.0);
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QCOMPARE(internalClient->bufferScale(), 2);
    }

    SECTION("effect window")
    {
        QSignalSpy clientAddedSpy(setup.base->mod.space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        HelperWindow win;
        win.setGeometry(0, 0, 100, 100);
        win.show();
        QTRY_COMPARE(clientAddedSpy.count(), 1);
        auto internalClient
            = get_internal_window_from_id(clientAddedSpy.first().first().value<quint32>());
        QVERIFY(internalClient);
        QVERIFY(internalClient->render);
        QVERIFY(internalClient->render->effect);
        QCOMPARE(internalClient->render->effect->internalWindow(), &win);

        QCOMPARE(effects->findWindow(&win), internalClient->render->effect.get());
        QCOMPARE(effects->findWindow(&win)->internalWindow(), &win);
    }
}

}

#include "internal_window.moc"
