/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"
#include <kwin_export.h>

#include <QHash>
#include <QJSEngine>
#include <QJSValue>

#include <QDBusContext>
#include <QDBusMessage>

class QQmlComponent;
class QQmlContext;
class QAction;
class QMenu;
class KConfigGroup;

namespace KWin
{

namespace base
{
class config;
}

namespace scripting
{

class options;
class platform_wrap;
class window;

class KWIN_EXPORT abstract_script : public QObject
{
    Q_OBJECT
public:
    abstract_script(int id,
                    QString scriptName,
                    QString pluginName,
                    base::config& config,
                    QObject* parent = nullptr);
    ~abstract_script() override;
    int scriptId() const
    {
        return m_scriptId;
    }
    QString fileName() const
    {
        return m_fileName;
    }
    const QString& pluginName()
    {
        return m_pluginName;
    }
    bool running() const
    {
        return m_running;
    }

    KConfigGroup config() const;

public Q_SLOTS:
    void stop();
    virtual void run() = 0;

Q_SIGNALS:
    void runningChanged(bool);

protected:
    void setRunning(bool running)
    {
        if (m_running == running) {
            return;
        }
        m_running = running;
        Q_EMIT runningChanged(m_running);
    }

private:
    int m_scriptId;
    QString m_fileName;
    QString m_pluginName;
    bool m_running;
    base::config& base_config;
};

// TODO(romangg): Give it a more specific name.
class KWIN_EXPORT script : public abstract_script, QDBusContext
{
    Q_OBJECT
public:
    script(int id,
           QString scriptName,
           QString pluginName,
           scripting::platform_wrap& platform,
           scripting::options& options,
           base::config& config,
           QObject* parent = nullptr);
    virtual ~script();

    Q_INVOKABLE QVariant readConfig(const QString& key, const QVariant& defaultValue = QVariant());

    Q_INVOKABLE void callDBus(const QString& service,
                              const QString& path,
                              const QString& interface,
                              const QString& method,
                              const QJSValue& arg1 = QJSValue(),
                              const QJSValue& arg2 = QJSValue(),
                              const QJSValue& arg3 = QJSValue(),
                              const QJSValue& arg4 = QJSValue(),
                              const QJSValue& arg5 = QJSValue(),
                              const QJSValue& arg6 = QJSValue(),
                              const QJSValue& arg7 = QJSValue(),
                              const QJSValue& arg8 = QJSValue(),
                              const QJSValue& arg9 = QJSValue());

    Q_INVOKABLE bool registerShortcut(const QString& objectName,
                                      const QString& text,
                                      const QString& keySequence,
                                      const QJSValue& callback);

    Q_INVOKABLE bool registerScreenEdge(int edge, const QJSValue& callback);
    Q_INVOKABLE bool unregisterScreenEdge(int edge);

    Q_INVOKABLE bool registerTouchScreenEdge(int edge, const QJSValue& callback);
    Q_INVOKABLE bool unregisterTouchScreenEdge(int edge);

    /**
     * @brief Registers the given @p callback to be invoked whenever the UserActionsMenu is about
     * to be showed. In the callback the script can create a further sub menu or menu entry to be
     * added to the UserActionsMenu.
     *
     * @param callback Script method to execute when the UserActionsMenu is about to be shown.
     * @return void
     * @see actionsForUserActionMenu
     */
    Q_INVOKABLE void registerUserActionsMenu(const QJSValue& callback);

