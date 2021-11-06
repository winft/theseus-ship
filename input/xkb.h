/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "redirect.h"
#include "types.h"

#include <kwin_export.h>

#include <KConfigGroup>

#include <QLoggingCategory>
#include <QPointer>

#include <xkbcommon/xkbcommon.h>

Q_DECLARE_LOGGING_CATEGORY(KWIN_XKB)

struct xkb_compose_table;
struct xkb_compose_state;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

typedef uint32_t xkb_mod_index_t;
typedef uint32_t xkb_led_index_t;
typedef uint32_t xkb_keysym_t;
typedef uint32_t xkb_layout_index_t;

namespace Wrapland::Server
{
class Seat;
}

namespace KWin::input
{

class KWIN_EXPORT xkb : public QObject
{
    Q_OBJECT
public:
    xkb();
    ~xkb() override;

    void setConfig(const KSharedConfigPtr& config);
    void setNumLockConfig(const KSharedConfigPtr& config);

    void reconfigure();

    void installKeymap(int fd, uint32_t size);
    void updateModifiers(uint32_t modsDepressed,
                         uint32_t modsLatched,
                         uint32_t modsLocked,
                         uint32_t group);
    void updateKey(uint32_t key, key_state state);

    xkb_keysym_t toKeysym(uint32_t key);
    xkb_keysym_t currentKeysym() const
    {
        return m_keysym;
    }

    QString toString(xkb_keysym_t keysym);
    Qt::Key toQtKey(xkb_keysym_t keysym,
                    uint32_t scanCode = 0,
                    Qt::KeyboardModifiers modifiers = Qt::KeyboardModifiers(),
                    bool superAsMeta = false) const;

    Qt::KeyboardModifiers modifiers() const;
    Qt::KeyboardModifiers modifiersRelevantForGlobalShortcuts(uint32_t scanCode = 0) const;

    bool shouldKeyRepeat(quint32 key) const;

    void switchToNextLayout();
    void switchToPreviousLayout();
    bool switchToLayout(xkb_layout_index_t layout);

    keyboard_leds leds() const
    {
        return m_leds;
    }

    xkb_keymap* keymap() const
    {
        return m_keymap;
    }

    xkb_state* state() const
    {
        return m_state;
    }

    quint32 currentLayout() const
    {
        return m_currentLayout;
    }

    QString layoutName(xkb_layout_index_t index) const;
    QString layoutName() const;
    const QString& layoutShortName(int index) const;
    quint32 numberOfLayouts() const;

    /**
     * Forwards the current modifier state to the Wayland seat
     */
    void forwardModifiers();

    void setSeat(Wrapland::Server::Seat* seat);

Q_SIGNALS:
    void ledsChanged(keyboard_leds leds);

private:
    void apply_environment_rules(xkb_rule_names&, QStringList& layouts) const;

    xkb_keymap* loadKeymapFromConfig();
    xkb_keymap* loadDefaultKeymap();

    void updateKeymap(xkb_keymap* keymap);
    void createKeymapFile();
    void updateModifiers();
    void updateConsumedModifiers(uint32_t key);
    void evaluate_startup_num_lock();

    xkb_context* context;
    xkb_keymap* m_keymap{nullptr};
    QStringList m_layoutList;
    xkb_state* m_state{nullptr};

    xkb_mod_index_t m_shiftModifier{0};
    xkb_mod_index_t m_capsModifier{0};
    xkb_mod_index_t m_controlModifier{0};
    xkb_mod_index_t m_altModifier{0};
    xkb_mod_index_t m_metaModifier{0};
    xkb_mod_index_t m_numModifier{0};

    xkb_led_index_t m_numLock{0};
    xkb_led_index_t m_capsLock{0};
    xkb_led_index_t m_scrollLock{0};

    Qt::KeyboardModifiers m_modifiers{Qt::NoModifier};
    Qt::KeyboardModifiers m_consumedModifiers{Qt::NoModifier};

    xkb_keysym_t m_keysym{XKB_KEY_NoSymbol};
    quint32 m_currentLayout{0};

    struct {
        xkb_compose_table* table{nullptr};
        xkb_compose_state* state{nullptr};
    } compose;

    keyboard_leds m_leds{keyboard_leds::none};

    KConfigGroup m_configGroup;
    KSharedConfigPtr m_numLockConfig;

    struct {
        xkb_mod_index_t depressed{0};
        xkb_mod_index_t latched{0};
        xkb_mod_index_t locked{0};
    } m_modifierState;

    enum class Ownership {
        Server,
        Client,
    };
    Ownership m_ownership{Ownership::Server};

    QPointer<Wrapland::Server::Seat> m_seat;
};

inline Qt::KeyboardModifiers xkb::modifiers() const
{
    return m_modifiers;
}

}
