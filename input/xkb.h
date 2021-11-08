/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <KConfigGroup>
#include <KSharedConfig>
#include <QLoggingCategory>
#include <memory>
#include <string>
#include <vector>
#include <xkbcommon/xkbcommon.h>

Q_DECLARE_LOGGING_CATEGORY(KWIN_XKB)

struct xkb_compose_table;
struct xkb_compose_state;
struct xkb_context;
struct xkb_keymap;

namespace KWin::input
{
class platform;
class xkb_keyboard;

enum class latched_key_change {
    off,
    on,
    unchanged,
};

class KWIN_EXPORT xkb
{
public:
    xkb(input::platform* platform);
    ~xkb();

    void setConfig(const KSharedConfigPtr& config);
    void setNumLockConfig(const KSharedConfigPtr& config);

    void reconfigure();

    latched_key_change read_startup_num_lock_config();

    xkb_context* context;
    xkb_compose_table* compose_table{nullptr};

    std::unique_ptr<xkb_keyboard> default_keyboard;

private:
    void apply_environment_rules(xkb_rule_names&, std::vector<std::string>& layouts) const;

    xkb_keymap* loadKeymapFromConfig(std::vector<std::string>& layouts);
    xkb_keymap* loadDefaultKeymap(std::vector<std::string>& layouts);

    KConfigGroup m_configGroup;
    KSharedConfigPtr m_numLockConfig;
    input::platform* platform;
};

}
