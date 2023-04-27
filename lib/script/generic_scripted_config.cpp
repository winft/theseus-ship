/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "generic_scripted_config.h"

#include "config-kwin.h"
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
                                                 QObject* /*parent*/,
                                                 const QVariantList& args)
{
    if (qstrcmp(iface, "KCModule") == 0) {
        if (args.count() < 2) {
            qWarning() << Q_FUNC_INFO << "expects two arguments (plugin id, package type)";
            return nullptr;
        }

        const QString pluginId = args.at(0).toString();
        const QString packageType = args.at(1).toString();

        if (packageType == QLatin1StringView("KWin/Effect")) {
            return new scripted_effect_config(pluginId, parentWidget, args);
        } else if (packageType == QLatin1StringView("KWin/Script")) {
            return new scripting_config(pluginId, parentWidget, args);
        } else {
            qWarning() << Q_FUNC_INFO << "got unknown package type:" << packageType;
        }
    }

    return nullptr;
}

generic_scripted_config::generic_scripted_config(const QString& keyword,
                                                 QWidget* parent,
                                                 const QVariantList& args)
    : KCModule(parent, KPluginMetaData(), args)
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
    QVBoxLayout* layout = new QVBoxLayout(widget());

    const QString packageRoot = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QLatin1String(KWIN_NAME) + QLatin1Char('/') + typeName() + QLatin1Char('/') + m_packageName,
        QStandardPaths::LocateDirectory);
    if (packageRoot.isEmpty()) {
        layout->addWidget(new QLabel(i18nc("Error message", "Could not locate package metadata")));
        return;
    }

    auto const metaData
        = KPluginMetaData::fromJsonFile(packageRoot + QLatin1String("/metadata.json"));
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

    QString const localePath = packageRoot + QLatin1String("/contents/locale");
    if (QFileInfo::exists(localePath)) {
        KLocalizedString::addDomainLocaleDir(
            metaData.value("X-KWin-Config-TranslationDomain").toUtf8(), localePath);
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
    QWidget* customConfigForm = loader->load(&uiFile, widget());
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
