/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "xkb.h"

#include <QObject>
#include <QPointF>
#include <QPointer>

#include <KSharedConfig>

class QWindow;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct xkb_compose_table;
struct xkb_compose_state;
typedef uint32_t xkb_mod_index_t;
typedef uint32_t xkb_led_index_t;
typedef uint32_t xkb_keysym_t;
typedef uint32_t xkb_layout_index_t;

namespace KWin
{
class Toplevel;

namespace input
{
class keyboard;
class keyboard_layout_spy;
class modifiers_changed_spy;
class redirect;

class KWIN_EXPORT keyboard_redirect : public QObject
{
    Q_OBJECT
public:
    explicit keyboard_redirect(input::redirect* parent);
    ~keyboard_redirect() override;

    void init();

    input::xkb* xkb() const;
    Qt::KeyboardModifiers modifiers() const;
    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const;

    void update();

    void processKey(uint32_t key,
                    input::redirect::KeyboardKeyState state,
                    uint32_t time,
                    KWin::input::keyboard* device = nullptr);
    void processModifiers(uint32_t modsDepressed,
                          uint32_t modsLatched,
                          uint32_t modsLocked,
                          uint32_t group);
    void processKeymapChange(int fd, uint32_t size);

Q_SIGNALS:
    void ledsChanged(input::xkb::LEDs);

private:
    input::redirect* m_input;
    bool m_inited = false;
    QScopedPointer<input::xkb> m_xkb;
    QMetaObject::Connection m_activeClientSurfaceChangedConnection;
    modifiers_changed_spy* modifiers_spy = nullptr;
    keyboard_layout_spy* m_keyboardLayout = nullptr;
};

}
}