    /**
     * @brief Creates actions for the UserActionsMenu by invoking the registered callbacks.
     *
     * This method invokes all the callbacks previously registered with
     * registerUseractionsMenuCallback. The Client @p c is passed in as an argument to the invoked
     * method.
     *
     * The invoked method is supposed to return a JavaScript object containing either the menu or
     * menu entry to be added. In case the callback returns a null or undefined or any other invalid
     * value, it is not considered for adding to the menu.
     *
     * The JavaScript object structure for a menu entry looks like the following:
     * @code
     * {
     *     title: "My Menu Entry",
     *     checkable: true,
     *     checked: false,
     *     triggered: function (action) {
     *         // callback when the menu entry is triggered with the QAction as argument
     *     }
     * }
     * @endcode
     *
     * To construct a complete Menu the JavaScript object looks like the following:
     * @code
     * {
     *     title: "My Menu Title",
     *     items: [{...}, {...}, ...] // list of menu entries as described above
     * }
     * @endcode
     *
     * The returned JavaScript object is introspected and for a menu entry a QAction is created,
     * while for a menu a QMenu is created and QActions for the individual entries. Of course it
     * is allowed to have nested structures.
     *
     * All created objects are (grand) children to the passed in @p parent menu, so that they get
     * deleted whenever the menu is destroyed.
     *
     * @param c The Client for which the menu is invoked, passed to the callback
     * @param parent The Parent for the created Menus or Actions
     * @return QList< QAction* > List of QActions obtained from asking the registered callbacks
     * @see registerUseractionsMenuCallback
     */
    QList<QAction*> actionsForUserActionMenu(window* window, QMenu* parent);

public Q_SLOTS:
    void run() override;

private Q_SLOTS:
    /**
     * Callback for when loadScriptFromFile has finished.
     */
    void slotScriptLoadedFromFile();

    /**
     * Called when any reserve screen edge is triggered.
     */
    bool slotBorderActivated(win::electric_border border);

private:
    /**
     * Read the script from file into a byte array.
     * If file cannot be read an empty byte array is returned.
     */
    QByteArray loadScriptFromFile(const QString& fileName);

    /**
     * @brief Parses the @p value to either a QMenu or QAction.
     *
     * @param value The ScriptValue describing either a menu or action
     * @param parent The parent to use for the created menu or action
     * @return QAction* The parsed action or menu action, if parsing fails returns @c null.
     */
    QAction* scriptValueToAction(const QJSValue& value, QMenu* parent);

    /**
     * @brief Creates a new QAction from the provided data and registers it for invoking the
     * @p callback when the action is triggered.
     *
     * The created action is added to the map of actions and callbacks shared with the global
     * shortcuts.
     *
     * @param title The title of the action
     * @param checkable Whether the action is checkable
     * @param checked Whether the checkable action is checked
     * @param callback The callback to invoke when the action is triggered
     * @param parent The parent to be used for the new created action
     * @return QAction* The created action
     */
    QAction* createAction(const QString& title, const QJSValue& item, QMenu* parent);

    /**
     * @brief Parses the @p items and creates a QMenu from it.
     *
     * @param title The title of the Menu.
     * @param items JavaScript Array containing Menu items.
     * @param parent The parent to use for the new created menu
     * @return QAction* The menu action for the new Menu
     */
    QAction* createMenu(const QString& title, const QJSValue& items, QMenu* parent);

    QJSEngine* m_engine;
    QDBusMessage m_invocationContext;
    bool m_starting{false};
    QHash<int, QJSValueList> m_screenEdgeCallbacks;
    std::unordered_map<win::electric_border, uint32_t> reserved_borders;
    QHash<int, QAction*> m_touchScreenEdgeCallbacks;
    QJSValueList m_userActionsMenuCallbacks;
    scripting::platform_wrap& platform;
    scripting::options& options;
};

class declarative_script : public abstract_script
{
    Q_OBJECT
public:
    explicit declarative_script(int id,
                                QString scriptName,
                                QString pluginName,
                                scripting::platform_wrap& platform,
                                QObject* parent = nullptr);
    ~declarative_script() override;

public Q_SLOTS:
    Q_SCRIPTABLE void run() override;

private Q_SLOTS:
    void createComponent();

private:
    QQmlContext* m_context;
    QQmlComponent* m_component;
};

}
}
