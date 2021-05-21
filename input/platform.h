/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <KSharedConfig>
#include <QObject>
#include <memory>
#include <vector>

namespace KWin::input
{

class keyboard;
class pointer;
class touch;

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;
    std::vector<touch*> touchs;

    platform(QObject* parent = nullptr);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform();

    KSharedConfigPtr config;

Q_SIGNALS:
    void keyboard_added(KWin::input::keyboard*);
    void pointer_added(KWin::input::pointer*);
    void touch_added(KWin::input::touch*);

    void keyboard_removed(KWin::input::keyboard*);
    void pointer_removed(KWin::input::pointer*);
    void touch_removed(KWin::input::touch*);
};

}
