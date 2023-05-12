/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "effect/basic_effect_loader.h"

#include "kwin_export.h"
#include "script/effect_loader.h"

#include <KPluginMetaData>
#include <memory>
#include <vector>

namespace KWin
{

class EffectPluginFactory;
class EffectsHandler;

namespace render
{

class KWIN_EXPORT plugin_effect_loader : public basic_effect_loader
{
public:
    plugin_effect_loader();
    ~plugin_effect_loader() override;

    bool hasEffect(const QString& name) const override;
    bool isEffectSupported(const QString& name) const override;
    QStringList listOfKnownEffects() const override;

    void clear() override;
    void queryAndLoadAll() override;
    bool loadEffect(const QString& name) override;
    bool loadEffect(const KPluginMetaData& info, load_effect_flags load_flags);

    void setPluginSubDirectory(const QString& directory);

private:
    QVector<KPluginMetaData> findAllEffects() const;
    KPluginMetaData findEffect(const QString& name) const;
    EffectPluginFactory* factory(const KPluginMetaData& info) const;
    QStringList m_loadedEffects;
    QString m_pluginSubDirectory;
};

class KWIN_EXPORT effect_loader : public basic_effect_loader
{
public:
    template<typename Platform>
    effect_loader(EffectsHandler& effects, Platform& platform)
    {
        m_loaders.emplace_back(
            std::make_unique<scripting::effect_loader<Platform>>(effects, platform));
        m_loaders.emplace_back(std::make_unique<plugin_effect_loader>());

        for (auto&& loader : m_loaders) {
            connect(loader.get(),
                    &basic_effect_loader::effectLoaded,
                    this,
                    &basic_effect_loader::effectLoaded);
        }
    }

    ~effect_loader() override;
    bool hasEffect(const QString& name) const override;
    bool isEffectSupported(const QString& name) const override;
    QStringList listOfKnownEffects() const override;
    bool loadEffect(const QString& name) override;
    void queryAndLoadAll() override;
    void setConfig(KSharedConfig::Ptr config) override;
    void clear() override;

private:
    std::vector<std::unique_ptr<basic_effect_loader>> m_loaders;
};

}
}
