/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <xcb/xproto.h>

namespace KWin::win::x11
{

class KWIN_EXPORT color_mapper : public QObject
{
    Q_OBJECT
public:
    color_mapper(QObject* parent);
    ~color_mapper() override;
public Q_SLOTS:
    void update();

private:
    xcb_colormap_t m_default;
    xcb_colormap_t m_installed;
};

}
