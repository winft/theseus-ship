/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "scripting.h"

#include "script.h"

#include "../options.h"
#include "../thumbnailitem.h"
#include "../workspace.h"
#include "dbuscall.h"
#include "input/redirect.h"
#include "screenedge.h"
#include "screenedgeitem.h"
#include "scripting_logging.h"
#include "window_wrapper.h"
#include "workspace_wrapper.h"

#include "win/x11/window.h"

#include "v2/clientmodel.h"
#include "v3/clientmodel.h"

#include <KConfigGroup>
#include <KPackage/PackageLoader>

#include <QDBusConnection>
#include <QFutureWatcher>
#include <QMenu>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QStandardPaths>

namespace KWin::scripting
{

Scripting* Scripting::s_self = nullptr;

Scripting* Scripting::create(QObject* parent)
{
    Q_ASSERT(!s_self);
    s_self = new Scripting(parent);
    return s_self;
}

Scripting::Scripting(QObject* parent)
    : QObject(parent)
    , m_scriptsLock(new QMutex(QMutex::Recursive))
    , m_qmlEngine(new QQmlEngine(this))
    , m_declarativeScriptSharedContext(new QQmlContext(m_qmlEngine, this))
    , m_workspaceWrapper(new QtScriptWorkspaceWrapper(this))
{
    init();
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Scripting"),
                                                 this,
                                                 QDBusConnection::ExportScriptableContents
                                                     | QDBusConnection::ExportScriptableInvokables);
    connect(Workspace::self(), &Workspace::configChanged, this, &Scripting::start);
    connect(Workspace::self(), &Workspace::workspaceInitialized, this, &Scripting::start);
}

void Scripting::init()
{
    qmlRegisterType<DesktopThumbnailItem>("org.kde.kwin", 2, 0, "DesktopThumbnailItem");
    qmlRegisterType<WindowThumbnailItem>("org.kde.kwin", 2, 0, "ThumbnailItem");
    qmlRegisterType<DBusCall>("org.kde.kwin", 2, 0, "DBusCall");
    qmlRegisterType<ScreenEdgeItem>("org.kde.kwin", 2, 0, "ScreenEdgeItem");
    qmlRegisterType<ScriptingModels::V2::ClientModel>();
    qmlRegisterType<ScriptingModels::V2::SimpleClientModel>("org.kde.kwin", 2, 0, "ClientModel");
    qmlRegisterType<ScriptingModels::V2::ClientModelByScreen>(
        "org.kde.kwin", 2, 0, "ClientModelByScreen");
    qmlRegisterType<ScriptingModels::V2::ClientModelByScreenAndDesktop>(
        "org.kde.kwin", 2, 0, "ClientModelByScreenAndDesktop");
    qmlRegisterType<ScriptingModels::V2::ClientModelByScreenAndActivity>(
        "org.kde.kwin", 2, 1, "ClientModelByScreenAndActivity");
    qmlRegisterType<ScriptingModels::V2::ClientFilterModel>(
        "org.kde.kwin", 2, 0, "ClientFilterModel");

    qmlRegisterType<WindowThumbnailItem>("org.kde.kwin", 3, 0, "WindowThumbnailItem");
    qmlRegisterType<DBusCall>("org.kde.kwin", 3, 0, "DBusCall");
    qmlRegisterType<ScreenEdgeItem>("org.kde.kwin", 3, 0, "ScreenEdgeItem");
    qmlRegisterType<ScriptingModels::V3::ClientModel>("org.kde.kwin", 3, 0, "ClientModel");
    qmlRegisterType<ScriptingModels::V3::ClientFilterModel>(
        "org.kde.kwin", 3, 0, "ClientFilterModel");

    qmlRegisterType<WindowWrapper>();
    qmlRegisterSingletonType<QtScriptWorkspaceWrapper>(
        "org.kde.kwin", 3, 0, "Workspace", [](QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
            Q_UNUSED(qmlEngine)
            Q_UNUSED(jsEngine)
            return new QtScriptWorkspaceWrapper();
        });
    qmlRegisterType<QAbstractItemModel>();

    m_qmlEngine->rootContext()->setContextProperty(QStringLiteral("workspace"), m_workspaceWrapper);
    m_qmlEngine->rootContext()->setContextProperty(QStringLiteral("options"), options);

    m_declarativeScriptSharedContext->setContextProperty(
        QStringLiteral("workspace"), new DeclarativeScriptWorkspaceWrapper(this));
    // QQmlListProperty interfaces only work via properties, rebind them as functions here
    QQmlExpression expr(m_declarativeScriptSharedContext,
                        nullptr,
                        "workspace.clientList = function() { return workspace.clients }");
    expr.evaluate();
}

void Scripting::start()
{
#if 0
    // TODO make this threaded again once KConfigGroup is sufficiently thread safe, bug #305361 and friends
    // perform querying for the services in a thread
    QFutureWatcher<LoadScriptList> *watcher = new QFutureWatcher<LoadScriptList>(this);
    connect(watcher, &QFutureWatcher<LoadScriptList>::finished, this, &Scripting::slotScriptsQueried);
    watcher->setFuture(QtConcurrent::run(this, &Scripting::queryScriptsToLoad, pluginStates, offers));
#else
    LoadScriptList scriptsToLoad = queryScriptsToLoad();
    for (LoadScriptList::const_iterator it = scriptsToLoad.constBegin();
         it != scriptsToLoad.constEnd();
         ++it) {
        if (it->first) {
            loadScript(it->second.first, it->second.second);
        } else {
            loadDeclarativeScript(it->second.first, it->second.second);
        }
    }

    runScripts();
#endif
}

