/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

#include "bridge_qobject.h"
#include "client_impl.h"
#include "decorations_logging.h"
#include "renderer.h"
#include "settings.h"
#include "window.h"

#include "render/scene.h"
#include "win/deco.h"

#include <kwineffects/effect_plugin_factory.h>

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>
#include <KDecoration2/Private/DecorationBridge>
#include <KPluginMetaData>
#include <QMetaProperty>

namespace KDecoration2
{
class DecorationSettings;
}

namespace KWin::win::deco
{

static const QString s_aurorae = QStringLiteral("org.kde.kwin.aurorae");
static const QString s_pluginName = QStringLiteral("org.kde.kdecoration2");

#if HAVE_BREEZE_DECO
static const QString s_defaultPlugin = QStringLiteral(BREEZE_KDECORATION_PLUGIN_ID);
#else
static const QString s_defaultPlugin = s_aurorae;
#endif

template<typename Space>
class bridge : public KDecoration2::DecorationBridge
{
public:
    bridge(Space& space)
        : qobject{std::make_unique<bridge_qobject>()}
        , m_factory(nullptr)
        , m_showToolTips(false)
        , m_settings()
        , space{space}
    {
        readDecorationOptions();
    }

    ~bridge() override = default;

    bool hasPlugin()
    {
        return !m_noPlugin && m_factory;
    }

    void init()
    {
        m_noPlugin = readNoPlugin();
        if (m_noPlugin) {
            return;
        }
        m_plugin = readPlugin();
        m_settings = std::make_shared<KDecoration2::DecorationSettings>(this);
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

    template<typename Win>
    KDecoration2::Decoration* createDecoration(deco::window<Win>* window)
    {
        if (m_noPlugin) {
            return nullptr;
        }
        if (!m_factory) {
            return nullptr;
        }
        QVariantMap args({{QStringLiteral("bridge"), QVariant::fromValue(this)}});

        if (!m_theme.isEmpty()) {
            args.insert(QStringLiteral("theme"), m_theme);
        }
        auto deco = m_factory->create<KDecoration2::Decoration>(window, QVariantList({args}));
        deco->setSettings(m_settings);
        deco->init();
        return deco;
    }

    std::unique_ptr<KDecoration2::DecoratedClientPrivate>
    createClient(KDecoration2::DecoratedClient* client,
                 KDecoration2::Decoration* decoration) override
    {
        using window_t = typename Space::window_t;

        return std::visit(
            overload{[&](auto win) -> std::unique_ptr<KDecoration2::DecoratedClientPrivate> {
                using win_t = std::remove_pointer_t<decltype(win)>;
                return std::make_unique<client_impl<win_t>>(win, client, decoration);
            }},
            static_cast<window<window_t>*>(decoration->parent())->win);
    }

    std::unique_ptr<KDecoration2::DecorationSettingsPrivate>
    settings(KDecoration2::DecorationSettings* parent) override
    {
        return std::make_unique<deco::settings<Space>>(space, parent);
    }

    QString recommendedBorderSize() const
    {
        return m_recommendedBorderSize;
    }

    bool showToolTips() const
    {
        return m_showToolTips;
    }

    void reconfigure()
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
                m_settings.reset();
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

    std::shared_ptr<KDecoration2::DecorationSettings> const& settings() const
    {
        return m_settings;
    }

    QString supportInformation() const
    {
        QString b;
        if (m_noPlugin) {
            b.append(QStringLiteral("Decorations are disabled"));
        } else {
            b.append(QStringLiteral("Plugin: %1\n").arg(m_plugin));
            b.append(QStringLiteral("Theme: %1\n").arg(m_theme));
            b.append(QStringLiteral("Plugin recommends border size: %1\n")
                         .arg(m_recommendedBorderSize.isNull() ? "No" : m_recommendedBorderSize));
            const QMetaObject* metaOptions = m_settings->metaObject();
            for (int i = 0; i < metaOptions->propertyCount(); ++i) {
                const QMetaProperty property = metaOptions->property(i);
                if (QLatin1String(property.name()) == QLatin1String("objectName")) {
                    continue;
                }
                b.append(QStringLiteral("%1: %2\n")
                             .arg(property.name())
                             .arg(settingsProperty(m_settings->property(property.name()))));
            }
        }
        return b;
    }

    std::unique_ptr<bridge_qobject> qobject;

private:
    QString readPlugin()
    {
        return space.base.config.main->group(s_pluginName).readEntry("library", s_defaultPlugin);
    }

    bool readNoPlugin()
    {
        return space.base.config.main->group(s_pluginName).readEntry("NoPlugin", false);
    }

    QString readTheme() const
    {
        return space.base.config.main->group(s_pluginName).readEntry("theme", m_defaultTheme);
    }

    void readDecorationOptions()
    {
        m_showToolTips
            = space.base.config.main->group(s_pluginName).readEntry("ShowToolTips", true);
    }

    void loadMetaData(const QJsonObject& object)
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

        Q_EMIT qobject->metaDataLoaded();
    }

    void findTheme(const QVariantMap& map)
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

    void initPlugin()
    {
        const KPluginMetaData metaData = KPluginMetaData::findPluginById(s_pluginName, m_plugin);
        if (!metaData.isValid()) {
            qCWarning(KWIN_CORE) << "Could not locate decoration plugin" << m_plugin;
            return;
        }
        qCDebug(KWIN_CORE) << "Trying to load decoration plugin: " << metaData.fileName();
        auto factoryResult = KPluginFactory::loadFactory(metaData);
        if (!factoryResult) {
            qCWarning(KWIN_CORE) << "Error loading plugin:" << factoryResult.errorText;
        } else {
            m_factory = factoryResult.plugin;
            loadMetaData(metaData.rawData());
        }
    }

    void recreateDecorations()
    {
        for (auto win : space.windows) {
            std::visit(overload{[&](auto&& win) {
                           if (win->control) {
                               win->updateDecoration(true, true);
                           }
                       }},
                       win);
        }
    }

    static QString settingsProperty(const QVariant& variant)
    {
        if (QLatin1String(variant.typeName()) == QLatin1String("KDecoration2::BorderSize")) {
            return QString::number(variant.toInt());
        } else if (QLatin1String(variant.typeName())
                   == QLatin1String("QVector<KDecoration2::DecorationButtonType>")) {
            const auto& b = variant.value<QVector<KDecoration2::DecorationButtonType>>();
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

    KPluginFactory* m_factory{nullptr};
    bool m_showToolTips{false};
    QString m_recommendedBorderSize;
    QString m_plugin;
    QString m_defaultTheme;
    QString m_theme;
    std::shared_ptr<KDecoration2::DecorationSettings> m_settings;
    bool m_noPlugin{false};
    Space& space;
};

}
