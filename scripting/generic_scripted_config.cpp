/*
 *  KWin - the KDE window manager
 *  This file is part of the KDE project.
 *
 * Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "generic_scripted_config.h"

#include "config-kwin.h"
#include <KAboutData>
#include <kwineffects_interface.h>
#define TRANSLATION_DOMAIN "kwin_scripting"
#include <KLocalizedString>
#include <KLocalizedTranslator>
#include <kconfigloader.h>

#include <QFile>
#include <QLabel>
#include <QStandardPaths>
#include <QUiLoader>
#include <QVBoxLayout>

namespace KWin::scripting
{

QObject* generic_scripted_config_factory::create(const char* iface,
                                                 QWidget* parentWidget,
                                                 QObject* parent,
                                                 const QVariantList& args,
                                                 const QString& keyword)
{
    Q_UNUSED(iface)
    Q_UNUSED(parent)
    Q_UNUSED(keyword)

    // the plugin id is in the args when created by desktop effects kcm or EffectsModel in general
    auto pluginId = args.isEmpty() ? QString() : args.first().toString();

    // If we do not get the id of the effect we want to load from the args, we have to check our
    // metadata. This can be the case if the factory gets loaded from a KPluginSelector the plugin
    // id is in plugin factory metadata when created by scripts kcm (because it uses
    // kpluginselector, which doesn't pass the plugin id as the first arg), can be dropped once the
    // scripts kcm is ported to qtquick (because then we could pass the plugin id via the args)
    if (pluginId.isEmpty()) {
        pluginId = metaData().pluginId();
    }
    if (pluginId.startsWith(QLatin1String("kwin4_effect_"))) {
        return new scripted_effect_config(pluginId, parentWidget, args);
    } else {
        return new scripting_config(pluginId, parentWidget, args);
    }
}

generic_scripted_config::generic_scripted_config(const QString& keyword,
                                                 QWidget* parent,
                                                 const QVariantList& args)
    : KCModule(parent, args)
    , m_packageName(keyword)
    , m_translator(new KLocalizedTranslator(this))
{
    QCoreApplication::instance()->installTranslator(m_translator);
}

generic_scripted_config::~generic_scripted_config()
{
}

void generic_scripted_config::createUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    const QString packageRoot = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QLatin1String(KWIN_NAME) + QLatin1Char('/') + typeName() + QLatin1Char('/') + m_packageName,
        QStandardPaths::LocateDirectory);
    if (packageRoot.isEmpty()) {
        layout->addWidget(new QLabel(i18nc("Error message", "Could not locate package metadata")));
        return;
    }

    KPluginMetaData metaData(packageRoot + QLatin1String("/metadata.json"));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (!metaData.isValid()) {
        metaData
            = KPluginMetaData::fromDesktopFile(packageRoot + QLatin1String("/metadata.desktop"));
        if (metaData.isValid()) {
            qWarning("metadata.desktop format is obsolete. Please convert %s to JSON metadata",
                     qPrintable(metaData.fileName()));
        }
    }
#endif
    if (!metaData.isValid()) {
        layout->addWidget(new QLabel(i18nc("Required file does not exist",
                                           "%1 does not contain a valid metadata.json file",
                                           qPrintable(packageRoot))));
        return;
    }

    const QString kconfigXTFile = packageRoot + QLatin1String("/contents/config/main.xml");
    if (!QFileInfo::exists(kconfigXTFile)) {
        layout->addWidget(new QLabel(
            i18nc("Required file does not exist", "%1 does not exist", qPrintable(kconfigXTFile))));
        return;
    }

    const QString uiPath = packageRoot + QLatin1String("/contents/ui/config.ui");
    if (!QFileInfo::exists(uiPath)) {
        layout->addWidget(new QLabel(
            i18nc("Required file does not exist", "%1 does not exist", qPrintable(uiPath))));
        return;
    }

    QFile xmlFile(kconfigXTFile);
    KConfigGroup cg = configGroup();
    KConfigLoader* configLoader = new KConfigLoader(cg, &xmlFile, this);
    // load the ui file
    QUiLoader* loader = new QUiLoader(this);
    loader->setLanguageChangeEnabled(true);
    QFile uiFile(uiPath);
    m_translator->setTranslationDomain(metaData.value("X-KWin-Config-TranslationDomain"));

    uiFile.open(QFile::ReadOnly);
    QWidget* customConfigForm = loader->load(&uiFile, this);
    m_translator->addContextToMonitor(customConfigForm->objectName());
    uiFile.close();

    // send a custom event to the translator to retranslate using our translator
    QEvent le(QEvent::LanguageChange);
    QCoreApplication::sendEvent(customConfigForm, &le);

    layout->addWidget(customConfigForm);
    addConfig(configLoader, customConfigForm);
}

void generic_scripted_config::save()
{
    KCModule::save();
    reload();
}

void generic_scripted_config::reload()
{
}

scripted_effect_config::scripted_effect_config(const QString& keyword,
                                               QWidget* parent,
                                               const QVariantList& args)
    : generic_scripted_config(keyword, parent, args)
{
    createUi();
}

scripted_effect_config::~scripted_effect_config()
{
}

QString scripted_effect_config::typeName() const
{
    return QStringLiteral("effects");
}

KConfigGroup scripted_effect_config::configGroup()
{
    return KSharedConfig::openConfig(QStringLiteral(KWIN_CONFIG))
        ->group(QLatin1String("Effect-") + packageName());
}

void scripted_effect_config::reload()
{
    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(packageName());
}

scripting_config::scripting_config(const QString& keyword,
                                   QWidget* parent,
                                   const QVariantList& args)
    : generic_scripted_config(keyword, parent, args)
{
    createUi();
}

scripting_config::~scripting_config()
{
}

KConfigGroup scripting_config::configGroup()
{
    return KSharedConfig::openConfig(QStringLiteral(KWIN_CONFIG))
        ->group(QLatin1String("Script-") + packageName());
}

QString scripting_config::typeName() const
{
    return QStringLiteral("scripts");
}

void scripting_config::reload()
{
    // TODO: what to call
}

}
