/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

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
#include "effect_loader.h"

#include "base/logging.h"
#include "scripting/effect.h"

#include "config-kwin.h"
#include "kwineffects.h"

#include <KConfigGroup>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <QFutureWatcher>
#include <QMap>
#include <QStringList>
#include <QtConcurrentRun>

namespace KWin::render
{

basic_effect_loader::basic_effect_loader(QObject* parent)
    : QObject(parent)
{
}

basic_effect_loader::~basic_effect_loader()
{
}

void basic_effect_loader::setConfig(KSharedConfig::Ptr config)
{
    m_config = config;
}

load_effect_flags basic_effect_loader::readConfig(const QString& effectName,
                                                  bool defaultValue) const
{
    Q_ASSERT(m_config);
    KConfigGroup plugins(m_config, QStringLiteral("Plugins"));

    const QString key = effectName + QStringLiteral("Enabled");

    // do we have a key for the effect?
    if (plugins.hasKey(key)) {
        // we have a key in the config, so read the enabled state
        const bool load = plugins.readEntry(key, defaultValue);
        return load ? load_effect_flags::load : load_effect_flags();
    }
    // we don't have a key, so we just use the enabled by default value
    if (defaultValue) {
        return load_effect_flags::load | load_effect_flags::check_default_function;
    }
    return load_effect_flags();
}

static const QString s_nameProperty = QStringLiteral("X-KDE-PluginInfo-Name");
static const QString s_jsConstraint = QStringLiteral("[X-Plasma-API] == 'javascript'");
static const QString s_serviceType = QStringLiteral("KWin/Effect");

scripted_effect_loader::scripted_effect_loader(QObject* parent)
    : basic_effect_loader(parent)
    , m_queue(new effect_load_queue<scripted_effect_loader, KPluginMetaData>(this))
{
}

scripted_effect_loader::~scripted_effect_loader()
{
}

bool scripted_effect_loader::hasEffect(const QString& name) const
{
    return findEffect(name).isValid();
}

bool scripted_effect_loader::isEffectSupported(const QString& name) const
{
    // scripted effects are in general supported
    if (!scripting::effect::supported()) {
        return false;
    }
    return hasEffect(name);
}

QStringList scripted_effect_loader::listOfKnownEffects() const
{
    const auto effects = findAllEffects();
    QStringList result;
    for (const auto& service : effects) {
        result << service.pluginId();
    }
    return result;
}

bool scripted_effect_loader::loadEffect(const QString& name)
{
    auto effect = findEffect(name);
    if (!effect.isValid()) {
        return false;
    }
    return loadEffect(effect, load_effect_flags::load);
}

bool scripted_effect_loader::loadEffect(const KPluginMetaData& effect, load_effect_flags flags)
{
    const QString name = effect.pluginId();
    if (!(flags & load_effect_flags::load)) {
        qCDebug(KWIN_CORE) << "Loading flags disable effect: " << name;
        return false;
    }
    if (m_loadedEffects.contains(name)) {
        qCDebug(KWIN_CORE) << name << "already loaded";
        return false;
    }

    if (!scripting::effect::supported()) {
        qCDebug(KWIN_CORE) << "Effect is not supported: " << name;
        return false;
    }

    auto e = scripting::effect::create(effect);
    if (!e) {
        qCDebug(KWIN_CORE) << "Could not initialize scripted effect: " << name;
        return false;
    }
    connect(e, &scripting::effect::destroyed, this, [this, name]() {
        m_loadedEffects.removeAll(name);
    });

    qCDebug(KWIN_CORE) << "Successfully loaded scripted effect: " << name;
    Q_EMIT effectLoaded(e, name);
    m_loadedEffects << name;
    return true;
}

void scripted_effect_loader::queryAndLoadAll()
{
    if (m_queryConnection) {
        return;
    }
    // perform querying for the services in a thread
    QFutureWatcher<QList<KPluginMetaData>>* watcher
        = new QFutureWatcher<QList<KPluginMetaData>>(this);
    m_queryConnection = connect(
        watcher,
        &QFutureWatcher<QList<KPluginMetaData>>::finished,
        this,
        [this, watcher]() {
            const auto effects = watcher->result();
            for (auto effect : effects) {
                auto const load_flags = readConfig(effect.pluginId(), effect.isEnabledByDefault());
                if (flags(load_flags & load_effect_flags::load)) {
                    m_queue->enqueue(qMakePair(effect, load_flags));
                }
            }
            watcher->deleteLater();
            m_queryConnection = QMetaObject::Connection();
        },
        Qt::QueuedConnection);
    watcher->setFuture(QtConcurrent::run(this, &scripted_effect_loader::findAllEffects));
}

QList<KPluginMetaData> scripted_effect_loader::findAllEffects() const
{
    return KPackage::PackageLoader::self()->listPackages(s_serviceType,
                                                         QStringLiteral("kwin/effects"));
}

KPluginMetaData scripted_effect_loader::findEffect(const QString& name) const
{
    const auto plugins = KPackage::PackageLoader::self()->findPackages(
        s_serviceType, QStringLiteral("kwin/effects"), [name](const KPluginMetaData& metadata) {
            return metadata.pluginId().compare(name, Qt::CaseInsensitive) == 0;
        });
    if (!plugins.isEmpty()) {
        return plugins.first();
    }
    return KPluginMetaData();
}

void scripted_effect_loader::clear()
{
    disconnect(m_queryConnection);
    m_queryConnection = QMetaObject::Connection();
    m_queue->clear();
}

plugin_effect_loader::plugin_effect_loader(QObject* parent)
    : basic_effect_loader(parent)
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

effect_loader::effect_loader(QObject* parent)
    : basic_effect_loader(parent)
{
    m_loaders << new scripted_effect_loader(this) << new plugin_effect_loader(this);
    for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
        connect(*it, &basic_effect_loader::effectLoaded, this, &basic_effect_loader::effectLoaded);
    }
}

effect_loader::~effect_loader()
{
}

#define BOOL_MERGE(method)                                                                         \
    bool effect_loader::method(const QString& name) const                                          \
    {                                                                                              \
        for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {                 \
            if ((*it)->method(name)) {                                                             \
                return true;                                                                       \
            }                                                                                      \
        }                                                                                          \
        return false;                                                                              \
    }

BOOL_MERGE(hasEffect)
BOOL_MERGE(isEffectSupported)

#undef BOOL_MERGE

QStringList effect_loader::listOfKnownEffects() const
{
    QStringList result;
    for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
        result << (*it)->listOfKnownEffects();
    }
    return result;
}

bool effect_loader::loadEffect(const QString& name)
{
    for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
        if ((*it)->loadEffect(name)) {
            return true;
        }
    }
    return false;
}

void effect_loader::queryAndLoadAll()
{
    for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
        (*it)->queryAndLoadAll();
    }
}

void effect_loader::setConfig(KSharedConfig::Ptr config)
{
    basic_effect_loader::setConfig(config);
    for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
        (*it)->setConfig(config);
    }
}

void effect_loader::clear()
{
    for (auto it = m_loaders.constBegin(); it != m_loaders.constEnd(); ++it) {
        (*it)->clear();
    }
}

}
