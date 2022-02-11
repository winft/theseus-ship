/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "dbus_call.h"
#include "screen_edge_item.h"
#include "script.h"
#include "space.h"
#include "v2/client_model.h"
#include "v3/client_model.h"
#include "v3/virtual_desktop_model.h"
#include "window.h"

#include "../options.h"
#include "input/redirect.h"
#include "render/thumbnail_item.h"
#include "scripting_logging.h"
#include "win/screen_edges.h"
#include "workspace.h"

#include "win/x11/window.h"

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

platform::platform()
    : m_scriptsLock(new QMutex(QMutex::Recursive))
    , m_qmlEngine(new QQmlEngine(this))
    , m_declarativeScriptSharedContext(new QQmlContext(m_qmlEngine, this))
    , qt_space{std::make_unique<template_space<qt_script_space, win::space>>(workspace())}
{
    init();
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Scripting"),
                                                 this,
                                                 QDBusConnection::ExportScriptableContents
                                                     | QDBusConnection::ExportScriptableInvokables);

    // Start the scripting platform, but first process all events.
    // TODO(romangg): Can we also do this through a simple call?
    QMetaObject::invokeMethod(this, "start", Qt::QueuedConnection);
}

platform::~platform()
{
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/Scripting"));
}

void platform::init()
{
    qmlRegisterType<render::desktop_thumbnail_item>("org.kde.kwin", 2, 0, "DesktopThumbnailItem");
    qmlRegisterType<render::window_thumbnail_item>("org.kde.kwin", 2, 0, "ThumbnailItem");
    qmlRegisterType<dbus_call>("org.kde.kwin", 2, 0, "DBusCall");
    qmlRegisterType<screen_edge_item>("org.kde.kwin", 2, 0, "ScreenEdgeItem");
    qmlRegisterType<models::v2::client_model>();
    qmlRegisterType<models::v2::simple_client_model>("org.kde.kwin", 2, 0, "ClientModel");
    qmlRegisterType<models::v2::client_model_by_screen>(
        "org.kde.kwin", 2, 0, "ClientModelByScreen");
    qmlRegisterType<models::v2::client_model_by_screen_and_desktop>(
        "org.kde.kwin", 2, 0, "ClientModelByScreenAndDesktop");
    qmlRegisterType<models::v2::client_model_by_screen_and_activity>(
        "org.kde.kwin", 2, 1, "ClientModelByScreenAndActivity");
    qmlRegisterType<models::v2::client_filter_model>("org.kde.kwin", 2, 0, "ClientFilterModel");

    qmlRegisterType<render::window_thumbnail_item>("org.kde.kwin", 3, 0, "WindowThumbnailItem");
    qmlRegisterType<dbus_call>("org.kde.kwin", 3, 0, "DBusCall");
    qmlRegisterType<screen_edge_item>("org.kde.kwin", 3, 0, "ScreenEdgeItem");
    qmlRegisterType<models::v3::client_model>("org.kde.kwin", 3, 0, "ClientModel");
    qmlRegisterType<models::v3::client_filter_model>("org.kde.kwin", 3, 0, "ClientFilterModel");
    qmlRegisterType<models::v3::virtual_desktop_model>("org.kde.kwin", 3, 0, "VirtualDesktopModel");

    qmlRegisterType<window>();
    qmlRegisterSingletonType<qt_script_space>(
        "org.kde.kwin",
        3,
        0,
        "Workspace",
        [](QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> qt_script_space* {
            Q_UNUSED(qmlEngine)
            Q_UNUSED(jsEngine)
            return new template_space<qt_script_space, win::space>(workspace());
        });
    qmlRegisterType<QAbstractItemModel>();

    m_qmlEngine->rootContext()->setContextProperty(QStringLiteral("workspace"), qt_space.get());
    m_qmlEngine->rootContext()->setContextProperty(QStringLiteral("options"), options);

    decl_space
        = std::make_unique<template_space<declarative_script_space, win::space>>(workspace());
    m_declarativeScriptSharedContext->setContextProperty(QStringLiteral("workspace"),
                                                         decl_space.get());
    // QQmlListProperty interfaces only work via properties, rebind them as functions here
    QQmlExpression expr(m_declarativeScriptSharedContext,
                        nullptr,
                        "workspace.clientList = function() { return workspace.clients }");
    expr.evaluate();
}

void platform::start()
{
#if 0
    // TODO make this threaded again once KConfigGroup is sufficiently thread safe, bug #305361 and friends
    // perform querying for the services in a thread
    QFutureWatcher<LoadScriptList> *watcher = new QFutureWatcher<LoadScriptList>(this);
    connect(watcher, &QFutureWatcher<LoadScriptList>::finished, this, &platform::slotScriptsQueried);
    watcher->setFuture(QtConcurrent::run(this, &platform::queryScriptsToLoad, pluginStates, offers));
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

LoadScriptList platform::queryScriptsToLoad()
{
    auto config = kwinApp()->config();

    if (is_running) {
        config->reparseConfiguration();
    } else {
        is_running = true;
    }

    QMap<QString, QString> pluginStates = KConfigGroup(config, "Plugins").entryMap();
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

void platform::slotScriptsQueried()
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

bool platform::isScriptLoaded(const QString& pluginName) const
{
    return findScript(pluginName) != nullptr;
}

qt_script_space* platform::workspaceWrapper() const
{
    return qt_space.get();
}

abstract_script* platform::findScript(const QString& pluginName) const
{
    QMutexLocker locker(m_scriptsLock.data());
    for (auto const& script : qAsConst(scripts)) {
        if (script->pluginName() == pluginName) {
            return script;
        }
    }
    return nullptr;
}

bool platform::unloadScript(const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    for (auto const& script : qAsConst(scripts)) {
        if (script->pluginName() == pluginName) {
            script->deleteLater();
            return true;
        }
    }
    return false;
}

void platform::runScripts()
{
    QMutexLocker locker(m_scriptsLock.data());
    for (int i = 0; i < scripts.size(); i++) {
        scripts.at(i)->run();
    }
}

void platform::scriptDestroyed(QObject* object)
{
    QMutexLocker locker(m_scriptsLock.data());
    scripts.removeAll(static_cast<script*>(object));
}

int platform::loadScript(const QString& filePath, const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    if (isScriptLoaded(pluginName)) {
        return -1;
    }
    const int id = scripts.size();
    auto script = new scripting::script(id, filePath, pluginName, this);
    connect(script, &QObject::destroyed, this, &platform::scriptDestroyed);
    scripts.append(script);
    return id;
}

int platform::loadDeclarativeScript(const QString& filePath, const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    if (isScriptLoaded(pluginName)) {
        return -1;
    }
    const int id = scripts.size();
    auto script = new declarative_script(id, filePath, pluginName, this);
    connect(script, &QObject::destroyed, this, &platform::scriptDestroyed);
    scripts.append(script);
    return id;
}

QList<QAction*> platform::actionsForUserActionMenu(Toplevel* window, QMenu* parent)
{
    auto const w_wins = workspaceWrapper()->clientList();
    auto window_it = std::find_if(
        w_wins.cbegin(), w_wins.cend(), [window](auto win) { return win->client() == window; });
    assert(window_it != w_wins.cend());

    QList<QAction*> actions;
    for (auto s : scripts) {
        // TODO: Allow declarative scripts to add their own user actions.
        if (auto script = qobject_cast<scripting::script*>(s)) {
            actions << script->actionsForUserActionMenu(*window_it, parent);
        }
    }
    return actions;
}

}
