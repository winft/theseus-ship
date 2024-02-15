/*
    SPDX-FileCopyrightText: 2018 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "virtualdesktops.h"
#include "animationsmodel.h"
#include "desktopsmodel.h"
#include "virtualdesktopssettings.h"
#include "virtualdesktopsdata.h"

#include <KAboutApplicationDialog>
#include <KAboutData>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <QDBusConnection>
#include <QDBusMessage>


K_PLUGIN_FACTORY_WITH_JSON(VirtualDesktopsFactory,
                           "kcm_kwin_virtualdesktops.json",
                           registerPlugin<theseus_ship::VirtualDesktops>();
                           registerPlugin<theseus_ship::VirtualDesktopsData>();)

namespace theseus_ship
{

VirtualDesktops::VirtualDesktops(QObject *parent, const KPluginMetaData &metaData)
    : KQuickManagedConfigModule(parent, metaData)
    , m_data(new VirtualDesktopsData(this))
{
    qmlRegisterAnonymousType<VirtualDesktopsSettings>("org.kde.kwin.kcm.desktop", 0);

    setButtons(Apply | Default | Help);

    QObject::connect(m_data->desktopsModel(), &theseus_ship::DesktopsModel::userModifiedChanged,
        this, &VirtualDesktops::settingsChanged);
    connect(m_data->animationsModel(), &AnimationsModel::animationEnabledChanged,
        this, &VirtualDesktops::settingsChanged);
    connect(m_data->animationsModel(), &AnimationsModel::animationIndexChanged,
        this, &VirtualDesktops::settingsChanged);
}

VirtualDesktops::~VirtualDesktops()
{
}

QAbstractItemModel *VirtualDesktops::desktopsModel() const
{
    return m_data->desktopsModel();
}

QAbstractItemModel *VirtualDesktops::animationsModel() const
{
    return m_data->animationsModel();
}

VirtualDesktopsSettings *VirtualDesktops::virtualDesktopsSettings() const
{
    return m_data->settings();
}

void VirtualDesktops::load()
{
    KQuickManagedConfigModule::load();

    m_data->desktopsModel()->load();
    m_data->animationsModel()->load();
}

void VirtualDesktops::save()
{
    KQuickManagedConfigModule::save();

    m_data->desktopsModel()->syncWithServer();
    m_data->animationsModel()->save();

    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig"));
    QDBusConnection::sessionBus().send(message);
}

void VirtualDesktops::defaults()
{
    KQuickManagedConfigModule::defaults();

    m_data->desktopsModel()->defaults();
    m_data->animationsModel()->defaults();
}

bool VirtualDesktops::isDefaults() const
{
    return m_data->isDefaults();
}

void VirtualDesktops::configureAnimation()
{
    const QModelIndex index = m_data->animationsModel()->index(m_data->animationsModel()->animationIndex(), 0);
    if (!index.isValid()) {
        return;
    }

    m_data->animationsModel()->requestConfigure(index, nullptr);
}

void VirtualDesktops::showAboutAnimation()
{
    const QModelIndex index = m_data->animationsModel()->index(m_data->animationsModel()->animationIndex(), 0);
    if (!index.isValid()) {
        return;
    }

    const QString name    = index.data(AnimationsModel::NameRole).toString();
    const QString comment = index.data(AnimationsModel::DescriptionRole).toString();
    const QString author  = index.data(AnimationsModel::AuthorNameRole).toString();
    const QString email   = index.data(AnimationsModel::AuthorEmailRole).toString();
    const QString website = index.data(AnimationsModel::WebsiteRole).toString();
    const QString version = index.data(AnimationsModel::VersionRole).toString();
    const QString license = index.data(AnimationsModel::LicenseRole).toString();
    const QString icon    = index.data(AnimationsModel::IconNameRole).toString();

    const KAboutLicense::LicenseKey licenseType = KAboutLicense::byKeyword(license).key();

    KAboutData aboutData(
        name,              // Plugin name
        name,              // Display name
        version,           // Version
        comment,           // Short description
        licenseType,       // License
        QString(),         // Copyright statement
        QString(),         // Other text
        website.toLatin1() // Home page
    );
    aboutData.setProgramLogo(icon);

    const QStringList authors = author.split(',');
    const QStringList emails = email.split(',');

    if (authors.count() == emails.count()) {
        int i = 0;
        for (const QString &author : authors) {
            if (!author.isEmpty()) {
                aboutData.addAuthor(i18n(author.toUtf8()), QString(), emails[i]);
            }
            i++;
        }
    }

    QPointer<KAboutApplicationDialog> aboutPlugin = new KAboutApplicationDialog(aboutData);
    aboutPlugin->exec();

    delete aboutPlugin;
}

bool VirtualDesktops::isSaveNeeded() const
{
    return m_data->animationsModel()->needsSave() || m_data->desktopsModel()->needsSave();
}

}

#include "virtualdesktops.moc"
#include "moc_virtualdesktops.cpp"
