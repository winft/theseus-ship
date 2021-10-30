/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwinglobals.h>

#include <QStringList>

class QQmlContext;
class QQmlEngine;
class QAction;
class QMenu;
class QMutex;

/// @c true == javascript, @c false == qml
typedef QList<QPair<bool, QPair<QString, QString>>> LoadScriptList;

namespace KWin
{
class Toplevel;

namespace scripting
{
class AbstractScript;
class DeclarativeScript;
class QtScriptWorkspaceWrapper;

/**
 * The heart of Scripting. Infinite power lies beyond
 */
class KWIN_EXPORT Scripting : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")
private:
    explicit Scripting(QObject* parent);
    QStringList scriptList;
    QList<AbstractScript*> scripts;
    /**
     * Lock to protect the scripts member variable.
     */
    QScopedPointer<QMutex> m_scriptsLock;

    // Preferably call ONLY at load time
    void runScripts();

public:
    ~Scripting() override;
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
    QtScriptWorkspaceWrapper* workspaceWrapper() const;

    AbstractScript* findScript(const QString& pluginName) const;

    static Scripting* self();
    static Scripting* create(QObject* parent);

public Q_SLOTS:
    void scriptDestroyed(QObject* object);
    Q_SCRIPTABLE void start();

private Q_SLOTS:
    void slotScriptsQueried();

private:
    void init();
    LoadScriptList queryScriptsToLoad();
    static Scripting* s_self;
    QQmlEngine* m_qmlEngine;
    QQmlContext* m_declarativeScriptSharedContext;
    QtScriptWorkspaceWrapper* m_workspaceWrapper;
};

inline QQmlEngine* Scripting::qmlEngine() const
{
    return m_qmlEngine;
}

inline QQmlEngine* Scripting::qmlEngine()
{
    return m_qmlEngine;
}

inline QQmlContext* Scripting::declarativeScriptSharedContext() const
{
    return m_declarativeScriptSharedContext;
}

inline QQmlContext* Scripting::declarativeScriptSharedContext()
{
    return m_declarativeScriptSharedContext;
}

inline QtScriptWorkspaceWrapper* Scripting::workspaceWrapper() const
{
    return m_workspaceWrapper;
}

inline Scripting* Scripting::self()
{
    return s_self;
}

}
}
