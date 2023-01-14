/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "base/options.h"
#include "scripting_logging.h"

#include <KConfigGroup>
#include <KPackage/PackageLoader>

#include <QDBusConnection>
#include <QFutureWatcher>
#include <QMenu>
#include <QQuickWindow>
#include <QStandardPaths>

namespace KWin::scripting
{

platform_wrap::platform_wrap(base::options& options, base::config& config)
    : qml_engine(new QQmlEngine(this))
    , declarative_script_shared_context(new QQmlContext(qml_engine, this))
    , config{config}
    , m_scriptsLock(new QRecursiveMutex)
    , options{options}

{
    qRegisterMetaType<KWin::SessionState>();
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Scripting"),
                                                 this,
                                                 QDBusConnection::ExportScriptableContents
                                                     | QDBusConnection::ExportScriptableInvokables);
}

platform_wrap::~platform_wrap()
{
    QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/Scripting"));
}

void platform_wrap::start()
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

LoadScriptList platform_wrap::queryScriptsToLoad()
{
    if (is_running) {
        config.main->reparseConfiguration();
    } else {
        is_running = true;
    }

    QMap<QString, QString> pluginStates = KConfigGroup(config.main, "Plugins").entryMap();
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

void platform_wrap::slotScriptsQueried()
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

bool platform_wrap::isScriptLoaded(const QString& pluginName) const
{
    return findScript(pluginName) != nullptr;
}

abstract_script* platform_wrap::findScript(const QString& pluginName) const
{
    QMutexLocker locker(m_scriptsLock.data());
    for (auto const& script : qAsConst(scripts)) {
        if (script->pluginName() == pluginName) {
            return script;
        }
    }
    return nullptr;
}

bool platform_wrap::unloadScript(const QString& pluginName)
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

void platform_wrap::runScripts()
{
    QMutexLocker locker(m_scriptsLock.data());
    for (int i = 0; i < scripts.size(); i++) {
        scripts.at(i)->run();
    }
}

void platform_wrap::scriptDestroyed(QObject* object)
{
    QMutexLocker locker(m_scriptsLock.data());
    scripts.removeAll(static_cast<script*>(object));
}

int platform_wrap::loadScript(const QString& filePath, const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    if (isScriptLoaded(pluginName)) {
        return -1;
    }
    const int id = scripts.size();
    auto script = new scripting::script(id, filePath, pluginName, *this, options, config, this);
    connect(script, &QObject::destroyed, this, &platform_wrap::scriptDestroyed);
    scripts.append(script);
    return id;
}

int platform_wrap::loadDeclarativeScript(const QString& filePath, const QString& pluginName)
{
    QMutexLocker locker(m_scriptsLock.data());
    if (isScriptLoaded(pluginName)) {
        return -1;
    }
    const int id = scripts.size();
    auto script = new declarative_script(id, filePath, pluginName, *this, this);
    connect(script, &QObject::destroyed, this, &platform_wrap::scriptDestroyed);
    scripts.append(script);
    return id;
}

}
