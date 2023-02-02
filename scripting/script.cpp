/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "script.h"

#include "js_engine_global_methods_wrapper.h"
#include "platform.h"
#include "screen_edge_item.h"
#include "script_timer.h"
#include "scripting_logging.h"
#include "space.h"
#include "utils.h"
#include "window.h"

#include "base/options.h"

#include <KConfigGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QFutureWatcher>
#include <QMenu>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtConcurrentRun>

#include "scriptadaptor.h"

namespace KWin::scripting
{

static QRect scriptValueToRect(const QJSValue& value)
{
    return QRect(value.property(QStringLiteral("x")).toInt(),
                 value.property(QStringLiteral("y")).toInt(),
                 value.property(QStringLiteral("width")).toInt(),
                 value.property(QStringLiteral("height")).toInt());
}

static QPoint scriptValueToPoint(const QJSValue& value)
{
    return QPoint(value.property(QStringLiteral("x")).toInt(),
                  value.property(QStringLiteral("y")).toInt());
}

static QSize scriptValueToSize(const QJSValue& value)
{
    return QSize(value.property(QStringLiteral("width")).toInt(),
                 value.property(QStringLiteral("height")).toInt());
}

abstract_script::abstract_script(int id,
                                 QString scriptName,
                                 QString pluginName,
                                 base::config& config,
                                 QObject* parent)
    : QObject(parent)
    , m_scriptId(id)
    , m_fileName(scriptName)
    , m_pluginName(pluginName)
    , m_running(false)
    , base_config{config}
{
    if (m_pluginName.isNull()) {
        m_pluginName = scriptName;
    }

    new ScriptAdaptor(this);
    QDBusConnection::sessionBus().registerObject(
        QLatin1Char('/') + QString::number(scriptId()), this, QDBusConnection::ExportAdaptors);
}

abstract_script::~abstract_script()
{
}

KConfigGroup abstract_script::config() const
{
    return base_config.main->group(QLatin1String("Script-") + m_pluginName);
}

void abstract_script::stop()
{
    deleteLater();
}

script::script(int id,
               QString scriptName,
               QString pluginName,
               scripting::platform_wrap& platform,
               base::options& options,
               base::config& config,
               QObject* parent)
    : abstract_script(id, scriptName, pluginName, config, parent)
    , m_engine(new QJSEngine(this))
    , platform{platform}
    , options{options}
{
    // TODO: Remove in kwin 6. We have these converters only for compatibility reasons.
    if (!QMetaType::hasRegisteredConverterFunction<QJSValue, QRect>()) {
        QMetaType::registerConverter<QJSValue, QRect>(scriptValueToRect);
    }
    if (!QMetaType::hasRegisteredConverterFunction<QJSValue, QPoint>()) {
        QMetaType::registerConverter<QJSValue, QPoint>(scriptValueToPoint);
    }
    if (!QMetaType::hasRegisteredConverterFunction<QJSValue, QSize>()) {
        QMetaType::registerConverter<QJSValue, QSize>(scriptValueToSize);
    }

    qRegisterMetaType<QList<window*>>();
    qRegisterMetaType<QVector<KWin::win::virtual_desktop*>>();
}

script::~script()
{
    for (auto& [border, id] : reserved_borders) {
        platform.unreserve(border, id);
    }
}

void script::run()
{
    if (running() || m_starting) {
        return;
    }

    if (calledFromDBus()) {
        m_invocationContext = message();
        setDelayedReply(true);
    }

    m_starting = true;
    QFutureWatcher<QByteArray>* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, &script::slotScriptLoadedFromFile);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    watcher->setFuture(QtConcurrent::run(this, &script::loadScriptFromFile, fileName()));
#else
    watcher->setFuture(QtConcurrent::run(&script::loadScriptFromFile, this, fileName()));
#endif
}

QByteArray script::loadScriptFromFile(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    QByteArray result(file.readAll());
    return result;
}

