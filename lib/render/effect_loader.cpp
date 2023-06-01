/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect_loader.h"

#include "base/logging.h"
#include "kwineffects/effect.h"
#include "kwineffects/effect_plugin_factory.h"
#include "kwineffects/effects_handler.h"
#include <kwinconfig.h>

#include <QPluginLoader>
#include <QStringList>

namespace KWin::render
{

plugin_effect_loader::plugin_effect_loader(KSharedConfig::Ptr config)
    : basic_effect_loader(config)
    , m_pluginSubDirectory(QStringLiteral("kwin/effects/plugins"))
{
}

plugin_effect_loader::~plugin_effect_loader()
{
}

bool plugin_effect_loader::hasEffect(const QString& name) const
{
    const auto info = findEffect(name);
    return info.isValid();
}

KPluginMetaData plugin_effect_loader::findEffect(const QString& name) const
{
    const auto plugins
        = KPluginMetaData::findPlugins(m_pluginSubDirectory, [name](const KPluginMetaData& data) {
              return data.pluginId().compare(name, Qt::CaseInsensitive) == 0;
          });
    if (plugins.isEmpty()) {
        return KPluginMetaData();
    }
    return plugins.first();
}

bool plugin_effect_loader::isEffectSupported(const QString& name) const
{
    if (EffectPluginFactory* effectFactory = factory(findEffect(name))) {
        return effectFactory->isSupported();
    }
    return false;
}

EffectPluginFactory* plugin_effect_loader::factory(const KPluginMetaData& info) const
{
    if (!info.isValid()) {
        return nullptr;
    }
    KPluginFactory* factory;
    if (info.isStaticPlugin()) {
        // in case of static plugins we don't need to worry about the versions, because
        // they are shipped as part of the kwin executables
        factory = KPluginFactory::loadFactory(info).plugin;
    } else {
        QPluginLoader loader(info.fileName());
        if (loader.metaData().value("IID").toString() != EffectPluginFactory_iid) {
            qCDebug(KWIN_CORE) << info.pluginId() << " has not matching plugin version, expected "
                               << EffectPluginFactory_iid << "got "
                               << loader.metaData().value("IID");
            return nullptr;
        }
        factory = qobject_cast<KPluginFactory*>(loader.instance());
    }
    if (!factory) {
        qCDebug(KWIN_CORE) << "Did not get KPluginFactory for " << info.pluginId();
        return nullptr;
    }
    return dynamic_cast<EffectPluginFactory*>(factory);
}

QStringList plugin_effect_loader::listOfKnownEffects() const
{
    const auto plugins = findAllEffects();
    QStringList result;
    for (const auto& plugin : plugins) {
        result << plugin.pluginId();
    }
    qCDebug(KWIN_CORE) << result;
    return result;
}

bool plugin_effect_loader::loadEffect(const QString& name)
{
    const auto info = findEffect(name);
    if (!info.isValid()) {
        return false;
    }
    return loadEffect(info, load_effect_flags::load);
}

bool plugin_effect_loader::loadEffect(const KPluginMetaData& info, load_effect_flags load_flags)
{
    if (!info.isValid()) {
        qCDebug(KWIN_CORE) << "Plugin info is not valid";
        return false;
    }
    const QString name = info.pluginId();
    if (!(load_flags & load_effect_flags::load)) {
        qCDebug(KWIN_CORE) << "Loading flags disable effect: " << name;
        return false;
    }
    if (m_loadedEffects.contains(name)) {
        qCDebug(KWIN_CORE) << name << " already loaded";
        return false;
    }
    EffectPluginFactory* effectFactory = factory(info);
    if (!effectFactory) {
        qCDebug(KWIN_CORE) << "Couldn't get an EffectPluginFactory for: " << name;
        return false;
    }

    effects->makeOpenGLContextCurrent();
    if (!effectFactory->isSupported()) {
        qCDebug(KWIN_CORE) << "Effect is not supported: " << name;
        return false;
    }

    if (flags(load_flags & load_effect_flags::check_default_function)) {
        if (!effectFactory->enabledByDefault()) {
            qCDebug(KWIN_CORE) << "Enabled by default function disables effect: " << name;
            return false;
        }
    }

    // ok, now we can try to create the Effect
    Effect* e = effectFactory->createEffect();
    if (!e) {
        qCDebug(KWIN_CORE) << "Failed to create effect: " << name;
        return false;
    }
    // insert in our loaded effects
    m_loadedEffects << name;
    connect(e, &Effect::destroyed, this, [this, name]() { m_loadedEffects.removeAll(name); });
    qCDebug(KWIN_CORE) << "Successfully loaded plugin effect: " << name;
    Q_EMIT effectLoaded(e, name);
    return true;
}

void plugin_effect_loader::queryAndLoadAll()
{
    auto const effects = findAllEffects();
    for (auto const& effect : effects) {
        auto const load_flags = readConfig(effect.pluginId(), effect.isEnabledByDefault());
        if (flags(load_flags & load_effect_flags::load)) {
            loadEffect(effect, load_flags);
        }
    }
}

QVector<KPluginMetaData> plugin_effect_loader::findAllEffects() const
{
    return KPluginMetaData::findPlugins(m_pluginSubDirectory);
}

void plugin_effect_loader::setPluginSubDirectory(const QString& directory)
{
    m_pluginSubDirectory = directory;
}

void plugin_effect_loader::clear()
{
}

effect_loader::~effect_loader()
{
}

void effect_loader::add_loader(std::unique_ptr<basic_effect_loader> loader)
{
    connect(
        loader.get(), &basic_effect_loader::effectLoaded, this, &basic_effect_loader::effectLoaded);
    m_loaders.push_back(std::move(loader));
}

bool effect_loader::hasEffect(QString const& name) const
{
    return std::any_of(m_loaders.cbegin(), m_loaders.cend(), [&name](auto const& loader) {
        return loader->hasEffect(name);
    });
}

bool effect_loader::isEffectSupported(QString const& name) const
{
    return std::any_of(m_loaders.cbegin(), m_loaders.cend(), [&name](auto const& loader) {
        return loader->isEffectSupported(name);
    });
}

QStringList effect_loader::listOfKnownEffects() const
{
    QStringList result;
    for (auto&& loader : m_loaders) {
        result << loader->listOfKnownEffects();
    }
    return result;
}

bool effect_loader::loadEffect(const QString& name)
{
    for (auto&& loader : m_loaders) {
        if (loader->loadEffect(name)) {
            return true;
        }
    }
    return false;
}

void effect_loader::queryAndLoadAll()
{
    for (auto&& loader : m_loaders) {
        loader->queryAndLoadAll();
    }
}

void effect_loader::clear()
{
    for (auto&& loader : m_loaders) {
        loader->clear();
    }
}

}
