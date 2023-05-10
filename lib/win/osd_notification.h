/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"
#include "kwin_export.h"

#include <epoxy/gl.h>

#include <KConfigGroup>
#include <KSharedConfig>
#include <QObject>
#include <QPropertyAnimation>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <cassert>
#include <functional>
#include <memory>

namespace KWin::win
{

template<typename Osd, typename Input>
class osd_notification_input_spy : public Input::event_spy_t
{
public:
    using abstract_type = typename Input::event_spy_t;

    explicit osd_notification_input_spy(Osd& osd)
        : abstract_type(osd.input)
        , osd{osd}
    {
    }

    void motion(typename abstract_type::motion_event_t const& /*event*/) override
    {
        auto const pos = this->redirect.pointer->pos();
        osd.setContainsPointer(osd.geometry().contains(pos.toPoint()));
    }

private:
    Osd& osd;
};

class KWIN_EXPORT osd_notification_qobject : public QObject
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

template<typename Input>
class osd_notification
{
public:
    using type = osd_notification<Input>;
    using input_t = Input;

    osd_notification(Input& input)
        : timer{std::make_unique<QTimer>()}
        , qobject{std::make_unique<osd_notification_qobject>(*timer)}
        , input{input}
    {
        timer->setSingleShot(true);
        QObject::connect(
            timer.get(), &QTimer::timeout, qobject.get(), [this] { qobject->setVisible(false); });
        QObject::connect(
            qobject.get(), &osd_notification_qobject::visibleChanged, qobject.get(), [this] {
                if (qobject->m_visible) {
                    show();
                } else {
                    timer->stop();
                    m_spy.reset();
                    m_containsPointer = false;
                }
            });
    }

    ~osd_notification()
    {
        if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
            win->hide();
            win->destroy();
        }
    }

    QRect geometry() const
    {
        if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
            return win->geometry();
        }
        return QRect();
    }

    void setContainsPointer(bool contains)
    {
        if (m_containsPointer == contains) {
            return;
        }
        m_containsPointer = contains;
        if (!m_animation) {
            return;
        }
        m_animation->setDirection(m_containsPointer ? QAbstractAnimation::Forward
                                                    : QAbstractAnimation::Backward);
        m_animation->start();
    }

    void setSkipCloseAnimation(bool skip)
    {
        if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
            win->setProperty("KWIN_SKIP_CLOSE_ANIMATION", skip);
        }
    }

    std::unique_ptr<QTimer> timer;
    std::unique_ptr<osd_notification_qobject> qobject;
    Input& input;

    KSharedConfigPtr m_config;
    QQmlEngine* m_qmlEngine{nullptr};

private:
    using input_spy = osd_notification_input_spy<type, Input>;

    void show()
    {
        assert(qobject->m_visible);

        ensureQmlContext();
        ensureQmlComponent();
        createInputSpy();

        if (timer->interval() != 0) {
            timer->start();
        }
    }

    void ensureQmlContext()
    {
        assert(m_qmlEngine);

        if (m_qmlContext) {
            return;
        }

        m_qmlContext.reset(new QQmlContext(m_qmlEngine));
        m_qmlContext->setContextProperty(QStringLiteral("osd"), qobject.get());
    }

    void ensureQmlComponent()
    {
        assert(m_config);
        assert(m_qmlEngine);

        if (m_qmlComponent) {
            return;
        }

        m_qmlComponent.reset(new QQmlComponent(m_qmlEngine));

        auto const fileName = QStandardPaths::locate(
            QStandardPaths::GenericDataLocation,
            m_config->group(QStringLiteral("OnScreenNotification"))
                .readEntry("QmlPath",
                           QStringLiteral(KWIN_NAME "/onscreennotification/plasma/main.qml")));

        if (fileName.isEmpty()) {
            return;
        }

        m_qmlComponent->loadUrl(QUrl::fromLocalFile(fileName));

        if (!m_qmlComponent->isError()) {
            m_mainItem.reset(m_qmlComponent->create(m_qmlContext.get()));
        } else {
            m_qmlComponent.reset();
        }
    }

    void createInputSpy()
    {
        assert(!m_spy);

        auto win = qobject_cast<QQuickWindow*>(m_mainItem.get());
        if (!win) {
            return;
        }

        m_spy = std::make_unique<input_spy>(*this);
        input.m_spies.push_back(m_spy.get());

        if (!m_animation) {
            m_animation = new QPropertyAnimation(win, "opacity", qobject.get());
            m_animation->setStartValue(1.0);
            m_animation->setEndValue(0.0);
            m_animation->setDuration(250);
            m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        }
    }

    std::unique_ptr<QQmlComponent> m_qmlComponent;
    std::unique_ptr<QQmlContext> m_qmlContext;
    std::unique_ptr<QObject> m_mainItem;
    std::unique_ptr<input_spy> m_spy;
    QPropertyAnimation* m_animation{nullptr};
    bool m_containsPointer{false};
};

}