void script::slotScriptLoadedFromFile()
{
    QFutureWatcher<QByteArray>* watcher = dynamic_cast<QFutureWatcher<QByteArray>*>(sender());
    if (!watcher) {
        // not invoked from a QFutureWatcher
        return;
    }
    if (watcher->result().isNull()) {
        // do not load empty script
        deleteLater();
        watcher->deleteLater();

        if (m_invocationContext.type() == QDBusMessage::MethodCallMessage) {
            auto reply = m_invocationContext.createErrorReply(
                "org.kde.kwin.Scripting.FileError", QString("Could not open %1").arg(fileName()));
            QDBusConnection::sessionBus().send(reply);
            m_invocationContext = QDBusMessage();
        }

        return;
    }

    // Install console functions (e.g. console.assert(), console.log(), etc).
    m_engine->installExtensions(QJSEngine::ConsoleExtension);

    // Make the timer visible to QJSEngine.
    QJSValue timerMetaObject = m_engine->newQMetaObject(&script_timer::staticMetaObject);
    m_engine->globalObject().setProperty("QTimer", timerMetaObject);

    // Expose enums.
    m_engine->globalObject().setProperty(
        QStringLiteral("KWin"), m_engine->newQMetaObject(&qt_script_space::staticMetaObject));

    // Make the options object visible to QJSEngine.
    QJSValue optionsObject = m_engine->newQObject(options.qobject.get());
    QQmlEngine::setObjectOwnership(options.qobject.get(), QQmlEngine::CppOwnership);
    m_engine->globalObject().setProperty(QStringLiteral("options"), optionsObject);

    // Make the workspace visible to QJSEngine.
    QJSValue workspaceObject = m_engine->newQObject(platform.workspaceWrapper());
    QQmlEngine::setObjectOwnership(platform.workspaceWrapper(), QQmlEngine::CppOwnership);
    m_engine->globalObject().setProperty(QStringLiteral("workspace"), workspaceObject);

    QJSValue self = m_engine->newQObject(this);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);

    static const QStringList globalProperties{
        QStringLiteral("readConfig"),
        QStringLiteral("callDBus"),

        QStringLiteral("registerShortcut"),
        QStringLiteral("registerScreenEdge"),
        QStringLiteral("unregisterScreenEdge"),
        QStringLiteral("registerTouchScreenEdge"),
        QStringLiteral("unregisterTouchScreenEdge"),
        QStringLiteral("registerUserActionsMenu"),
    };

    for (const QString& propertyName : globalProperties) {
        m_engine->globalObject().setProperty(propertyName, self.property(propertyName));
    }

    // Inject assertion functions. It would be better to create a module with all
    // this assert functions or just deprecate them in favor of console.assert().
    QJSValue result = m_engine->evaluate(QStringLiteral(R"(
        function assert(condition, message) {
            console.assert(condition, message || 'Assertion failed');
        }
        function assertTrue(condition, message) {
            console.assert(condition, message || 'Assertion failed');
        }
        function assertFalse(condition, message) {
            console.assert(!condition, message || 'Assertion failed');
        }
        function assertNull(value, message) {
            console.assert(value === null, message || 'Assertion failed');
        }
        function assertNotNull(value, message) {
            console.assert(value !== null, message || 'Assertion failed');
        }
        function assertEquals(expected, actual, message) {
            console.assert(expected === actual, message || 'Assertion failed');
        }
    )"));
    Q_ASSERT(!result.isError());

    result = m_engine->evaluate(QString::fromUtf8(watcher->result()), fileName());
    if (result.isError()) {
        qCWarning(KWIN_SCRIPTING,
                  "%s:%d: error: %s",
                  qPrintable(fileName()),
                  result.property(QStringLiteral("lineNumber")).toInt(),
                  qPrintable(result.property(QStringLiteral("message")).toString()));
        deleteLater();
    }

    if (m_invocationContext.type() == QDBusMessage::MethodCallMessage) {
        auto reply = m_invocationContext.createReply();
        QDBusConnection::sessionBus().send(reply);
        m_invocationContext = QDBusMessage();
    }

    watcher->deleteLater();
    setRunning(true);
    m_starting = false;
}

QVariant script::readConfig(const QString& key, const QVariant& defaultValue)
{
    return config().readEntry(key, defaultValue);
}

