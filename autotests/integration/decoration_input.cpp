/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "input/pointer_redirect.h"
#include "win/deco.h"
#include "win/deco/bridge.h"
#include "win/deco/client_impl.h"
#include "win/deco/settings.h"
#include "win/internal_window.h"
#include "win/move.h"
#include "win/screen_edges.h"
#include "win/space_reconfigure.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

Q_DECLARE_METATYPE(Qt::WindowFrameSection)

namespace KWin::detail::test
{

#define MOTION(target) pointer_motion_absolute(target, timestamp++)

#define PRESS pointer_button_pressed(BTN_LEFT, timestamp++)

#define RELEASE pointer_button_released(BTN_LEFT, timestamp++)

class EventHelper : public QObject
{
    Q_OBJECT
public:
    EventHelper()
        : QObject()
    {
    }
    ~EventHelper() override = default;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched)
        if (event->type() == QEvent::HoverMove) {
            Q_EMIT hoverMove();
        } else if (event->type() == QEvent::HoverLeave) {
            Q_EMIT hoverLeave();
        }
        return false;
    }

Q_SIGNALS:
    void hoverMove();
    void hoverLeave();
};

TEST_CASE("decoration input", "[input],[win]")
{
#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif
    test::setup setup("decoration-input", operation_mode);

    struct {
        std::unique_ptr<Wrapland::Client::XdgShellToplevel> toplevel;
        std::unique_ptr<Wrapland::Client::Surface> surface;
    } client;

    auto showWindow = [&]() -> space::wayland_window* {
        using namespace Wrapland::Client;

#define VERIFY(statement)                                                                          \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))                          \
        return nullptr;
#define COMPARE(actual, expected)                                                                  \
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))                \
        return nullptr;

        client.surface = create_surface();
        VERIFY(client.surface.get());
        client.toplevel = create_xdg_shell_toplevel(client.surface, CreationSetup::CreateOnly);
        VERIFY(client.toplevel.get());

        QSignalSpy configureRequestedSpy(client.toplevel.get(), &XdgShellToplevel::configured);

        auto deco = get_client().interfaces.xdg_decoration->getToplevelDecoration(
            client.toplevel.get(), client.toplevel.get());
        QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
        VERIFY(decoSpy.isValid());
        deco->setMode(XdgDecoration::Mode::ServerSide);
        COMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
        init_xdg_shell_toplevel(client.surface, client.toplevel);
        COMPARE(decoSpy.count(), 1);
        COMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

        VERIFY(configureRequestedSpy.count() > 0 || configureRequestedSpy.wait());
        COMPARE(configureRequestedSpy.count(), 1);

        client.toplevel->ackConfigure(configureRequestedSpy.back().front().toInt());

        // let's render
        auto c = render_and_wait_for_shown(client.surface, QSize(500, 50), Qt::blue);
        VERIFY(c);
        COMPARE(get_wayland_window(setup.base->space->stacking.active), c);
        COMPARE(c->userCanSetNoBorder(), true);
        COMPARE(win::decoration(c) != nullptr, true);

