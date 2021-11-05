/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "xkb.h"

#include <QObject>

namespace KWin::input
{

class keyboard;
class redirect;

class KWIN_EXPORT keyboard_redirect : public QObject
{
    Q_OBJECT
public:
    explicit keyboard_redirect(input::redirect* parent);
    ~keyboard_redirect() override;

    input::xkb* xkb() const;
    Qt::KeyboardModifiers modifiers() const;
    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts() const;

    virtual void update();

    virtual void process_key(key_event const& event);
    virtual void process_key_repeat(uint32_t key, uint32_t time);

    void process_modifiers(modifiers_event const& event);
    virtual void processModifiers(uint32_t modsDepressed,
                                  uint32_t modsLatched,
                                  uint32_t modsLocked,
                                  uint32_t group);

    virtual void processKeymapChange(int fd, uint32_t size);

Q_SIGNALS:
    void ledsChanged(input::keyboard_leds);

protected:
    std::unique_ptr<input::xkb> m_xkb;

    input::redirect* redirect;
    bool m_inited = false;
};

}
