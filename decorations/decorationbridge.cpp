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
#include "decorationbridge.h"

#include "decoratedclient.h"
#include "decorationrenderer.h"
#include "decorations_logging.h"
#include "settings.h"
#include "window.h"

#include "config-kwin.h"
#include "main.h"
#include "render/scene.h"
#include "toplevel.h"
#include "win/control.h"
#include "win/deco.h"
#include "win/space.h"

#include <kwineffects/effect_plugin_factory.h>

#include <KDecoration2/Decoration>
#include <KDecoration2/DecoratedClient>
#include <KDecoration2/DecorationSettings>
#include <KPluginMetaData>
#include <QMetaProperty>
#include <QPainter>

namespace KWin
{
namespace Decoration
{

static const QString s_aurorae = QStringLiteral("org.kde.kwin.aurorae");
static const QString s_pluginName = QStringLiteral("org.kde.kdecoration2");
#if HAVE_BREEZE_DECO
static const QString s_defaultPlugin = QStringLiteral(BREEZE_KDECORATION_PLUGIN_ID);
#else
static const QString s_defaultPlugin = s_aurorae;
#endif

KWIN_SINGLETON_FACTORY(DecorationBridge)

DecorationBridge::DecorationBridge(QObject *parent)
    : KDecoration2::DecorationBridge(parent)
    , m_factory(nullptr)
    , m_showToolTips(false)
    , m_settings()
    , m_noPlugin(false)
{
    readDecorationOptions();
}

DecorationBridge::~DecorationBridge()
{
    s_self = nullptr;
}

QString DecorationBridge::readPlugin()
{
    return kwinApp()->config()->group(s_pluginName).readEntry("library", s_defaultPlugin);
}

static bool readNoPlugin()
{
    return kwinApp()->config()->group(s_pluginName).readEntry("NoPlugin", false);
}

QString DecorationBridge::readTheme() const
{
    return kwinApp()->config()->group(s_pluginName).readEntry("theme", m_defaultTheme);
}

void DecorationBridge::readDecorationOptions()
{
    m_showToolTips = kwinApp()->config()->group(s_pluginName).readEntry("ShowToolTips", true);
}

bool DecorationBridge::hasPlugin()
{
    const DecorationBridge *bridge = DecorationBridge::self();
    if (!bridge) {
        return false;
    }
    return !bridge->m_noPlugin && bridge->m_factory;
}

void DecorationBridge::init()
{
    using namespace Wrapland::Server;
    m_noPlugin = readNoPlugin();
    if (m_noPlugin) {
        return;
    }
    m_plugin = readPlugin();
    m_settings = QSharedPointer<KDecoration2::DecorationSettings>::create(this);
    initPlugin();
    if (!m_factory) {
        if (m_plugin != s_defaultPlugin) {
            // try loading default plugin
            m_plugin = s_defaultPlugin;
            initPlugin();
        }
        // default plugin failed to load, try fallback
        if (!m_factory) {
            m_plugin = s_aurorae;
            initPlugin();
        }
    }
}

void DecorationBridge::initPlugin()
{
    const KPluginMetaData metaData = KPluginMetaData::findPluginById(s_pluginName, m_plugin);
    if (!metaData.isValid()) {
        qCWarning(KWIN_DECORATIONS) << "Could not locate decoration plugin" << m_plugin;
        return;
    }
    qCDebug(KWIN_DECORATIONS) << "Trying to load decoration plugin: " << metaData.fileName();
    auto factoryResult = KPluginFactory::loadFactory(metaData);
    if (!factoryResult) {
        qCWarning(KWIN_DECORATIONS) << "Error loading plugin:" << factoryResult.errorText;
    } else {
        m_factory = factoryResult.plugin;
        loadMetaData(metaData.rawData());
    }
}

static void recreateDecorations()
{
    workspace()->forEachAbstractClient([](Toplevel* t) { t->updateDecoration(true, true); });
}

void DecorationBridge::reconfigure()
{
    readDecorationOptions();

    if (m_noPlugin != readNoPlugin()) {
        m_noPlugin = !m_noPlugin;
        // no plugin setting changed
        if (m_noPlugin) {
            // decorations disabled now
            m_plugin = QString();
            delete m_factory;
            m_factory = nullptr;
            m_settings.clear();
        } else {
            // decorations enabled now
            init();
        }
        recreateDecorations();
        return;
    }

    const QString newPlugin = readPlugin();
    if (newPlugin != m_plugin) {
        // plugin changed, recreate everything
        auto oldFactory = m_factory;
        const auto oldPluginName = m_plugin;
        m_plugin = newPlugin;
        initPlugin();
        if (m_factory == oldFactory) {
            // loading new plugin failed
            m_factory = oldFactory;
            m_plugin = oldPluginName;
        } else {
            recreateDecorations();
            // TODO: unload and destroy old plugin
        }
    } else {
        // same plugin, but theme might have changed
        const QString oldTheme = m_theme;
        m_theme = readTheme();
        if (m_theme != oldTheme) {
            recreateDecorations();
        }
    }
}

void DecorationBridge::loadMetaData(const QJsonObject &object)
{
    // reset all settings
    m_recommendedBorderSize = QString();
    m_theme = QString();
    m_defaultTheme = QString();

    // load the settings
    const QJsonValue decoSettings = object.value(s_pluginName);
    if (decoSettings.isUndefined()) {
        // no settings
        return;
    }
    const QVariantMap decoSettingsMap = decoSettings.toObject().toVariantMap();
    auto recBorderSizeIt = decoSettingsMap.find(QStringLiteral("recommendedBorderSize"));
    if (recBorderSizeIt != decoSettingsMap.end()) {
        m_recommendedBorderSize = recBorderSizeIt.value().toString();
    }
    findTheme(decoSettingsMap);

    Q_EMIT metaDataLoaded();
}

void DecorationBridge::findTheme(const QVariantMap &map)
{
    auto it = map.find(QStringLiteral("themes"));
    if (it == map.end()) {
        return;
    }
    if (!it.value().toBool()) {
        return;
    }
    it = map.find(QStringLiteral("defaultTheme"));
    m_defaultTheme = it != map.end() ? it.value().toString() : QString();
    m_theme = readTheme();
}

std::unique_ptr<KDecoration2::DecoratedClientPrivate> DecorationBridge::createClient(KDecoration2::DecoratedClient *client, KDecoration2::Decoration *decoration)
{
    return std::make_unique<DecoratedClientImpl>(static_cast<window*>(decoration->parent())->win,
                                                 client, decoration);
}

std::unique_ptr<KDecoration2::DecorationSettingsPrivate> DecorationBridge::settings(KDecoration2::DecorationSettings *parent)
{
    return std::unique_ptr<SettingsImpl>(new SettingsImpl(parent));
}

KDecoration2::Decoration *DecorationBridge::createDecoration(window* window)
{
    if (m_noPlugin) {
        return nullptr;
    }
    if (!m_factory) {
        return nullptr;
    }
    QVariantMap args({ {QStringLiteral("bridge"), QVariant::fromValue(this)} });

    if (!m_theme.isEmpty()) {
        args.insert(QStringLiteral("theme"), m_theme);
    }
    auto deco = m_factory->create<KDecoration2::Decoration>(window, QVariantList({args}));
    deco->setSettings(m_settings);
    deco->init();
    return deco;
}

static
QString settingsProperty(const QVariant &variant)
{
    if (QLatin1String(variant.typeName()) == QLatin1String("KDecoration2::BorderSize")) {
        return QString::number(variant.toInt());
    } else if (QLatin1String(variant.typeName()) == QLatin1String("QVector<KDecoration2::DecorationButtonType>")) {
        const auto &b = variant.value<QVector<KDecoration2::DecorationButtonType>>();
        QString buffer;
        for (auto it = b.begin(); it != b.end(); ++it) {
            if (it != b.begin()) {
                buffer.append(QStringLiteral(", "));
            }
            buffer.append(QString::number(int(*it)));
        }
        return buffer;
    }
    return variant.toString();
}

QString DecorationBridge::supportInformation() const
{
    QString b;
    if (m_noPlugin) {
        b.append(QStringLiteral("Decorations are disabled"));
    } else {
        b.append(QStringLiteral("Plugin: %1\n").arg(m_plugin));
        b.append(QStringLiteral("Theme: %1\n").arg(m_theme));
        b.append(QStringLiteral("Plugin recommends border size: %1\n").arg(m_recommendedBorderSize.isNull() ? "No" : m_recommendedBorderSize));
        const QMetaObject *metaOptions = m_settings->metaObject();
        for (int i=0; i<metaOptions->propertyCount(); ++i) {
            const QMetaProperty property = metaOptions->property(i);
            if (QLatin1String(property.name()) == QLatin1String("objectName")) {
                continue;
            }
            b.append(QStringLiteral("%1: %2\n").arg(property.name()).arg(settingsProperty(m_settings->property(property.name()))));
        }
    }
    return b;
}

} // Decoration
} // KWin
