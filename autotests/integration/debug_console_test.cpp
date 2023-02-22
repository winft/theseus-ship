/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "base/x11/xcb/window.h"
#include "debug/console/wayland/wayland_console.h"
#include "win/control.h"
#include "win/deco.h"
#include "win/internal_window.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>

#include <QPainter>
#include <QRasterWindow>

namespace KWin::detail::test
{

class HelperWindow : public QRasterWindow
{
    Q_OBJECT
public:
    HelperWindow()
        : QRasterWindow(nullptr)
    {
    }
    ~HelperWindow() override = default;

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
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event)
        QPainter p(this);
        p.fillRect(0, 0, width(), height(), Qt::red);
    }
};

TEST_CASE("debug console", "[debug]")
{
    test::setup setup("debug-console", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    Test::test_outputs_default();
    Test::setup_wayland_connection();

    SECTION("toplevel")
    {
        struct data {
            int row;
            int column;
            bool expected_valid;
        };

        // This tests various combinations of row/column on the toplevel whether they are valid.
        // Valid are rows 0-4 with column 0, everything else is invalid.
        auto test_data = GENERATE(data{0, 0, true},
                                  data{0, 1, false},
                                  data{0, 3, false},
                                  data{1, 0, true},
                                  data{1, 1, false},
                                  data{1, 3, false},
                                  data{2, 0, true},
                                  data{3, 0, true},
                                  data{4, 0, false},
                                  data{100, 0, false});

        auto model = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));
        QCOMPARE(model->rowCount(QModelIndex()), 4);
        QCOMPARE(model->columnCount(QModelIndex()), 2);

        auto const index = model->index(test_data.row, test_data.column, QModelIndex());
        QCOMPARE(index.isValid(), test_data.expected_valid);

        if (index.isValid()) {
            QVERIFY(!model->parent(index).isValid());
            QVERIFY(model->data(index, Qt::DisplayRole).isValid());
            QCOMPARE(model->data(index, Qt::DisplayRole).userType(), int(QMetaType::QString));
            for (int i = Qt::DecorationRole; i <= Qt::UserRole; i++) {
                QVERIFY(!model->data(index, i).isValid());
            }
        }
    }

    SECTION("x11 window with control")
    {
        auto model = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));

        auto x11TopLevelIndex = model->index(0, 0, QModelIndex());
        QVERIFY(x11TopLevelIndex.isValid());

        // we don't have any windows yet
        QCOMPARE(model->rowCount(x11TopLevelIndex), 0);
        QVERIFY(!model->hasChildren(x11TopLevelIndex));

        // child index must be invalid
        QVERIFY(!model->index(0, 0, x11TopLevelIndex).isValid());
        QVERIFY(!model->index(0, 1, x11TopLevelIndex).isValid());
        QVERIFY(!model->index(0, 2, x11TopLevelIndex).isValid());
        QVERIFY(!model->index(1, 0, x11TopLevelIndex).isValid());

        // start glxgears, to get a window, which should be added to the model
        QSignalSpy rowsInsertedSpy(model.get(), &QAbstractItemModel::rowsInserted);
        QVERIFY(rowsInsertedSpy.isValid());

        QProcess glxgears;
        glxgears.setProgram(QStringLiteral("glxgears"));
        glxgears.start();
        QVERIFY(glxgears.waitForStarted());

        QVERIFY(rowsInsertedSpy.wait());
        QCOMPARE(rowsInsertedSpy.count(), 1);
        QVERIFY(model->hasChildren(x11TopLevelIndex));
        QCOMPARE(model->rowCount(x11TopLevelIndex), 1);
        QCOMPARE(rowsInsertedSpy.first().at(0).value<QModelIndex>(), x11TopLevelIndex);
        QCOMPARE(rowsInsertedSpy.first().at(1).value<int>(), 0);
        QCOMPARE(rowsInsertedSpy.first().at(2).value<int>(), 0);

        QModelIndex clientIndex = model->index(0, 0, x11TopLevelIndex);
        QVERIFY(clientIndex.isValid());
        QCOMPARE(model->parent(clientIndex), x11TopLevelIndex);
        QVERIFY(model->hasChildren(clientIndex));
        QVERIFY(model->rowCount(clientIndex) != 0);
        QCOMPARE(model->columnCount(clientIndex), 2);

        // other indexes are still invalid
        QVERIFY(!model->index(0, 1, x11TopLevelIndex).isValid());
        QVERIFY(!model->index(0, 2, x11TopLevelIndex).isValid());
        QVERIFY(!model->index(1, 0, x11TopLevelIndex).isValid());

        // the clientIndex has children and those are properties
        for (int i = 0; i < model->rowCount(clientIndex); i++) {
            const QModelIndex propNameIndex = model->index(i, 0, clientIndex);
            QVERIFY(propNameIndex.isValid());
            QCOMPARE(model->parent(propNameIndex), clientIndex);
            QVERIFY(!model->hasChildren(propNameIndex));
            QVERIFY(!model->index(0, 0, propNameIndex).isValid());
            QVERIFY(model->data(propNameIndex, Qt::DisplayRole).isValid());
            QCOMPARE(model->data(propNameIndex, Qt::DisplayRole).userType(),
                     int(QMetaType::QString));

            // and the value
            const QModelIndex propValueIndex = model->index(i, 1, clientIndex);
            QVERIFY(propValueIndex.isValid());
            QCOMPARE(model->parent(propValueIndex), clientIndex);
            QVERIFY(!model->index(0, 0, propValueIndex).isValid());
            QVERIFY(!model->hasChildren(propValueIndex));
            // TODO: how to test whether the values actually work?

            // and on third column we should not get an index any more
            QVERIFY(!model->index(i, 2, clientIndex).isValid());
        }

        // row after count should be invalid
        QVERIFY(!model->index(model->rowCount(clientIndex), 0, clientIndex).isValid());

        // creating a second model should be initialized directly with the X11 child
        auto model2 = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));
        QVERIFY(model2->hasChildren(model2->index(0, 0, QModelIndex())));

        // now close the window again, it should be removed from the model
        QSignalSpy rowsRemovedSpy(model.get(), &QAbstractItemModel::rowsRemoved);
        QVERIFY(rowsRemovedSpy.isValid());

        glxgears.terminate();
        QVERIFY(glxgears.waitForFinished());

        QVERIFY(rowsRemovedSpy.wait());
        QCOMPARE(rowsRemovedSpy.count(), 1);
        QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), x11TopLevelIndex);
        QCOMPARE(rowsRemovedSpy.first().at(1).value<int>(), 0);
        QCOMPARE(rowsRemovedSpy.first().at(2).value<int>(), 0);

        // the child should be gone again
        QVERIFY(!model->hasChildren(x11TopLevelIndex));
        QVERIFY(!model2->hasChildren(model2->index(0, 0, QModelIndex())));
    }

    SECTION("x11 unmanaged window")
    {
        auto model = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));

        auto unmanagedTopLevelIndex = model->index(1, 0, QModelIndex());
        QVERIFY(unmanagedTopLevelIndex.isValid());

        // we don't have any windows yet
        QCOMPARE(model->rowCount(unmanagedTopLevelIndex), 0);
        QVERIFY(!model->hasChildren(unmanagedTopLevelIndex));

        // child index must be invalid
        QVERIFY(!model->index(0, 0, unmanagedTopLevelIndex).isValid());
        QVERIFY(!model->index(0, 1, unmanagedTopLevelIndex).isValid());
        QVERIFY(!model->index(0, 2, unmanagedTopLevelIndex).isValid());
        QVERIFY(!model->index(1, 0, unmanagedTopLevelIndex).isValid());

        // we need to create an unmanaged window
        QSignalSpy rowsInsertedSpy(model.get(), &QAbstractItemModel::rowsInserted);
        QVERIFY(rowsInsertedSpy.isValid());

        // let's create an override redirect window
        const uint32_t values[] = {true};
        base::x11::xcb::window window(setup.base->x11_data.connection,
                                      setup.base->x11_data.root_window,
                                      QRect(0, 0, 10, 10),
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      values);
        window.map();

        QVERIFY(rowsInsertedSpy.wait());
        QCOMPARE(rowsInsertedSpy.count(), 1);
        QVERIFY(model->hasChildren(unmanagedTopLevelIndex));
        QCOMPARE(model->rowCount(unmanagedTopLevelIndex), 1);
        QCOMPARE(rowsInsertedSpy.first().at(0).value<QModelIndex>(), unmanagedTopLevelIndex);
        QCOMPARE(rowsInsertedSpy.first().at(1).value<int>(), 0);
        QCOMPARE(rowsInsertedSpy.first().at(2).value<int>(), 0);

        QModelIndex clientIndex = model->index(0, 0, unmanagedTopLevelIndex);
        QVERIFY(clientIndex.isValid());
        QCOMPARE(model->parent(clientIndex), unmanagedTopLevelIndex);
        QVERIFY(model->hasChildren(clientIndex));
        QVERIFY(model->rowCount(clientIndex) != 0);
        QCOMPARE(model->columnCount(clientIndex), 2);
        // other indexes are still invalid
        QVERIFY(!model->index(0, 1, unmanagedTopLevelIndex).isValid());
        QVERIFY(!model->index(0, 2, unmanagedTopLevelIndex).isValid());
        QVERIFY(!model->index(1, 0, unmanagedTopLevelIndex).isValid());

        QCOMPARE(model->data(clientIndex, Qt::DisplayRole).toString(), QString::number(window));

        // the clientIndex has children and those are properties
        for (int i = 0; i < model->rowCount(clientIndex); i++) {
            const QModelIndex propNameIndex = model->index(i, 0, clientIndex);
            QVERIFY(propNameIndex.isValid());
            QCOMPARE(model->parent(propNameIndex), clientIndex);
            QVERIFY(!model->hasChildren(propNameIndex));
            QVERIFY(!model->index(0, 0, propNameIndex).isValid());
            QVERIFY(model->data(propNameIndex, Qt::DisplayRole).isValid());
            QCOMPARE(model->data(propNameIndex, Qt::DisplayRole).userType(),
                     int(QMetaType::QString));

            // and the value
            const QModelIndex propValueIndex = model->index(i, 1, clientIndex);
            QVERIFY(propValueIndex.isValid());
            QCOMPARE(model->parent(propValueIndex), clientIndex);
            QVERIFY(!model->index(0, 0, propValueIndex).isValid());
            QVERIFY(!model->hasChildren(propValueIndex));
            // TODO: how to test whether the values actually work?

            // and on third column we should not get an index any more
            QVERIFY(!model->index(i, 2, clientIndex).isValid());
        }
        // row after count should be invalid
        QVERIFY(!model->index(model->rowCount(clientIndex), 0, clientIndex).isValid());

        // creating a second model should be initialized directly with the X11 child
        auto model2 = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));
        QVERIFY(model2->hasChildren(model2->index(1, 0, QModelIndex())));

        // now close the window again, it should be removed from the model
        QSignalSpy rowsRemovedSpy(model.get(), &QAbstractItemModel::rowsRemoved);
        QVERIFY(rowsRemovedSpy.isValid());

        window.unmap();

        QVERIFY(rowsRemovedSpy.wait());
        QCOMPARE(rowsRemovedSpy.count(), 1);
        QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), unmanagedTopLevelIndex);
        QCOMPARE(rowsRemovedSpy.first().at(1).value<int>(), 0);
        QCOMPARE(rowsRemovedSpy.first().at(2).value<int>(), 0);

        // the child should be gone again
        QVERIFY(!model->hasChildren(unmanagedTopLevelIndex));
        QVERIFY(!model2->hasChildren(model2->index(1, 0, QModelIndex())));
    }

    SECTION("wayland window")
    {
        auto model = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));

        auto waylandTopLevelIndex = model->index(2, 0, QModelIndex());
        QVERIFY(waylandTopLevelIndex.isValid());

        // we don't have any windows yet
        QCOMPARE(model->rowCount(waylandTopLevelIndex), 0);
        QVERIFY(!model->hasChildren(waylandTopLevelIndex));

        // child index must be invalid
        QVERIFY(!model->index(0, 0, waylandTopLevelIndex).isValid());
        QVERIFY(!model->index(0, 1, waylandTopLevelIndex).isValid());
        QVERIFY(!model->index(0, 2, waylandTopLevelIndex).isValid());
        QVERIFY(!model->index(1, 0, waylandTopLevelIndex).isValid());

        // we need to create a wayland window
        QSignalSpy rowsInsertedSpy(model.get(), &QAbstractItemModel::rowsInserted);
        QVERIFY(rowsInsertedSpy.isValid());

        // create our connection
        Test::setup_wayland_connection();

        // create the Surface and ShellSurface
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(Test::create_surface());
        QVERIFY(surface->isValid());
        std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        Test::render(surface, QSize(10, 10), Qt::red);

        // now we have the window, it should be added to our model
        QVERIFY(rowsInsertedSpy.wait());
        QCOMPARE(rowsInsertedSpy.count(), 1);

        QVERIFY(model->hasChildren(waylandTopLevelIndex));
        QCOMPARE(model->rowCount(waylandTopLevelIndex), 1);
        QCOMPARE(rowsInsertedSpy.first().at(0).value<QModelIndex>(), waylandTopLevelIndex);
        QCOMPARE(rowsInsertedSpy.first().at(1).value<int>(), 0);
        QCOMPARE(rowsInsertedSpy.first().at(2).value<int>(), 0);

        QModelIndex clientIndex = model->index(0, 0, waylandTopLevelIndex);
        QVERIFY(clientIndex.isValid());
        QCOMPARE(model->parent(clientIndex), waylandTopLevelIndex);
        QVERIFY(model->hasChildren(clientIndex));
        QVERIFY(model->rowCount(clientIndex) != 0);
        QCOMPARE(model->columnCount(clientIndex), 2);

        // other indexes are still invalid
        QVERIFY(!model->index(0, 1, waylandTopLevelIndex).isValid());
        QVERIFY(!model->index(0, 2, waylandTopLevelIndex).isValid());
        QVERIFY(!model->index(1, 0, waylandTopLevelIndex).isValid());

        // the clientIndex has children and those are properties
        for (int i = 0; i < model->rowCount(clientIndex); i++) {
            const QModelIndex propNameIndex = model->index(i, 0, clientIndex);
            QVERIFY(propNameIndex.isValid());
            QCOMPARE(model->parent(propNameIndex), clientIndex);
            QVERIFY(!model->hasChildren(propNameIndex));
            QVERIFY(!model->index(0, 0, propNameIndex).isValid());
            QVERIFY(model->data(propNameIndex, Qt::DisplayRole).isValid());
            QCOMPARE(model->data(propNameIndex, Qt::DisplayRole).userType(),
                     int(QMetaType::QString));

            // and the value
            const QModelIndex propValueIndex = model->index(i, 1, clientIndex);
            QVERIFY(propValueIndex.isValid());
            QCOMPARE(model->parent(propValueIndex), clientIndex);
            QVERIFY(!model->index(0, 0, propValueIndex).isValid());
            QVERIFY(!model->hasChildren(propValueIndex));

            // TODO: how to test whether the values actually work?

            // and on third column we should not get an index any more
            QVERIFY(!model->index(i, 2, clientIndex).isValid());
        }
        // row after count should be invalid
        QVERIFY(!model->index(model->rowCount(clientIndex), 0, clientIndex).isValid());

        // creating a second model should be initialized directly with the X11 child
        auto model2 = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));
        QVERIFY(model2->hasChildren(model2->index(2, 0, QModelIndex())));

        // now close the window again, it should be removed from the model
        QSignalSpy rowsRemovedSpy(model.get(), &QAbstractItemModel::rowsRemoved);
        QVERIFY(rowsRemovedSpy.isValid());

        surface->attachBuffer(Buffer::Ptr());
        surface->commit(Surface::CommitFlag::None);
        shellSurface.reset();
        Test::flush_wayland_connection();
        QVERIFY(rowsRemovedSpy.wait(500));
        surface.reset();

        if (rowsRemovedSpy.isEmpty()) {
            QVERIFY(rowsRemovedSpy.wait());
        }
        QCOMPARE(rowsRemovedSpy.count(), 1);
        QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), waylandTopLevelIndex);
        QCOMPARE(rowsRemovedSpy.first().at(1).value<int>(), 0);
        QCOMPARE(rowsRemovedSpy.first().at(2).value<int>(), 0);

        // the child should be gone again
        QVERIFY(!model->hasChildren(waylandTopLevelIndex));
        QVERIFY(!model2->hasChildren(model2->index(2, 0, QModelIndex())));
    }

    SECTION("internal window")
    {
        auto model = std::unique_ptr<debug::wayland_console_model>(
            debug::wayland_console_model::create(*setup.base->space));

        auto internalTopLevelIndex = model->index(3, 0, QModelIndex());
        QVERIFY(internalTopLevelIndex.isValid());

        // there might already be some internal windows, so we cannot reliable test whether there
        // are children given that we just test whether adding a window works.

        QSignalSpy rowsInsertedSpy(model.get(), &QAbstractItemModel::rowsInserted);
        QVERIFY(rowsInsertedSpy.isValid());

        std::unique_ptr<HelperWindow> w(new HelperWindow);
        w->setGeometry(0, 0, 100, 100);
        w->show();

        QTRY_COMPARE(rowsInsertedSpy.count(), 1);
        QCOMPARE(rowsInsertedSpy.first().first().value<QModelIndex>(), internalTopLevelIndex);

        QModelIndex clientIndex
            = model->index(rowsInsertedSpy.first().last().toInt(), 0, internalTopLevelIndex);
        QVERIFY(clientIndex.isValid());
        QCOMPARE(model->parent(clientIndex), internalTopLevelIndex);
        QVERIFY(model->hasChildren(clientIndex));
        QVERIFY(model->rowCount(clientIndex) != 0);
        QCOMPARE(model->columnCount(clientIndex), 2);
        // other indexes are still invalid
        QVERIFY(!model->index(rowsInsertedSpy.first().last().toInt(), 1, internalTopLevelIndex)
                     .isValid());
        QVERIFY(!model->index(rowsInsertedSpy.first().last().toInt(), 2, internalTopLevelIndex)
                     .isValid());
        QVERIFY(!model->index(rowsInsertedSpy.first().last().toInt() + 1, 0, internalTopLevelIndex)
                     .isValid());

        // the wayland shell client top level should not have gained this window
        QVERIFY(!model->hasChildren(model->index(2, 0, QModelIndex())));

        // the clientIndex has children and those are properties
        for (int i = 0; i < model->rowCount(clientIndex); i++) {
            const QModelIndex propNameIndex = model->index(i, 0, clientIndex);
            QVERIFY(propNameIndex.isValid());
            QCOMPARE(model->parent(propNameIndex), clientIndex);
            QVERIFY(!model->hasChildren(propNameIndex));
            QVERIFY(!model->index(0, 0, propNameIndex).isValid());
            QVERIFY(model->data(propNameIndex, Qt::DisplayRole).isValid());
            QCOMPARE(model->data(propNameIndex, Qt::DisplayRole).userType(),
                     int(QMetaType::QString));

            // and the value
            const QModelIndex propValueIndex = model->index(i, 1, clientIndex);
            QVERIFY(propValueIndex.isValid());
            QCOMPARE(model->parent(propValueIndex), clientIndex);
            QVERIFY(!model->index(0, 0, propValueIndex).isValid());
            QVERIFY(!model->hasChildren(propValueIndex));
            // TODO: how to test whether the values actually work?

            // and on third column we should not get an index any more
            QVERIFY(!model->index(i, 2, clientIndex).isValid());
        }
        // row after count should be invalid
        QVERIFY(!model->index(model->rowCount(clientIndex), 0, clientIndex).isValid());

        // now close the window again, it should be removed from the model
        QSignalSpy rowsRemovedSpy(model.get(), &QAbstractItemModel::rowsRemoved);
        QVERIFY(rowsRemovedSpy.isValid());

        w->hide();
        w.reset();

        QTRY_COMPARE(rowsRemovedSpy.count(), 1);
        QCOMPARE(rowsRemovedSpy.first().first().value<QModelIndex>(), internalTopLevelIndex);
    }

    SECTION("closing debug console")
    {
        // this test verifies that the DebugConsole gets destroyed when closing the window
        // BUG: 369858

        auto console = new debug::console(*setup.base->space);
        QSignalSpy destroyedSpy(console, &QObject::destroyed);
        QVERIFY(destroyedSpy.isValid());

        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &win::space::qobject_t::internalClientAdded);
        QVERIFY(clientAddedSpy.isValid());
        console->show();
        QCOMPARE(console->windowHandle()->isVisible(), true);
        QTRY_COMPARE(clientAddedSpy.count(), 1);

        auto win_id = clientAddedSpy.first().first().value<quint32>();
        auto c = Test::get_internal_window(setup.base->space->windows_map.at(win_id));
        QVERIFY(c);
        QVERIFY(c->isInternal());
        QCOMPARE(c->internalWindow(), console->windowHandle());
        QVERIFY(win::decoration(c));
        QCOMPARE(c->isMinimizable(), false);
        c->closeWindow();
        QVERIFY(destroyedSpy.wait());
    }
}

}

#include "debug_console_test.moc"