#undef VERIFY
#undef COMPARE

        return c;
    };

    // change some options
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group(QStringLiteral("MouseBindings"))
        .writeEntry("CommandTitlebarWheel", QStringLiteral("above/below"));
    config->group(QStringLiteral("Windows"))
        .writeEntry("TitlebarDoubleClickCommand", QStringLiteral("OnAllDesktops"));
    config->group(QStringLiteral("Desktops")).writeEntry("Number", 2);
    config->sync();

    setup.base->config.main = config;

    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::seat | global_selection::xdg_decoration);
    QVERIFY(wait_for_wayland_pointer());
    cursor()->set_pos(QPoint(640, 512));

    SECTION("axis")
    {
        struct data {
            QPoint deco_point;
            Qt::WindowFrameSection expected_section;
        };
        auto test_data = GENERATE(data{{0, 0}, Qt::TopLeftSection},
                                  data{{250, 0}, Qt::TopSection},
                                  data{{499, 0}, Qt::TopRightSection});

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        QVERIFY(!c->control->keep_above);
        QVERIFY(!c->control->keep_below);

        quint32 timestamp = 1;

        MOTION(QPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));

        QVERIFY(setup.base->space->input->pointer->focus.deco.client);
        QCOMPARE(
            setup.base->space->input->pointer->focus.deco.client->decoration()->sectionUnderMouse(),
            Qt::TitleBarArea);

        // TODO: mouse wheel direction looks wrong to me
        // simulate wheel
        pointer_axis_vertical(5.0, timestamp++, 0);
        QVERIFY(c->control->keep_below);
        QVERIFY(!c->control->keep_above);
        pointer_axis_vertical(-5.0, timestamp++, 0);
        QVERIFY(!c->control->keep_below);
        QVERIFY(!c->control->keep_above);
        pointer_axis_vertical(-5.0, timestamp++, 0);
        QVERIFY(!c->control->keep_below);
        QVERIFY(c->control->keep_above);

        // test top most deco pixel, BUG: 362860
        win::move(c, QPoint(0, 0));
        MOTION(test_data.deco_point);
        QVERIFY(setup.base->space->input->pointer->focus.deco.client);
        QVERIFY(setup.base->space->input->pointer->focus.deco.window);
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), c);
        REQUIRE(
            setup.base->space->input->pointer->focus.deco.client->decoration()->sectionUnderMouse()
            == test_data.expected_section);
        pointer_axis_vertical(5.0, timestamp++, 0);
        QVERIFY(!c->control->keep_below);

        // Button at (0,0;24x24) filters out the event.
        REQUIRE(c->control->keep_above == (test_data.expected_section == Qt::TopLeftSection));
    }

    SECTION("double click")
    {
        struct data {
            QPoint deco_point;
            Qt::WindowFrameSection expected_section;
        };
        auto test_data = GENERATE(data{{0, 0}, Qt::TopLeftSection},
                                  data{{250, 0}, Qt::TopSection},
                                  data{{499, 0}, Qt::TopRightSection});

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        QVERIFY(!win::on_all_subspaces(*c));
        quint32 timestamp = 1;
        MOTION(QPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));

        // double click
        PRESS;
        RELEASE;
        PRESS;
        RELEASE;
        QVERIFY(win::on_all_subspaces(*c));
        // double click again
        PRESS;
        RELEASE;
        QVERIFY(win::on_all_subspaces(*c));
        PRESS;
        RELEASE;
        QVERIFY(!win::on_all_subspaces(*c));

        // test top most deco pixel, BUG: 362860
        win::move(c, QPoint(0, 0));
        MOTION(test_data.deco_point);
        QVERIFY(setup.base->space->input->pointer->focus.deco.client);
        QVERIFY(setup.base->space->input->pointer->focus.deco.window);
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), c);
        REQUIRE(
            setup.base->space->input->pointer->focus.deco.client->decoration()->sectionUnderMouse()
            == test_data.expected_section);
        // double click
        PRESS;
        RELEASE;
        QVERIFY(!win::on_all_subspaces(*c));
        PRESS;
        RELEASE;
        QVERIFY(win::on_all_subspaces(*c));
    }

    SECTION("double tap")
    {
        struct data {
            QPoint deco_point;
            Qt::WindowFrameSection expected_section;
        };
        auto test_data = GENERATE(data{{10, 10}, Qt::TopLeftSection},
                                  data{{260, 10}, Qt::TopSection},
                                  data{{509, 10}, Qt::TopRightSection});

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        QVERIFY(!win::on_all_subspaces(*c));
        quint32 timestamp = 1;
        const QPoint tapPoint(c->geo.frame.center().x(),
                              win::frame_to_client_pos(c, QPoint()).y() / 2);

        // double tap
        touch_down(0, tapPoint, timestamp++);
        touch_up(0, timestamp++);
        touch_down(0, tapPoint, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(win::on_all_subspaces(*c));
        // double tap again
        touch_down(0, tapPoint, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(win::on_all_subspaces(*c));
        touch_down(0, tapPoint, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(!win::on_all_subspaces(*c));

        // test top most deco pixel, BUG: 362860
        //
        // Not directly at (0, 0), otherwise ScreenEdgeInputFilter catches
        // event before DecorationEventFilter.
        win::move(c, QPoint(10, 10));

        // double click
        touch_down(0, test_data.deco_point, timestamp++);
        QVERIFY(setup.base->space->input->touch->focus.deco.client);
        QVERIFY(setup.base->space->input->touch->focus.deco.window);
        QCOMPARE(get_wayland_window(setup.base->space->input->touch->focus.window), c);
        REQUIRE(
            setup.base->space->input->touch->focus.deco.client->decoration()->sectionUnderMouse()
            == test_data.expected_section);
        touch_up(0, timestamp++);
        QVERIFY(!win::on_all_subspaces(*c));
        touch_down(0, test_data.deco_point, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(win::on_all_subspaces(*c));
    }

    SECTION("hover")
    {
        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());

        // our left border is moved out of the visible area, so move the window to a better place
        win::move(c, QPoint(20, 0));

        quint32 timestamp = 1;
        MOTION(QPoint(c->geo.frame.center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));
        QCOMPARE(c->control->move_resize.cursor, win::cursor_shape(Qt::ArrowCursor));

        // There is a mismatch of the cursor key positions between windows
        // with and without borders (with borders one can move inside a bit and still
        // be on an edge, without not). We should make this consistent in KWin's core.
        //
        // TODO: Test input position with different border sizes.
        // TODO: We should test with the fake decoration to have a fixed test environment.
        auto const hasBorders
            = setup.base->space->deco->settings()->borderSize() != KDecoration2::BorderSize::None;
        auto deviation = [hasBorders] { return hasBorders ? -1 : 0; };

        MOTION(QPoint(c->geo.frame.x(), 0));
        QCOMPARE(c->control->move_resize.cursor,
                 win::cursor_shape(win::extended_cursor::SizeNorthWest));
        MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() / 2, 0));
        QCOMPARE(c->control->move_resize.cursor,
                 win::cursor_shape(win::extended_cursor::SizeNorth));
        MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() - 1, 0));
        QCOMPARE(c->control->move_resize.cursor,
                 win::cursor_shape(win::extended_cursor::SizeNorthEast));
        MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() + deviation(),
                      c->geo.size().height() / 2));
        QCOMPARE(c->control->move_resize.cursor, win::cursor_shape(win::extended_cursor::SizeEast));
        MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() + deviation(),
                      c->geo.size().height() - 1));
        QCOMPARE(c->control->move_resize.cursor,
                 win::cursor_shape(win::extended_cursor::SizeSouthEast));
        MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() / 2,
                      c->geo.size().height() + deviation()));
        QCOMPARE(c->control->move_resize.cursor,
                 win::cursor_shape(win::extended_cursor::SizeSouth));
        MOTION(QPoint(c->geo.frame.x(), c->geo.size().height() + deviation()));
        QCOMPARE(c->control->move_resize.cursor,
                 win::cursor_shape(win::extended_cursor::SizeSouthWest));
        MOTION(QPoint(c->geo.frame.x() - 1, c->geo.size().height() / 2));
        QCOMPARE(c->control->move_resize.cursor, win::cursor_shape(win::extended_cursor::SizeWest));

        MOTION(c->geo.frame.center());

        // Cursor not set back on leave
        REQUIRE_FALSE(c->control->move_resize.cursor == win::cursor_shape(Qt::ArrowCursor));
    }

    SECTION("press to move")
    {
        struct data {
            std::string desc;
            QPoint offset;
            QPoint offset2;
            QPoint offset3;
        };
        auto test_data = GENERATE(data{"to right", {10, 0}, {20, 0}, {30, 0}},
                                  data{"to left", {-10, 0}, {-20, 0}, {-30, 0}},
                                  data{"to bottom", {0, 10}, {0, 20}, {0, 30}},
                                  data{"to bottom", {0, -10}, {0, -20}, {0, -30}});

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        win::move(c,
                  get_output(0)->geometry().center()
                      - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
        QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                       &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(startMoveResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            c->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        quint32 timestamp = 1;
        MOTION(QPoint(c->geo.frame.center().x(),
                      c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));
        QCOMPARE(c->control->move_resize.cursor, win::cursor_shape(Qt::ArrowCursor));

        PRESS;
        QVERIFY(!win::is_move(c));
        MOTION(QPoint(c->geo.frame.center().x(),
                      c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
               + test_data.offset);
        auto const oldPos = c->geo.pos();
        QVERIFY(win::is_move(c));
        QCOMPARE(startMoveResizedSpy.count(), 1);

        RELEASE;
        QTRY_VERIFY(!win::is_move(c));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);

        // Just trigger move doesn't move the window.
        REQUIRE(c->geo.pos() != oldPos + test_data.offset);

        // again
        PRESS;
        QVERIFY(!win::is_move(c));
        MOTION(QPoint(c->geo.frame.center().x(),
                      c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
               + test_data.offset2);
        QVERIFY(win::is_move(c));
        QCOMPARE(startMoveResizedSpy.count(), 2);
        MOTION(QPoint(c->geo.frame.center().x(),
                      c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
               + test_data.offset3);

        RELEASE;
        QTRY_VERIFY(!win::is_move(c));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 2);

        // TODO: the offset should also be included
        QCOMPARE(c->geo.pos(), oldPos + test_data.offset2 + test_data.offset3);
    }

    SECTION("tap to move")
    {
        struct data {
            std::string desc;
            QPoint offset;
            QPoint offset2;
            QPoint offset3;
        };
        auto test_data = GENERATE(data{"to right", {10, 0}, {20, 0}, {30, 0}},
                                  data{"to left", {-10, 0}, {-20, 0}, {-30, 0}},
                                  data{"to bottom", {0, 10}, {0, 20}, {0, 30}},
                                  data{"to bottom", {0, -10}, {0, -20}, {0, -30}});

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        win::move(c,
                  get_output(0)->geometry().center()
                      - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
        QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                       &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(startMoveResizedSpy.isValid());
        QSignalSpy clientFinishUserMovedResizedSpy(
            c->qobject.get(), &win::window_qobject::clientFinishUserMovedResized);
        QVERIFY(clientFinishUserMovedResizedSpy.isValid());

        quint32 timestamp = 1;
        QPoint p = QPoint(c->geo.frame.center().x(),
                          c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2);

        touch_down(0, p, timestamp++);
        QVERIFY(!win::is_move(c));
        QCOMPARE(setup.base->space->input->touch->decorationPressId(), 0);
        touch_motion(0, p + test_data.offset, timestamp++);
        const QPoint oldPos = c->geo.pos();
        QVERIFY(win::is_move(c));
        QCOMPARE(startMoveResizedSpy.count(), 1);

        touch_up(0, timestamp++);
        QTRY_VERIFY(!win::is_move(c));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);

        // Just trigger move doesn't move the window.
        REQUIRE(c->geo.pos() != oldPos + test_data.offset);

        // again
        touch_down(1, p + test_data.offset, timestamp++);
        QCOMPARE(setup.base->space->input->touch->decorationPressId(), 1);
        QVERIFY(!win::is_move(c));
        touch_motion(1,
                     QPoint(c->geo.frame.center().x(),
                            c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
                         + test_data.offset2,
                     timestamp++);
        QVERIFY(win::is_move(c));
        QCOMPARE(startMoveResizedSpy.count(), 2);
        touch_motion(1,
                     QPoint(c->geo.frame.center().x(),
                            c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2)
                         + test_data.offset3,
                     timestamp++);

        touch_up(1, timestamp++);
        QTRY_VERIFY(!win::is_move(c));
        QCOMPARE(clientFinishUserMovedResizedSpy.count(), 2);

        // TODO: the offset should also be included
        QCOMPARE(c->geo.pos(), oldPos + test_data.offset2 + test_data.offset3);
    }

    SECTION("resize outside window")
    {
        struct data {
            Qt::Edge edge;
            Qt::CursorShape expected_cursor;
        };
        auto test_data = GENERATE(data{Qt::LeftEdge, Qt::SizeHorCursor},
                                  data{Qt::RightEdge, Qt::SizeHorCursor},
                                  data{Qt::BottomEdge, Qt::SizeVerCursor});

        // Verifies that one can resize the window outside the decoration with NoSideBorder.

        // first adjust config
        setup.base->config.main->group("org.kde.kdecoration2")
            .writeEntry("BorderSize", QStringLiteral("None"));
        setup.base->config.main->sync();
        win::space_reconfigure(*setup.base->space);

        // now create window
        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        win::move(c,
                  get_output(0)->geometry().center()
                      - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));
        QVERIFY(c->geo.frame != win::input_geometry(c));
        QVERIFY(win::input_geometry(c).contains(c->geo.frame));
        QSignalSpy startMoveResizedSpy(c->qobject.get(),
                                       &win::window_qobject::clientStartUserMovedResized);
        QVERIFY(startMoveResizedSpy.isValid());

        // go to border
        quint32 timestamp = 1;
        switch (test_data.edge) {
        case Qt::LeftEdge:
            MOTION(QPoint(c->geo.frame.x() - 1, c->geo.frame.center().y()));
            break;
        case Qt::RightEdge:
            MOTION(QPoint(c->geo.frame.x() + c->geo.frame.width() + 1, c->geo.frame.center().y()));
            break;
        case Qt::BottomEdge:
            MOTION(QPoint(c->geo.frame.center().x(), c->geo.frame.y() + c->geo.frame.height() + 1));
            break;
        default:
            break;
        }

        QVERIFY(!c->geo.frame.contains(cursor()->pos()));

        // pressing should trigger resize
        PRESS;
        QVERIFY(!win::is_resize(c));
        QVERIFY(startMoveResizedSpy.wait());
        QVERIFY(win::is_resize(c));

        RELEASE;
        QVERIFY(!win::is_resize(c));
    }

    SECTION("modifier click unrestricted move")
    {
        // Ensures that Alt+mouse button press triggers unrestricted move.

        struct mod_data {
            int key;
            QString key_name;
        };

        auto modifier = GENERATE(mod_data{KEY_LEFTALT, QStringLiteral("Alt")},
                                 mod_data{KEY_RIGHTALT, QStringLiteral("Alt")},
                                 mod_data{KEY_LEFTMETA, QStringLiteral("Meta")},
                                 mod_data{KEY_RIGHTMETA, QStringLiteral("Meta")});
        auto mouse_button = GENERATE(BTN_LEFT, BTN_RIGHT, BTN_MIDDLE);
        auto caps_lock = GENERATE(true, false);

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", modifier.key_name);
        group.writeEntry("CommandAll1", "Move");
        group.writeEntry("CommandAll2", "Move");
        group.writeEntry("CommandAll3", "Move");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        REQUIRE(
            setup.base->space->options->qobject->commandAllModifier()
            == (modifier.key_name == QStringLiteral("Alt") ? Qt::AltModifier : Qt::MetaModifier));
        QCOMPARE(setup.base->space->options->qobject->commandAll1(),
                 win::mouse_cmd::unrestricted_move);
        QCOMPARE(setup.base->space->options->qobject->commandAll2(),
                 win::mouse_cmd::unrestricted_move);
        QCOMPARE(setup.base->space->options->qobject->commandAll3(),
                 win::mouse_cmd::unrestricted_move);

        // create a window
        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        win::move(c,
                  get_output(0)->geometry().center()
                      - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));

        // move cursor on window
        cursor()->set_pos(QPoint(c->geo.frame.center().x(),
                                 c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));

        // simulate modifier+click
        quint32 timestamp = 1;
        if (caps_lock) {
            keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        }

        keyboard_key_pressed(modifier.key, timestamp++);
        QVERIFY(!win::is_move(c));
        pointer_button_pressed(mouse_button, timestamp++);
        QVERIFY(win::is_move(c));

        // release modifier should not change it
        keyboard_key_released(modifier.key, timestamp++);
        QVERIFY(win::is_move(c));

        // but releasing the key should end move/resize
        pointer_button_released(mouse_button, timestamp++);
        QVERIFY(!win::is_move(c));

        if (caps_lock) {
            keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        }
    }

    SECTION("modifier scroll opacity")
    {
        struct mod_data {
            int key;
            QString key_name;
        };

        auto modifier = GENERATE(mod_data{KEY_LEFTALT, QStringLiteral("Alt")},
                                 mod_data{KEY_RIGHTALT, QStringLiteral("Alt")},
                                 mod_data{KEY_LEFTMETA, QStringLiteral("Meta")},
                                 mod_data{KEY_RIGHTMETA, QStringLiteral("Meta")});
        auto caps_lock = GENERATE(true, false);

        // first modify the config for this run
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", modifier.key_name);
        group.writeEntry("CommandAllWheel", "change opacity");
        group.sync();
        win::space_reconfigure(*setup.base->space);

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        win::move(c,
                  get_output(0)->geometry().center()
                      - QPoint(c->geo.size().width() / 2, c->geo.size().height() / 2));

        // move cursor on window
        cursor()->set_pos(QPoint(c->geo.frame.center().x(),
                                 c->geo.pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));

        // set the opacity to 0.5
        c->setOpacity(0.5);
        QCOMPARE(c->opacity(), 0.5);

        // simulate modifier+wheel
        quint32 timestamp = 1;
        if (caps_lock) {
            keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        }

        keyboard_key_pressed(modifier.key, timestamp++);
        pointer_axis_vertical(-5, timestamp++, 0);
        QCOMPARE(c->opacity(), 0.6);
        pointer_axis_vertical(5, timestamp++, 0);
        QCOMPARE(c->opacity(), 0.5);
        keyboard_key_released(modifier.key, timestamp++);

        if (caps_lock) {
            keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        }
    }

    SECTION("touch events")
    {
        // Verifies that the decoration gets a hover leave event on touch release, see BUG 386231.
        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());

        EventHelper helper;
        win::decoration(c)->installEventFilter(&helper);
        QSignalSpy hoverMoveSpy(&helper, &EventHelper::hoverMove);
        QVERIFY(hoverMoveSpy.isValid());
        QSignalSpy hoverLeaveSpy(&helper, &EventHelper::hoverLeave);
        QVERIFY(hoverLeaveSpy.isValid());

        quint32 timestamp = 1;
        const QPoint tapPoint(c->geo.frame.center().x(),
                              win::frame_to_client_pos(c, QPoint()).y() / 2);

        QVERIFY(!setup.base->space->input->touch->focus.deco.client);
        touch_down(0, tapPoint, timestamp++);
        QVERIFY(setup.base->space->input->touch->focus.deco.client);
        QCOMPARE(setup.base->space->input->touch->focus.deco.client->decoration(),
                 win::decoration(c));
        QCOMPARE(hoverMoveSpy.count(), 1);
        QCOMPARE(hoverLeaveSpy.count(), 0);
        touch_up(0, timestamp++);
        QCOMPARE(hoverMoveSpy.count(), 1);
        QCOMPARE(hoverLeaveSpy.count(), 1);

        QCOMPARE(win::is_move(c), false);

        // let's check that a hover motion is sent if the pointer is on deco, when touch release
        cursor()->set_pos(tapPoint);
        QCOMPARE(hoverMoveSpy.count(), 2);
        touch_down(0, tapPoint, timestamp++);
        QCOMPARE(hoverMoveSpy.count(), 3);
        QCOMPARE(hoverLeaveSpy.count(), 1);
        touch_up(0, timestamp++);
        QCOMPARE(hoverMoveSpy.count(), 3);
        QCOMPARE(hoverLeaveSpy.count(), 2);
    }

    SECTION("tooltip doesnt eat key events")
    {
        // Verifies that a tooltip on the decoration does not steal key events, see BUG: 393253.

        // first create a keyboard
        auto seat = get_client().interfaces.seat.get();
        auto keyboard = seat->createKeyboard(seat);
        QVERIFY(keyboard);
        QSignalSpy enteredSpy(keyboard, &Wrapland::Client::Keyboard::entered);
        QVERIFY(enteredSpy.isValid());

        auto c = showWindow();
        QVERIFY(c);
        QVERIFY(win::decoration(c));
        QVERIFY(!c->noBorder());
        QVERIFY(enteredSpy.wait());

        QSignalSpy keyEvent(keyboard, &Wrapland::Client::Keyboard::keyChanged);
        QVERIFY(keyEvent.isValid());

        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        c->control->deco.client->requestShowToolTip(QStringLiteral("test"));
        // now we should get an internal window

        QVERIFY(clientAddedSpy.wait());
        auto win_id = clientAddedSpy.first().first().value<quint32>();
        auto internal = get_internal_window(setup.base->space->windows_map.at(win_id));
        QVERIFY(internal);
        QVERIFY(internal->isInternal());
        QVERIFY(internal->internalWindow()->flags().testFlag(Qt::ToolTip));

        // now send a key
        quint32 timestamp = 0;
        keyboard_key_pressed(KEY_A, timestamp++);
        QVERIFY(keyEvent.wait());
        keyboard_key_released(KEY_A, timestamp++);
        QVERIFY(keyEvent.wait());

        c->control->deco.client->requestHideToolTip();
        wait_for_destroyed(internal);
    }
}

}

#include "decoration_input.moc"
