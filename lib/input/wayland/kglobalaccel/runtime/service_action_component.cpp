/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "service_action_component.h"

#include "input/logging.h"

#include <win/singleton_interface.h>

#include <KShell>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QProcess>
#include <private/qtx11extras_p.h>

KServiceActionComponent::KServiceActionComponent(GlobalShortcutsRegistry& registry,
                                                 QString const& serviceStorageId,
                                                 QString const& friendlyName)
    : Component(registry, serviceStorageId, friendlyName)
    , m_serviceStorageId(serviceStorageId)
{
    auto filePath = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                           QStringLiteral("kglobalaccel/") + serviceStorageId);
    if (filePath.isEmpty()) {
        // Fallback to applications data dir for custom shortcut for instance
        filePath = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                          QStringLiteral("applications/") + serviceStorageId);
        m_isInApplicationsDir = true;
    } else {
        QFileInfo info(filePath);
        if (info.isSymLink()) {
            auto const filePath2
                = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                         QStringLiteral("applications/") + serviceStorageId);
            if (info.symLinkTarget() == filePath2) {
                filePath = filePath2;
                m_isInApplicationsDir = true;
            }
        }
    }

    if (filePath.isEmpty()) {
        qCWarning(KWIN_INPUT) << "No desktop file found for service " << serviceStorageId;
    }
    m_desktopFile.reset(new KDesktopFile(filePath));
}

KServiceActionComponent::~KServiceActionComponent() = default;

void KServiceActionComponent::runProcess(const KConfigGroup& group, QString const& token)
{
    auto args = KShell::splitArgs(group.readEntry(QStringLiteral("Exec"), QString()));
    if (args.isEmpty()) {
        return;
    }
    // sometimes entries have an %u for command line parameters
    if (args.last().contains(QLatin1Char('%'))) {
        args.pop_back();
    }

    auto const command = args.takeFirst();

    auto startDetachedWithToken = [token](QString const& program, QStringList const& args) {
        QProcess p;
        p.setProgram(program);
        p.setArguments(args);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!token.isEmpty()) {
            env.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), token);
        }
        p.setProcessEnvironment(env);
        if (!p.startDetached()) {
            qCWarning(KWIN_INPUT) << "Failed to start" << program;
        }
    };

    const auto kstart = QStandardPaths::findExecutable(QStringLiteral("kstart5"));
    if (!kstart.isEmpty()) {
        if (group.name() == QLatin1String("Desktop Entry") && m_isInApplicationsDir) {
            startDetachedWithToken(kstart,
                                   {QStringLiteral("--application"),
                                    QFileInfo(m_desktopFile->fileName()).completeBaseName()});
        } else {
            args.prepend(command);
            args.prepend(QStringLiteral("--"));
            startDetachedWithToken(kstart, args);
        }
        return;
    }

    auto dbusDaemon = QDBusConnection::sessionBus().interface();
    auto const klauncherAvailable
        = dbusDaemon->isServiceRegistered(QStringLiteral("org.kde.klauncher5"));
    if (klauncherAvailable) {
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.klauncher5"),
                                                          QStringLiteral("/KLauncher"),
                                                          QStringLiteral("org.kde.KLauncher"),
                                                          QStringLiteral("exec_blind"));
        msg << command << args;

        QDBusConnection::sessionBus().asyncCall(msg);
        return;
    }

    auto const cmdExec = QStandardPaths::findExecutable(command);
    if (cmdExec.isEmpty()) {
        qCWarning(KWIN_INPUT) << "Could not find executable in PATH" << command;
        return;
    }
    startDetachedWithToken(cmdExec, args);
}

void KServiceActionComponent::emitGlobalShortcutPressed(const GlobalShortcut& shortcut)
{
    // TODO KF6 use ApplicationLauncherJob to start processes when it's available in a framework
    // that we depend on

    auto launchWithToken = [this, shortcut](QString const& token) {
        // DBusActivatatable spec as per
        // https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#dbus
        if (m_desktopFile->desktopGroup().readEntry("DBusActivatable", false)) {
            auto const serviceName = m_serviceStorageId.chopped(strlen(".desktop"));
            QString const objectPath = QStringLiteral("/%1")
                                           .arg(serviceName)
                                           .replace(QLatin1Char('.'), QLatin1Char('/'));
            QString const interface = QStringLiteral("org.freedesktop.Application");
            QDBusMessage message;
            if (shortcut.uniqueName() == QLatin1String("_launch")) {
                message = QDBusMessage::createMethodCall(
                    serviceName, objectPath, interface, QStringLiteral("Activate"));
            } else {
                message = QDBusMessage::createMethodCall(
                    serviceName, objectPath, interface, QStringLiteral("ActivateAction"));
                message << shortcut.uniqueName() << QVariantList();
            }
            if (!token.isEmpty()) {
                message << QVariantMap{{QStringLiteral("activation-token"), token}};
            } else {
                message << QVariantMap();
            }

            QDBusConnection::sessionBus().asyncCall(message);
            return;
        }

        // we can't use KRun there as it depends from KIO and would create a circular dep
        if (shortcut.uniqueName() == QLatin1String("_launch")) {
            runProcess(m_desktopFile->desktopGroup(), token);
            return;
        }
        const auto lstActions = m_desktopFile->readActions();
        for (auto const& action : lstActions) {
            if (action == shortcut.uniqueName()) {
                runProcess(m_desktopFile->actionGroup(action), token);
                return;
            }
        }
    };

    auto const serviceName = m_serviceStorageId.chopped(strlen(".desktop"));

    auto& token_setter = KWin::win::singleton_interface::set_activation_token;
    assert(token_setter);

    auto token = token_setter(serviceName.toStdString());
    launchWithToken(QString::fromStdString(token));
}

void KServiceActionComponent::loadFromService()
{
    auto registerGroupShortcut = [this](auto const& name, auto const& group) {
        auto const shortcutString = group.readEntry(QStringLiteral("X-KDE-Shortcuts"), QString())
                                        .replace(QLatin1Char(','), QLatin1Char('\t'));
        auto shortcut = registerShortcut(name,
                                         group.readEntry(QStringLiteral("Name"), QString()),
                                         shortcutString,
                                         shortcutString);
        shortcut->setIsPresent(true);
    };

    registerGroupShortcut(QStringLiteral("_launch"), m_desktopFile->desktopGroup());
    auto const lstActions = m_desktopFile->readActions();
    for (auto const& action : lstActions) {
        registerGroupShortcut(action, m_desktopFile->actionGroup(action));
    }
}

bool KServiceActionComponent::cleanUp()
{
    qCDebug(KWIN_INPUT) << "Disabling desktop file";

    const auto shortcuts = allShortcuts();
    for (auto shortcut : shortcuts) {
        shortcut->setIsPresent(false);
    }

    return Component::cleanUp();
}

#include "moc_service_action_component.cpp"
