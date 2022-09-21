/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QByteArray>
#include <QUuid>

namespace KWin::win
{

struct window_metadata {
    window_metadata(uint32_t signal_id)
        : internal_id{QUuid::createUuid()}
        , signal_id{signal_id}
    {
    }

    struct {
        QString normal;
        // Suffix added to normal caption (e.g. shortcut, machine name, etc.).
        QString suffix;
    } caption;

    struct {
        // Always lowercase.
        QByteArray res_name;
        QByteArray res_class;
    } wm_class;

    // A UUID to uniquely identify this Toplevel independent of windowing system.
    QUuid internal_id;

    // Being used internally when emitting signals. Access via the space windows_map.
    uint32_t signal_id;
};

}
