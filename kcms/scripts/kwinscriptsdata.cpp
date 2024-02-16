/*
    SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwinscriptsdata.h"

#include <KConfigGroup>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KPackage/PackageStructure>
#include <KPluginFactory>

KWinScriptsData::KWinScriptsData(QObject* parent)
    : KCModuleData(parent)
    , m_kwinConfig(KSharedConfig::openConfig("kwinrc"))
{
}

QVector<KPluginMetaData> KWinScriptsData::pluginMetaDataList() const
{
    const QString scriptFolder = QStringLiteral("kwin/scripts/");
    return KPackage::PackageLoader::self()->findPackages(QStringLiteral("KWin/Script"),
                                                         scriptFolder);
}

bool KWinScriptsData::isDefaults() const
{
    auto plugins = pluginMetaDataList();
    KConfigGroup cfgGroup(m_kwinConfig, QStringLiteral("Plugins"));
    for (auto& plugin : plugins) {
        if (cfgGroup.readEntry(plugin.pluginId() + QLatin1String("Enabled"),
                               plugin.isEnabledByDefault())
            != plugin.isEnabledByDefault()) {
            return false;
        }
    }

    return true;
}
