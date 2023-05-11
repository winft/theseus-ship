/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect.h"

#include "base/logging.h"
#include "render/effect/basic_effect_loader.h"
#include <KPackage/PackageLoader>
#include <KPluginMetaData>
#include <string_view>

namespace KWin::scripting
{

/**
 * @brief Can load scripted Effects
 */
template<typename Compositor>
class effect_loader : public render::basic_effect_loader
{
public:
    effect_loader(EffectsHandler& effects, Compositor& compositor, QObject* parent = nullptr)
        : render::basic_effect_loader(parent)
        , effects{effects}
        , compositor{compositor}
    {
    }

    ~effect_loader() override = default;

    bool hasEffect(QString const& name) const override
    {
        return findEffect(name).isValid();
    }

    bool isEffectSupported(QString const& name) const override
    {
        // scripted effects are in general supported
        if (!effect::supported(effects)) {
            return false;
        }
        return hasEffect(name);
    }

    QStringList listOfKnownEffects() const override
    {
        const auto effects = findAllEffects();
        QStringList result;
        for (const auto& service : effects) {
            result << service.pluginId();
        }
        return result;
    }

    void clear() override
    {
    }

    void queryAndLoadAll() override
    {
        auto const effects = findAllEffects();
        for (auto const& effect : effects) {
            auto const load_flags = readConfig(effect.pluginId(), effect.isEnabledByDefault());
            if (flags(load_flags & render::load_effect_flags::load)) {
                loadEffect(effect, load_flags);
            }
        }
    }

    bool loadEffect(QString const& name) override
    {
        auto effect = findEffect(name);
        if (!effect.isValid()) {
            return false;
        }
        return loadEffect(effect, render::load_effect_flags::load);
    }

    bool loadEffect(const KPluginMetaData& effect, render::load_effect_flags flags)
    {
        QString const name = effect.pluginId();
        if (!(flags & render::load_effect_flags::load)) {
            qCDebug(KWIN_CORE) << "Loading flags disable effect: " << name;
            return false;
        }
        if (m_loadedEffects.contains(name)) {
            qCDebug(KWIN_CORE) << name << "already loaded";
            return false;
        }

        if (!effect::supported(effects)) {
            qCDebug(KWIN_CORE) << "Effect is not supported: " << name;
            return false;
        }

        auto e = effect::create(effect, effects, compositor);
        if (!e) {
            qCDebug(KWIN_CORE) << "Could not initialize scripted effect: " << name;
            return false;
        }
        connect(e, &effect::destroyed, this, [this, name]() { m_loadedEffects.removeAll(name); });

        qCDebug(KWIN_CORE) << "Successfully loaded scripted effect: " << name;
        Q_EMIT effectLoaded(e, name);
        m_loadedEffects << name;
        return true;
    }

private:
    static constexpr std::string_view s_serviceType{"KWin/Effect"};

    QList<KPluginMetaData> findAllEffects() const
    {
        return KPackage::PackageLoader::self()->listPackages(
            QString::fromStdString(std::string(s_serviceType)), QStringLiteral("kwin/effects"));
    }

    KPluginMetaData findEffect(QString const& name) const
    {
        auto const plugins = KPackage::PackageLoader::self()->findPackages(
            QString::fromStdString(std::string(s_serviceType)),
            QStringLiteral("kwin/effects"),
            [name](const KPluginMetaData& metadata) {
                return metadata.pluginId().compare(name, Qt::CaseInsensitive) == 0;
            });
        if (!plugins.isEmpty()) {
            return plugins.first();
        }
        return KPluginMetaData();
    }

    QStringList m_loadedEffects;
    EffectsHandler& effects;
    Compositor& compositor;
};

}
