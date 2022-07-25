/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>

#include <KSharedConfig>
#include <memory>

class QPropertyAnimation;
class QTimer;
class QQmlComponent;
class QQmlContext;
class QQmlEngine;

namespace KWin::win
{

class osd_notification_input_spy;

class osd_notification_qobject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)

public:
    osd_notification_qobject(QTimer& timer);

    bool isVisible() const;
    QString message() const;
    QString iconName() const;
    int timeout() const;

    void setVisible(bool m_visible);
    void setMessage(const QString& message);
    void setIconName(const QString& iconName);
    void setTimeout(int timeout);

    bool m_visible{false};
    QString m_message;
    QString m_iconName;

Q_SIGNALS:
    void visibleChanged();
    void messageChanged();
    void iconNameChanged();
    void timeoutChanged();

private:
    QTimer& timer;
};

class KWIN_EXPORT osd_notification
{
public:
    osd_notification();
    ~osd_notification();

    QRect geometry() const;

    void setContainsPointer(bool contains);
    void setSkipCloseAnimation(bool skip);

    std::unique_ptr<QTimer> timer;
    std::unique_ptr<osd_notification_qobject> qobject;

    KSharedConfigPtr m_config;
    QQmlEngine* m_qmlEngine{nullptr};

private:
    void show();
    void ensureQmlContext();
    void ensureQmlComponent();
    void createInputSpy();

    std::unique_ptr<QQmlComponent> m_qmlComponent;
    std::unique_ptr<QQmlContext> m_qmlContext;
    std::unique_ptr<QObject> m_mainItem;
    std::unique_ptr<osd_notification_input_spy> m_spy;
    QPropertyAnimation* m_animation{nullptr};
    bool m_containsPointer{false};
};

}