void script::callDBus(const QString& service,
                      const QString& path,
                      const QString& interface,
                      const QString& method,
                      const QJSValue& arg1,
                      const QJSValue& arg2,
                      const QJSValue& arg3,
                      const QJSValue& arg4,
                      const QJSValue& arg5,
                      const QJSValue& arg6,
                      const QJSValue& arg7,
                      const QJSValue& arg8,
                      const QJSValue& arg9)
{
    QJSValueList jsArguments;
    jsArguments.reserve(9);

    if (!arg1.isUndefined()) {
        jsArguments << arg1;
    }
    if (!arg2.isUndefined()) {
        jsArguments << arg2;
    }
    if (!arg3.isUndefined()) {
        jsArguments << arg3;
    }
    if (!arg4.isUndefined()) {
        jsArguments << arg4;
    }
    if (!arg5.isUndefined()) {
        jsArguments << arg5;
    }
    if (!arg6.isUndefined()) {
        jsArguments << arg6;
    }
    if (!arg7.isUndefined()) {
        jsArguments << arg7;
    }
    if (!arg8.isUndefined()) {
        jsArguments << arg8;
    }
    if (!arg9.isUndefined()) {
        jsArguments << arg9;
    }

    QJSValue callback;
    if (!jsArguments.isEmpty() && jsArguments.last().isCallable()) {
        callback = jsArguments.takeLast();
    }

    QVariantList dbusArguments;
    dbusArguments.reserve(jsArguments.count());
    for (const QJSValue& jsArgument : qAsConst(jsArguments)) {
        dbusArguments << jsArgument.toVariant();
    }

    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(dbusArguments);

    const QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(message);
    if (callback.isUndefined()) {
        return;
    }

    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher,
            &QDBusPendingCallWatcher::finished,
            this,
            [this, callback](QDBusPendingCallWatcher* self) {
                self->deleteLater();

                if (self->isError()) {
                    qCWarning(KWIN_SCRIPTING)
                        << "Received D-Bus message is error:" << self->error().message();
                    return;
                }

                QJSValueList arguments;
                const QVariantList reply = self->reply().arguments();
                for (const QVariant& variant : reply) {
                    arguments << m_engine->toScriptValue(dbusToVariant(variant));
                }

                QJSValue(callback).call(arguments);
            });
}

bool script::registerShortcut(const QString& objectName,
                              const QString& text,
                              const QString& keySequence,
                              const QJSValue& callback)
{
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("Shortcut handler must be callable"));
        return false;
    }

    QAction* action = new QAction(this);
    action->setObjectName(objectName);
    action->setText(text);
    platform.register_shortcut(keySequence, action);

    connect(action, &QAction::triggered, this, [this, action, callback]() {
        QJSValue(callback).call({m_engine->toScriptValue(action)});
    });

    return true;
}

bool script::registerScreenEdge(int edge, const QJSValue& callback)
{
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("Screen edge handler must be callable"));
        return false;
    }

    QJSValueList& callbacks = m_screenEdgeCallbacks[edge];
    if (callbacks.isEmpty()) {
        auto border = static_cast<ElectricBorder>(edge);
        auto id = platform.reserve(border, [this](auto eb) { return slotBorderActivated(eb); });
        reserved_borders.insert({border, id});
    }

    callbacks << callback;

    return true;
}

bool script::unregisterScreenEdge(int edge)
{
    auto it = m_screenEdgeCallbacks.find(edge);
    if (it == m_screenEdgeCallbacks.end()) {
        return false;
    }

    auto border = static_cast<ElectricBorder>(edge);
    if (auto it = reserved_borders.find(border); it != reserved_borders.end()) {
        platform.unreserve(border, it->second);
        reserved_borders.erase(it);
    }

    m_screenEdgeCallbacks.erase(it);
    return true;
}

bool script::registerTouchScreenEdge(int edge, const QJSValue& callback)
{
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("Touch screen edge handler must be callable"));
        return false;
    }
    if (m_touchScreenEdgeCallbacks.contains(edge)) {
        return false;
    }

    QAction* action = new QAction(this);
    platform.reserve_touch(ElectricBorder(edge), action);
    m_touchScreenEdgeCallbacks.insert(edge, action);

    connect(action, &QAction::triggered, this, [callback]() { QJSValue(callback).call(); });

    return true;
}

