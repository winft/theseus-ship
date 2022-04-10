/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwinglobals.h>

#include <QStringList>
#include <memory>

class QQmlContext;
class QQmlEngine;
class QAction;
class QMenu;
class QRecursiveMutex;

/// @c true == javascript, @c false == qml
typedef QList<QPair<bool, QPair<QString, QString>>> LoadScriptList;

namespace KWin
{

namespace win
{
class space;
}

class Toplevel;

namespace scripting
{
class abstract_script;
class declarative_script_space;
class qt_script_space;

template<typename Space, typename RefSpace>
class template_space;

/**
 * The heart of Scripting. Infinite power lies beyond
 */
class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")
private:
    QStringList scriptList;
    QList<abstract_script*> scripts;
    /**
     * Lock to protect the scripts member variable.
     */
    QScopedPointer<QRecursiveMutex> m_scriptsLock;

    // Preferably call ONLY at load time
    void runScripts();

public:
    platform();
    ~platform() override;
    Q_SCRIPTABLE Q_INVOKABLE int loadScript(const QString& filePath,
                                            const QString& pluginName = QString());
    Q_SCRIPTABLE Q_INVOKABLE int loadDeclarativeScript(const QString& filePath,
                                                       const QString& pluginName = QString());
    Q_SCRIPTABLE Q_INVOKABLE bool isScriptLoaded(const QString& pluginName) const;
    Q_SCRIPTABLE Q_INVOKABLE bool unloadScript(const QString& pluginName);

    /**
     * @brief Invokes all registered callbacks to add actions to the UserActionsMenu.
     *
     * @param c The Client for which the UserActionsMenu is about to be shown
     * @param parent The parent menu to which to add created child menus and items
     * @return QList< QAction* > List of all actions aggregated from all scripts.
     */
    QList<QAction*> actionsForUserActionMenu(Toplevel* window, QMenu* parent);

    QQmlEngine* qmlEngine() const;
    QQmlEngine* qmlEngine();
    QQmlContext* declarativeScriptSharedContext() const;
    QQmlContext* declarativeScriptSharedContext();
    qt_script_space* workspaceWrapper() const;

    abstract_script* findScript(const QString& pluginName) const;

public Q_SLOTS:
    void scriptDestroyed(QObject* object);
    Q_SCRIPTABLE void start();

private Q_SLOTS:
    void slotScriptsQueried();

private:
    void init();
    LoadScriptList queryScriptsToLoad();

    QQmlEngine* m_qmlEngine;
    QQmlContext* m_declarativeScriptSharedContext;

    std::unique_ptr<template_space<qt_script_space, win::space>> qt_space;
    std::unique_ptr<template_space<declarative_script_space, win::space>> decl_space;

    bool is_running{false};
};

inline QQmlEngine* platform::qmlEngine() const
{
    return m_qmlEngine;
}

inline QQmlEngine* platform::qmlEngine()
{
    return m_qmlEngine;
}

inline QQmlContext* platform::declarativeScriptSharedContext() const
{
    return m_declarativeScriptSharedContext;
}

inline QQmlContext* platform::declarativeScriptSharedContext()
{
    return m_declarativeScriptSharedContext;
}

}
}
