/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco/client_impl.h"
#include "deco/palette.h"
#include "types.h"

#include "input/cursor.h"

#include <KDecoration2/Decoration>

#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QTimer>

#include <memory>

namespace KWin
{

namespace win
{

namespace deco
{
class window;
}

struct move_resize_op {
    bool enabled{false};
    bool unrestricted{false};
    QPoint offset;
    QPoint inverted_offset;
    QRect initial_geometry;
    QRect geometry;
    win::position contact{win::position::center};
    bool button_down{false};
    input::cursor_shape cursor{Qt::ArrowCursor};
    int start_screen{0};
    QTimer* delay_timer{nullptr};
};

struct deco_impl {
    QMetaObject::Connection client_destroy;

    deco::window* window{nullptr};
    KDecoration2::Decoration* decoration{nullptr};
    deco::client_impl* client{nullptr};

    struct {
    private:
        std::unique_ptr<QElapsedTimer> timer;

    public:
        bool active()
        {
            return timer != nullptr;
        }
        void start()
        {
            if (!timer) {
                timer.reset(new QElapsedTimer);
            }
            timer->start();
        }
        qint64 stop()
        {
            qint64 const elapsed = timer ? timer->elapsed() : 0;
            timer.reset();
            return elapsed;
        }
    } double_click;

    deco_impl() = default;
    deco_impl(deco_impl&) = delete;
    deco_impl& operator=(deco_impl) = delete;
    deco_impl(deco_impl&& source) noexcept = delete;
    deco_impl& operator=(deco_impl&& source) noexcept = delete;

    void set_client(deco::client_impl* client)
    {
        assert(client);
        assert(!client_destroy);

        this->client = client;
        QObject::disconnect(client_destroy);
        client_destroy = QObject::connect(client->qobject.get(),
                                          &deco::client_impl_qobject::destroyed,
                                          client->qobject.get(),
                                          [this]() { this->client = nullptr; });
    }

    bool enabled() const
    {
        return decoration != nullptr;
    }
};

struct palette {
    using dp = deco::palette;

    std::shared_ptr<dp> current;
    QString color_scheme;

    inline static QHash<QString, std::weak_ptr<dp>> palettes_registry;
    inline static std::shared_ptr<dp> default_palette;

    QPalette q_palette() const
    {
        if (!current) {
            return QPalette();
        }
        return current->get_qt_palette();
    }
};

}
}
