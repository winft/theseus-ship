/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "keyboard.h"
#include "numlock.h"

#include "base/logging.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <memory>
#include <string>
#include <vector>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

namespace KWin::input::xkb
{

static inline void
log_handler(xkb_context* /*context*/, xkb_log_level priority, char const* format, va_list args)
{
    char buf[1024];
    int length = std::vsnprintf(buf, 1023, format, args);

    while (length > 0 && std::isspace(buf[length - 1])) {
        --length;
    }

    if (length <= 0) {
        return;
    }

    switch (priority) {
    case XKB_LOG_LEVEL_DEBUG:
        qCDebug(KWIN_CORE, "XKB: %.*s", length, buf);
        break;
    case XKB_LOG_LEVEL_INFO:
        qCInfo(KWIN_CORE, "XKB: %.*s", length, buf);
        break;
    case XKB_LOG_LEVEL_WARNING:
        qCWarning(KWIN_CORE, "XKB: %.*s", length, buf);
        break;
    case XKB_LOG_LEVEL_ERROR:
    case XKB_LOG_LEVEL_CRITICAL:
    default:
        qCCritical(KWIN_CORE, "XKB: %.*s", length, buf);
        break;
    }
}

template<typename Platform>
class manager
{
public:
    manager(Platform* platform)
        : context(xkb_context_new(XKB_CONTEXT_NO_FLAGS))
        , platform{platform}
    {
        qRegisterMetaType<KWin::input::keyboard_leds>();

        if (!context) {
            // TODO(romangg): throw instead
            qCCritical(KWIN_CORE) << "Could not create xkb context";
            ::exit(1);
        }
        xkb_context_set_log_level(context, XKB_LOG_LEVEL_DEBUG);
        xkb_context_set_log_fn(context, &log_handler);

        // Get locale as described in xkbcommon doc, cannot use QLocale as it drops the modifier
        // part.
        QByteArray locale = qgetenv("LC_ALL");
        if (locale.isEmpty()) {
            locale = qgetenv("LC_CTYPE");
        }
        if (locale.isEmpty()) {
            locale = qgetenv("LANG");
        }
        if (locale.isEmpty()) {
            locale = QByteArrayLiteral("C");
        }

        compose_table = xkb_compose_table_new_from_locale(
            context, locale.constData(), XKB_COMPOSE_COMPILE_NO_FLAGS);

        default_keyboard = std::make_unique<keyboard>(context, compose_table);
        m_configGroup = platform->config.xkb->group("Layout");
    }

    ~manager()
    {
        xkb_compose_table_unref(compose_table);
        xkb_context_unref(context);
    }

    void setConfig(const KSharedConfigPtr& config)
    {
        m_configGroup = config->group("Layout");
    }

    void reconfigure()
    {
        xkb_keymap* keymap{nullptr};
        std::vector<std::string> layouts;

        if (!qEnvironmentVariableIsSet("KWIN_XKB_DEFAULT_KEYMAP")) {
            keymap = loadKeymapFromConfig(layouts);
        }
        if (!keymap) {
            qCDebug(KWIN_CORE) << "Could not create xkb keymap from configuration";
            keymap = loadDefaultKeymap(layouts);
        }
        if (!keymap) {
            qCDebug(KWIN_CORE) << "Could not create default xkb keymap";
            return;
        }

        default_keyboard->update(std::make_unique<xkb::keymap>(keymap), layouts);
        xkb_keymap_unref(keymap);

        numlock_evaluate_startup(*this, *default_keyboard);
        default_keyboard->update_modifiers();

        for (auto& keyboard : platform->keyboards) {
            keyboard->xkb->update(default_keyboard->keymap, layouts);
            numlock_evaluate_startup(*this, *keyboard->xkb);
        }
    }

    xkb_context* context;
    xkb_compose_table* compose_table{nullptr};

    std::unique_ptr<keyboard> default_keyboard;
    Platform* platform;

    KSharedConfigPtr numlock_config;
    KConfigGroup m_configGroup;

private:
    /**
     * libxkbcommon uses secure_getenv to read the XKB_DEFAULT_* variables.
     * As kwin_wayland may have the CAP_SET_NICE capability, it returns nullptr
     * so we need to do it ourselves (see xkb_context_sanitize_rule_names).
     **/
    void apply_environment_rules(xkb_rule_names& ruleNames, std::vector<std::string>& layouts) const
    {
        auto stringIsEmptyOrNull = [](const char* str) { return str == nullptr || str[0] == '\0'; };

        if (stringIsEmptyOrNull(ruleNames.rules)) {
            ruleNames.rules = getenv("XKB_DEFAULT_RULES");
        }

        if (stringIsEmptyOrNull(ruleNames.model)) {
            ruleNames.model = getenv("XKB_DEFAULT_MODEL");
        }

        if (stringIsEmptyOrNull(ruleNames.layout)) {
            ruleNames.layout = getenv("XKB_DEFAULT_LAYOUT");
            ruleNames.variant = getenv("XKB_DEFAULT_VARIANT");
        }

        if (ruleNames.options == nullptr) {
            ruleNames.options = getenv("XKB_DEFAULT_OPTIONS");
        }

        layouts.clear();

        if (ruleNames.layout) {
            auto layout_stream = std::stringstream(ruleNames.layout);
            while (layout_stream.good()) {
                std::string layout;
                getline(layout_stream, layout, ',');
                layouts.push_back(layout);
            }
        }
    }

    xkb_keymap* loadKeymapFromConfig(std::vector<std::string>& layouts)
    {
        // load config
        if (!m_configGroup.isValid()) {
            return nullptr;
        }

        QByteArray const model = m_configGroup.readEntry("Model", "pc104").toLatin1();
        QByteArray const layout = m_configGroup.readEntry("LayoutList").toLatin1();
        QByteArray const variant = m_configGroup.readEntry("VariantList").toLatin1();
        QByteArray const options = m_configGroup.readEntry("Options").toLatin1();

        xkb_rule_names ruleNames = {.rules = nullptr,
                                    .model = model.constData(),
                                    .layout = layout.constData(),
                                    .variant = variant.constData(),
                                    .options = options.constData()};

        apply_environment_rules(ruleNames, layouts);

        return xkb_keymap_new_from_names(context, &ruleNames, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    xkb_keymap* loadDefaultKeymap(std::vector<std::string>& layouts)
    {
        xkb_rule_names ruleNames = {};

        apply_environment_rules(ruleNames, layouts);

        return xkb_keymap_new_from_names(context, &ruleNames, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
};

}
