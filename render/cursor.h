/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <memory>

namespace KWin
{

namespace input
{
class platform;
}

namespace render
{

class platform;

class KWIN_EXPORT cursor_qobject : public QObject
{
    Q_OBJECT
public:
    cursor_qobject();

Q_SIGNALS:
    void changed();
};

template<typename Platform, typename Input>
class cursor
{
public:
    cursor(Platform& platform, Input* input)
        : qobject{std::make_unique<cursor_qobject>()}
        , platform{platform}
        , input{input}
    {
        QObject::connect(
            qobject.get(), &cursor_qobject::changed, qobject.get(), [this] { rerender(); });
    }

    void set_enabled(bool enable)
    {
        if (qEnvironmentVariableIsSet("KWIN_FORCE_SW_CURSOR")) {
            enable = true;
        }
        if (enabled == enable) {
            return;
        }

        enabled = enable;
        auto cursor = input->cursor.get();
        using cursor_t = typename decltype(input->cursor)::element_type;

        if (enable) {
            cursor->start_image_tracking();
            notifiers.pos = QObject::connect(
                cursor, &cursor_t::pos_changed, qobject.get(), [this] { rerender(); });
            notifiers.image = QObject::connect(
                cursor, &cursor_t::image_changed, qobject.get(), &cursor_qobject::changed);
        } else {
            cursor->stop_image_tracking();
            QObject::disconnect(notifiers.pos);
            QObject::disconnect(notifiers.image);
        }
    }

    QImage image() const
    {
        return input->cursor->image();
    }

    QPoint hotspot() const
    {
        return input->cursor->hotspot();
    }

    void mark_as_rendered()
    {
        if (enabled) {
            last_rendered_geometry = QRect(input->cursor->pos() - hotspot(), image().size());
        }
        input->cursor->mark_as_rendered();
    }

    std::unique_ptr<cursor_qobject> qobject;
    bool enabled{false};

private:
    void rerender()
    {
        auto& compositor = platform.compositor;
        compositor->addRepaint(last_rendered_geometry);
        compositor->addRepaint(QRect(input->cursor->pos() - hotspot(), image().size()));
    }

    Platform& platform;
    Input* input;
    QRect last_rendered_geometry;

    struct {
        QMetaObject::Connection pos;
        QMetaObject::Connection image;
    } notifiers;
};

}
}
