/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "js_engine_global_methods_wrapper.h"

#include "script.h"
#include "scripting_logging.h"

#include <KConfigGroup>
#include <QAction>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>

namespace KWin::scripting
{

js_engine_global_methods_wrapper::js_engine_global_methods_wrapper(declarative_script* parent)
    : QObject(parent)
    , m_script(parent)
{
}

js_engine_global_methods_wrapper::~js_engine_global_methods_wrapper()
{
}

QVariant js_engine_global_methods_wrapper::readConfig(const QString& key, QVariant defaultValue)
{
    return m_script->config().readEntry(key, defaultValue);
}

void js_engine_global_methods_wrapper::registerWindow(QQuickWindow* window)
{
    QPointer<QQuickWindow> guard = window;
    connect(
        window,
        &QWindow::visibilityChanged,
        this,
        [guard](QWindow::Visibility visibility) {
            if (guard && visibility == QWindow::Hidden) {
                guard->destroy();
            }
        },
        Qt::QueuedConnection);
}

}