bool script::unregisterTouchScreenEdge(int edge)
{
    auto it = m_touchScreenEdgeCallbacks.find(edge);
    if (it == m_touchScreenEdgeCallbacks.end()) {
        return false;
    }

    delete it.value();
    m_touchScreenEdgeCallbacks.erase(it);

    return true;
}

void script::registerUserActionsMenu(const QJSValue& callback)
{
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("User action handler must be callable"));
        return;
    }
    m_userActionsMenuCallbacks.append(callback);
}

QList<QAction*> script::actionsForUserActionMenu(window* window, QMenu* parent)
{
    QList<QAction*> actions;
    actions.reserve(m_userActionsMenuCallbacks.count());

    for (QJSValue callback : qAsConst(m_userActionsMenuCallbacks)) {
        QJSValue result = callback.call({m_engine->toScriptValue(window)});
        if (result.isError()) {
            continue;
        }
        if (!result.isObject()) {
            continue;
        }
        if (QAction* action = scriptValueToAction(result, parent)) {
            actions << action;
        }
    }

    return actions;
}

bool script::slotBorderActivated(ElectricBorder border)
{
    const QJSValueList callbacks = m_screenEdgeCallbacks.value(border);
    if (callbacks.isEmpty()) {
        return false;
    }
    std::for_each(callbacks.begin(), callbacks.end(), [](QJSValue callback) { callback.call(); });
    return true;
}

QAction* script::scriptValueToAction(const QJSValue& value, QMenu* parent)
{
    const QString title = value.property(QStringLiteral("text")).toString();
    if (title.isEmpty()) {
        return nullptr;
    }

    // Either a menu or a menu item.
    const QJSValue itemsValue = value.property(QStringLiteral("items"));
    if (!itemsValue.isUndefined()) {
        return createMenu(title, itemsValue, parent);
    }

    return createAction(title, value, parent);
}

QAction* script::createAction(const QString& title, const QJSValue& item, QMenu* parent)
{
    const QJSValue callback = item.property(QStringLiteral("triggered"));
    if (!callback.isCallable()) {
        return nullptr;
    }

    const bool checkable = item.property(QStringLiteral("checkable")).toBool();
    const bool checked = item.property(QStringLiteral("checked")).toBool();

    QAction* action = new QAction(title, parent);
    action->setCheckable(checkable);
    action->setChecked(checked);

    connect(action, &QAction::triggered, this, [this, action, callback]() {
        QJSValue(callback).call({m_engine->toScriptValue(action)});
    });

    return action;
}

QAction* script::createMenu(const QString& title, const QJSValue& items, QMenu* parent)
{
    if (!items.isArray()) {
        return nullptr;
    }

    const int length = items.property(QStringLiteral("length")).toInt();
    if (!length) {
        return nullptr;
    }

    QMenu* menu = new QMenu(title, parent);
    for (int i = 0; i < length; ++i) {
        const QJSValue value = items.property(QString::number(i));
        if (!value.isObject()) {
            continue;
        }
        if (QAction* action = scriptValueToAction(value, menu)) {
            menu->addAction(action);
        }
    }

    return menu->menuAction();
}

declarative_script::declarative_script(int id,
                                       QString scriptName,
                                       QString pluginName,
                                       scripting::platform_wrap& platform,
                                       QObject* parent)
    : abstract_script(id, scriptName, pluginName, platform.config, parent)
    , m_context(new QQmlContext(platform.declarative_script_shared_context, this))
    , m_component(new QQmlComponent(platform.qml_engine, this))
{
    m_context->setContextProperty(QStringLiteral("KWin"),
                                  new js_engine_global_methods_wrapper(platform, this));
}

declarative_script::~declarative_script()
{
}

void declarative_script::run()
{
    if (running()) {
        return;
    }

    m_component->loadUrl(QUrl::fromLocalFile(fileName()));
    if (m_component->isLoading()) {
        connect(
            m_component, &QQmlComponent::statusChanged, this, &declarative_script::createComponent);
    } else {
        createComponent();
    }
}

void declarative_script::createComponent()
{
    if (m_component->isError()) {
        qCWarning(KWIN_SCRIPTING) << "Component failed to load: " << m_component->errors();
    } else {
        if (QObject* object = m_component->create(m_context)) {
            object->setParent(this);
        }
    }
    setRunning(true);
}

}
