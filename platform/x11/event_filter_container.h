/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>

namespace KWin
{

class X11EventFilter;

namespace platform::x11
{

class event_filter_container : public QObject
{
    Q_OBJECT

public:
    explicit event_filter_container(X11EventFilter* filter);

    X11EventFilter* filter() const;

private:
    X11EventFilter* m_filter;
};

}
}