LoadScriptList Scripting::queryScriptsToLoad()
{
    KSharedConfig::Ptr _config = kwinApp()->config();
    static bool s_started = false;
    if (s_started) {
        _config->reparseConfiguration();
    } else {
        s_started = true;
    }
    QMap<QString, QString> pluginStates = KConfigGroup(_config, "Plugins").entryMap();
    const QString scriptFolder = QStringLiteral(KWIN_NAME "/scripts/");
    const auto offers = KPackage::PackageLoader::self()->listPackages(QStringLiteral("KWin/Script"),
                                                                      scriptFolder);

    LoadScriptList scriptsToLoad;

    for (const KPluginMetaData& service : offers) {
        const QString value
            = pluginStates.value(service.pluginId() + QLatin1String("Enabled"), QString());
        const bool enabled
            = value.isNull() ? service.isEnabledByDefault() : QVariant(value).toBool();
        const bool javaScript
            = service.value(QStringLiteral("X-Plasma-API")) == QLatin1String("javascript");
        const bool declarativeScript
            = service.value(QStringLiteral("X-Plasma-API")) == QLatin1String("declarativescript");
        if (!javaScript && !declarativeScript) {
            continue;
        }

        if (!enabled) {
            if (isScriptLoaded(service.pluginId())) {
                // unload the script
                unloadScript(service.pluginId());
            }
            continue;
        }
        const QString pluginName = service.pluginId();
        const QString scriptName = service.value(QStringLiteral("X-Plasma-MainScript"));
        const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                    scriptFolder + pluginName
                                                        + QLatin1String("/contents/") + scriptName);
        if (file.isNull()) {
            qCDebug(KWIN_SCRIPTING) << "Could not find script file for " << pluginName;
            continue;
        }
        scriptsToLoad << qMakePair(javaScript, qMakePair(file, pluginName));
    }
    return scriptsToLoad;
}

void Scripting::slotScriptsQueried()
{
    QFutureWatcher<LoadScriptList>* watcher
        = dynamic_cast<QFutureWatcher<LoadScriptList>*>(sender());
    if (!watcher) {
        // slot invoked not from a FutureWatcher
        return;
    }

    LoadScriptList scriptsToLoad = watcher->result();
    for (LoadScriptList::const_iterator it = scriptsToLoad.constBegin();
         it != scriptsToLoad.constEnd();
         ++it) {
        if (it->first) {
            loadScript(it->second.first, it->second.second);
        } else {
            loadDeclarativeScript(it->second.first, it->second.second);
        }
    }

    runScripts();
    watcher->deleteLater();
}

bool Scripting::isScriptLoaded(const QString& pluginName) const
{
    return findScript(pluginName) != nullptr;
}

AbstractScript* Scripting::findScript(const QString& pluginName) const
{
    QMutexLocker locker(m_scriptsLock.data());
    foreach (AbstractScript* script, scripts) {
        if (script->pluginName() == pluginName) {
            return script;
        }
    }
    return nullptr;
}

bool Scripting::unloadScript(const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    foreach (AbstractScript* script, scripts) {
        if (script->pluginName() == pluginName) {
            script->deleteLater();
            return true;
        }
    }
    return false;
}

void Scripting::runScripts()
{
    QMutexLocker locker(m_scriptsLock.data());
    for (int i = 0; i < scripts.size(); i++) {
        scripts.at(i)->run();
    }
}

void Scripting::scriptDestroyed(QObject* object)
{
    QMutexLocker locker(m_scriptsLock.data());
    scripts.removeAll(static_cast<Script*>(object));
}

int Scripting::loadScript(const QString& filePath, const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    if (isScriptLoaded(pluginName)) {
        return -1;
    }
    const int id = scripts.size();
    auto script = new Script(id, filePath, pluginName, this);
    connect(script, &QObject::destroyed, this, &Scripting::scriptDestroyed);
    scripts.append(script);
    return id;
}

int Scripting::loadDeclarativeScript(const QString& filePath, const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    if (isScriptLoaded(pluginName)) {
        return -1;
    }
    const int id = scripts.size();
    auto script = new DeclarativeScript(id, filePath, pluginName, this);
    connect(script, &QObject::destroyed, this, &Scripting::scriptDestroyed);
    scripts.append(script);
    return id;
}

Scripting::~Scripting()
{
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/Scripting"));
    s_self = nullptr;
}

QList<QAction*> Scripting::actionsForUserActionMenu(Toplevel* window, QMenu* parent)
{
    auto const w_wins = Scripting::self()->workspaceWrapper()->clientList();
    auto window_it = std::find_if(
        w_wins.cbegin(), w_wins.cend(), [window](auto win) { return win->client() == window; });
    assert(window_it != w_wins.cend());

    QList<QAction*> actions;
    for (AbstractScript* s : scripts) {
        // TODO: Allow declarative scripts to add their own user actions.
        if (Script* script = qobject_cast<Script*>(s)) {
            actions << script->actionsForUserActionMenu(*window_it, parent);
        }
    }
    return actions;
}

}
