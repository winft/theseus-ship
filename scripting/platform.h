/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "dbus_call.h"
#include "screen_edge_item.h"
#include "script.h"
#include "scripting/desktop_background_item.h"
#include "space.h"
#include "v2/client_model.h"
#include "v3/client_model.h"
#include "v3/virtual_desktop_model.h"
#include "window.h"

#include "kwinglobals.h"
#include "render/thumbnail_item.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QStringList>
#include <memory>

class QQmlContext;
class QQmlEngine;
class QAction;
class QMenu;
class QRecursiveMutex;

/// @c true == javascript, @c false == qml
typedef QList<QPair<bool, QPair<QString, QString>>> LoadScriptList;

namespace KWin::scripting
{

class abstract_script;

/**
 * The heart of Scripting. Infinite power lies beyond
 */
class KWIN_EXPORT platform_wrap : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")

public:
    platform_wrap(base::options& options, base::config& config);
    ~platform_wrap() override;

    Q_SCRIPTABLE Q_INVOKABLE int loadScript(const QString& filePath,
                                            const QString& pluginName = QString());
    Q_SCRIPTABLE Q_INVOKABLE int loadDeclarativeScript(const QString& filePath,
                                                       const QString& pluginName = QString());
    Q_SCRIPTABLE Q_INVOKABLE bool isScriptLoaded(const QString& pluginName) const;
    Q_SCRIPTABLE Q_INVOKABLE bool unloadScript(const QString& pluginName);

    virtual qt_script_space* workspaceWrapper() const = 0;

    abstract_script* findScript(const QString& pluginName) const;

    virtual uint32_t reserve(ElectricBorder border, std::function<bool(ElectricBorder)> callback)
        = 0;
    virtual void unreserve(ElectricBorder border, uint32_t id) = 0;
    virtual void reserve_touch(ElectricBorder border, QAction* action) = 0;
    virtual void register_shortcut(QKeySequence const& shortcut, QAction* action) = 0;

    QQmlEngine* qml_engine;
    QQmlContext* declarative_script_shared_context;
    base::config& config;

public Q_SLOTS:
    void scriptDestroyed(QObject* object);
    Q_SCRIPTABLE void start();

private Q_SLOTS:
    void slotScriptsQueried();

protected:
    QList<abstract_script*> scripts;

private:
    LoadScriptList queryScriptsToLoad();

    // Preferably call ONLY at load time
    void runScripts();

    // Lock to protect the scripts member variable.
    QScopedPointer<QRecursiveMutex> m_scriptsLock;

    QStringList scriptList;
    bool is_running{false};
    base::options& options;
};

