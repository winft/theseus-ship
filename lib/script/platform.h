/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "dbus_call.h"
#include "effect_loader.h"
#include "options.h"
#include "output.h"
#include "screen_edge_handler.h"
#include "script.h"
#include "script/desktop_background_item.h"
#include "shortcut_handler.h"
#include "singleton_interface.h"
#include "space.h"
#include "virtual_desktop_model.h"
#include "window.h"
#include "window_model.h"
#include "window_thumbnail_item.h"

#include <base/kwin_export.h>
#include <render/effect/interface/quick_scene.h>

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
class options;

/**
 * The heart of Scripting. Infinite power lies beyond
 */
class KWIN_EXPORT platform_wrap : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Scripting")

public:
    platform_wrap(base::options& options,
                  win::options& win_opts,
                  render::options& render_opts,
                  base::config& config);
    ~platform_wrap() override;

    Q_SCRIPTABLE Q_INVOKABLE int loadScript(const QString& filePath,
                                            const QString& pluginName = QString());
    Q_SCRIPTABLE Q_INVOKABLE int loadDeclarativeScript(const QString& filePath,
                                                       const QString& pluginName = QString());
    Q_SCRIPTABLE Q_INVOKABLE bool isScriptLoaded(const QString& pluginName) const;
    Q_SCRIPTABLE Q_INVOKABLE bool unloadScript(const QString& pluginName);

    virtual qt_script_space* workspaceWrapper() const = 0;

    abstract_script* findScript(const QString& pluginName) const;

    virtual uint32_t reserve(win::electric_border border,
                             std::function<bool(win::electric_border)> callback)
        = 0;
    virtual void unreserve(win::electric_border border, uint32_t id) = 0;
    virtual void reserve_touch(win::electric_border border, QAction* action) = 0;
    virtual void register_shortcut(QKeySequence const& shortcut, QAction* action) = 0;

    QQmlEngine* qml_engine;
    QQmlContext* declarative_script_shared_context;
    base::config& config;
    std::unique_ptr<scripting::options> options;

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
};

template<typename Space>
class platform : public platform_wrap
{
public:
    platform(Space& space)
        : platform_wrap(*space.base.options,
                        *space.options,
                        *space.base.render->options,
                        space.base.config)
        , space{space}
    {
        singleton_interface::register_shortcut
            = [this](auto const& shortcut, auto action) { register_shortcut(shortcut, action); };

        qRegisterMetaType<QList<output*>>();
        qRegisterMetaType<QList<window*>>();
        qRegisterMetaType<QVector<KWin::win::virtual_desktop*>>();

        qmlRegisterType<desktop_background_item>("org.kde.kwin", 3, 0, "DesktopBackground");
        qmlRegisterType<window_thumbnail_item>("org.kde.kwin", 3, 0, "WindowThumbnail");
        qmlRegisterType<dbus_call>("org.kde.kwin", 3, 0, "DBusCall");
        qmlRegisterType<screen_edge_handler>("org.kde.kwin", 3, 0, "ScreenEdgeHandler");
        qmlRegisterType<shortcut_handler>("org.kde.kwin", 3, 0, "ShortcutHandler");
        qmlRegisterType<window_model>("org.kde.kwin", 3, 0, "WindowModel");
        qmlRegisterType<window_filter_model>("org.kde.kwin", 3, 0, "WindowFilterModel");
        qmlRegisterType<virtual_desktop_model>("org.kde.kwin", 3, 0, "VirtualDesktopModel");
        qmlRegisterUncreatableType<KWin::QuickSceneView>(
            "org.kde.kwin",
            3,
            0,
            "SceneView",
            QStringLiteral("Can't instantiate an object of type SceneView"));

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
        qmlRegisterSingletonInstance("org.kde.kwin", 3, 0, "Options", options.get());

        qmlRegisterAnonymousType<output>("org.kde.kwin", 3);
        qmlRegisterAnonymousType<window>("org.kde.kwin", 3);
        qmlRegisterAnonymousType<win::virtual_desktop>("org.kde.kwin", 3);
        qmlRegisterAnonymousType<QAbstractItemModel>("org.kde.kwin", 3);

        if (auto& render = space.base.render; render->compositor->effects) {
            add_effect_loader(*render);
        }

        QObject::connect(space.base.render->compositor->qobject.get(),
                         &Space::base_t::render_t::compositor_t::qobject_t::compositingToggled,
                         this,
                         [this](bool on) {
                             if (on) {
                                 add_effect_loader(*this->space.base.render);
                             }
                         });

        // TODO Plasma 6: Drop context properties.
        qt_space = std::make_unique<template_space<qt_script_space, Space>>(&space);
        decl_space = std::make_unique<template_space<declarative_script_space, Space>>(&space);

        // Start the scripting platform, but first process all events.
        // TODO(romangg): Can we also do this through a simple call?
        QMetaObject::invokeMethod(this, "start", Qt::QueuedConnection);
    }

    ~platform()
    {
        singleton_interface::register_shortcut = {};
    }

    qt_script_space* workspaceWrapper() const override
    {
        return qt_space.get();
    }

    uint32_t reserve(win::electric_border border,
                     std::function<bool(win::electric_border)> callback) override
    {
        return space.edges->reserve(border, callback);
    }

    void unreserve(win::electric_border border, uint32_t id) override
    {
        space.edges->unreserve(border, id);
    }

    void reserve_touch(win::electric_border border, QAction* action) override
    {
        space.edges->reserveTouch(border, action);
    }

    void register_shortcut(QKeySequence const& shortcut, QAction* action) override
    {
        space.base.input->shortcuts->register_keyboard_shortcut(action, {shortcut});
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
        auto const id
            = std::visit(overload{[](auto&& win) { return win->meta.internal_id; }}, window);
        auto window_it = std::find_if(
            w_wins.cbegin(), w_wins.cend(), [&id](auto win) { return win->internalId() == id; });
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
