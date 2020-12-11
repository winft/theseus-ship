/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "cursor.h"
#include "decorations/decoratedclient.h"
#include "decorations/decorationpalette.h"

#include <KDecoration2/Decoration>

#include <QElapsedTimer>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QTimer>

#include <memory>

namespace KWin::win
{

struct move_resize_op {
    bool enabled{false};
    bool unrestricted{false};
    QPoint offset;
    QPoint inverted_offset;
    QRect initial_geometry;
    QRect geometry;
    win::position contact{win::position::center};
    bool button_down{false};
    CursorShape cursor{Qt::ArrowCursor};
    int start_screen{0};
    QTimer* delay_timer{nullptr};
};

struct deco {
    KDecoration2::Decoration* decoration{nullptr};
    QPointer<Decoration::DecoratedClientImpl> client;

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

    bool enabled() const
    {
        return decoration != nullptr;
    }
};

struct palette {
    using dp = Decoration::DecorationPalette;

    std::shared_ptr<dp> current;
    QString color_scheme;

    inline static QHash<QString, std::weak_ptr<dp>> palettes_registry;
    inline static std::shared_ptr<dp> default_palette;

    QPalette q_palette() const
    {
        if (!current) {
            return QPalette();
        }
        return current->palette();
    }
};

}