template<typename Space>
class platform : public platform_wrap
{
public:
    platform(Space& space)
        : platform_wrap(*space.base.options, space.base.config)
        , space{space}
    {
        qmlRegisterType<render::window_thumbnail_item>("org.kde.kwin", 2, 0, "ThumbnailItem");
        qmlRegisterType<dbus_call>("org.kde.kwin", 2, 0, "DBusCall");
        qmlRegisterType<screen_edge_item>("org.kde.kwin", 2, 0, "ScreenEdgeItem");
        qmlRegisterAnonymousType<models::v2::client_model>("org.kde.kwin", 2);
        qmlRegisterType<models::v2::simple_client_model>("org.kde.kwin", 2, 0, "ClientModel");
        qmlRegisterType<models::v2::client_model_by_screen>(
            "org.kde.kwin", 2, 0, "ClientModelByScreen");
        qmlRegisterType<models::v2::client_model_by_screen_and_desktop>(
            "org.kde.kwin", 2, 0, "ClientModelByScreenAndDesktop");
        qmlRegisterType<models::v2::client_model_by_screen_and_activity>(
            "org.kde.kwin", 2, 1, "ClientModelByScreenAndActivity");
        qmlRegisterType<models::v2::client_filter_model>("org.kde.kwin", 2, 0, "ClientFilterModel");

        qmlRegisterType<desktop_background_item>("org.kde.kwin", 3, 0, "DesktopBackgroundItem");
        qmlRegisterType<render::window_thumbnail_item>("org.kde.kwin", 3, 0, "WindowThumbnailItem");
        qmlRegisterType<dbus_call>("org.kde.kwin", 3, 0, "DBusCall");
        qmlRegisterType<screen_edge_item>("org.kde.kwin", 3, 0, "ScreenEdgeItem");
        qmlRegisterType<models::v3::client_model>("org.kde.kwin", 3, 0, "ClientModel");
        qmlRegisterType<models::v3::client_filter_model>("org.kde.kwin", 3, 0, "ClientFilterModel");
        qmlRegisterType<models::v3::virtual_desktop_model>(
            "org.kde.kwin", 3, 0, "VirtualDesktopModel");

        qmlRegisterSingletonType<qt_script_space>(
            "org.kde.kwin",
            3,
            0,
            "Workspace",
            [this](QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> qt_script_space* {
                Q_UNUSED(qmlEngine)
                Q_UNUSED(jsEngine)
                return new template_space<qt_script_space, Space>(&this->space);
            });
        qmlRegisterSingletonInstance(
            "org.kde.kwin", 3, 0, "Options", space.base.options->qobject.get());

        qmlRegisterAnonymousType<window>("org.kde.kwin", 2);
        qmlRegisterAnonymousType<win::virtual_desktop>("org.kde.kwin", 2);
        qmlRegisterAnonymousType<QAbstractItemModel>("org.kde.kwin", 2);
        qmlRegisterAnonymousType<window>("org.kde.kwin", 3);
        qmlRegisterAnonymousType<win::virtual_desktop>("org.kde.kwin", 3);
        qmlRegisterAnonymousType<QAbstractItemModel>("org.kde.kwin", 3);

        // TODO Plasma 6: Drop context properties.
        qt_space = std::make_unique<template_space<qt_script_space, Space>>(&space);
        qml_engine->rootContext()->setContextProperty("workspace", qt_space.get());
        qml_engine->rootContext()->setContextProperty("options", space.base.options->qobject.get());

        decl_space = std::make_unique<template_space<declarative_script_space, Space>>(&space);
        declarative_script_shared_context->setContextProperty("workspace", decl_space.get());

        // QQmlListProperty interfaces only work via properties, rebind them as functions here
        QQmlExpression expr(declarative_script_shared_context,
                            nullptr,
                            "workspace.clientList = function() { return workspace.clients }");
        expr.evaluate();

        // Start the scripting platform, but first process all events.
        // TODO(romangg): Can we also do this through a simple call?
        QMetaObject::invokeMethod(this, "start", Qt::QueuedConnection);
    }

    qt_script_space* workspaceWrapper() const override
    {
        return qt_space.get();
    }

    uint32_t reserve(ElectricBorder border, std::function<bool(ElectricBorder)> callback) override
    {
        return space.edges->reserve(border, callback);
    }

    void unreserve(ElectricBorder border, uint32_t id) override
    {
        space.edges->unreserve(border, id);
    }

    void reserve_touch(ElectricBorder border, QAction* action) override
    {
        space.edges->reserveTouch(border, action);
    }

    void register_shortcut(QKeySequence const& shortcut, QAction* action) override
    {
        space.base.input->registerShortcut(shortcut, action);
    }

    /**
     * @brief Invokes all registered callbacks to add actions to the UserActionsMenu.
     *
     * @param c The Client for which the UserActionsMenu is about to be shown
     * @param parent The parent menu to which to add created child menus and items
     * @return QList< QAction* > List of all actions aggregated from all scripts.
     */
    QList<QAction*> actionsForUserActionMenu(typename Space::window_t window, QMenu* parent)
    {
        auto const w_wins = workspaceWrapper()->clientList();
        auto window_it = std::find_if(w_wins.cbegin(), w_wins.cend(), [window](auto win) {
            return static_cast<window_impl<typename Space::window_t>*>(win)->client() == window;
        });
        assert(window_it != w_wins.cend());

        QList<QAction*> actions;
        for (auto s : qAsConst(scripts)) {
            // TODO: Allow declarative scripts to add their own user actions.
            if (auto script = qobject_cast<scripting::script*>(s)) {
                actions << script->actionsForUserActionMenu(*window_it, parent);
            }
        }
        return actions;
    }

    Space& space;

private:
    std::unique_ptr<template_space<qt_script_space, Space>> qt_space;
    std::unique_ptr<template_space<declarative_script_space, Space>> decl_space;
};

}
