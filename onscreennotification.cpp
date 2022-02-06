/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "onscreennotification.h"
#include "input/event.h"
#include "input/event_spy.h"
#include "input/redirect.h"
#include "main.h"
#include <config-kwin.h>
#include <input/pointer_redirect.h>

#include <QPropertyAnimation>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <KConfigGroup>

#include <cassert>
#include <functional>

namespace KWin
{

class OnScreenNotificationInputEventSpy : public input::event_spy
{
public:
    explicit OnScreenNotificationInputEventSpy(OnScreenNotification* parent);

    void motion(input::motion_event const& event) override;

private:
    OnScreenNotification* m_parent;
};

OnScreenNotificationInputEventSpy::OnScreenNotificationInputEventSpy(OnScreenNotification* parent)
    : m_parent(parent)
{
}

void OnScreenNotificationInputEventSpy::motion(input::motion_event const& /*event*/)
{
    auto const pos = kwinApp()->input->redirect->pointer()->pos();
    m_parent->setContainsPointer(m_parent->geometry().contains(pos.toPoint()));
}

OnScreenNotification::OnScreenNotification(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    QObject::connect(
        m_timer, &QTimer::timeout, this, std::bind(&OnScreenNotification::setVisible, this, false));
    QObject::connect(this, &OnScreenNotification::visibleChanged, this, [this] {
        if (m_visible) {
            show();
        } else {
            m_timer->stop();
            m_spy.reset();
            m_containsPointer = false;
        }
    });
}

OnScreenNotification::~OnScreenNotification()
{
    if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
        win->hide();
        win->destroy();
    }
}

void OnScreenNotification::setConfig(KSharedConfigPtr config)
{
    m_config = config;
}

void OnScreenNotification::setEngine(QQmlEngine* engine)
{
    m_qmlEngine = engine;
}

bool OnScreenNotification::isVisible() const
{
    return m_visible;
}

void OnScreenNotification::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;
    Q_EMIT visibleChanged();
}

QString OnScreenNotification::message() const
{
    return m_message;
}

void OnScreenNotification::setMessage(const QString& message)
{
    if (m_message == message) {
        return;
    }

    m_message = message;
    Q_EMIT messageChanged();
}

QString OnScreenNotification::iconName() const
{
    return m_iconName;
}

void OnScreenNotification::setIconName(const QString& iconName)
{
    if (m_iconName == iconName) {
        return;
    }

    m_iconName = iconName;
    Q_EMIT iconNameChanged();
}

int OnScreenNotification::timeout() const
{
    return m_timer->interval();
}

void OnScreenNotification::setTimeout(int timeout)
{
    if (m_timer->interval() == timeout) {
        return;
    }

    m_timer->setInterval(timeout);
    Q_EMIT timeoutChanged();
}

void OnScreenNotification::show()
{
    assert(m_visible);

    ensureQmlContext();
    ensureQmlComponent();
    createInputSpy();

    if (m_timer->interval() != 0) {
        m_timer->start();
    }
}

void OnScreenNotification::ensureQmlContext()
{
    assert(m_qmlEngine);

    if (m_qmlContext) {
        return;
    }

    m_qmlContext.reset(new QQmlContext(m_qmlEngine));
    m_qmlContext->setContextProperty(QStringLiteral("osd"), this);
}

void OnScreenNotification::ensureQmlComponent()
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

void OnScreenNotification::createInputSpy()
{
    assert(!m_spy);

    auto win = qobject_cast<QQuickWindow*>(m_mainItem.get());
    if (!win) {
        return;
    }

    m_spy.reset(new OnScreenNotificationInputEventSpy(this));
    kwinApp()->input->redirect->installInputEventSpy(m_spy.get());

    if (!m_animation) {
        m_animation = new QPropertyAnimation(win, "opacity", this);
        m_animation->setStartValue(1.0);
        m_animation->setEndValue(0.0);
        m_animation->setDuration(250);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);
    }
}

QRect OnScreenNotification::geometry() const
{
    if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
        return win->geometry();
    }
    return QRect();
}

void OnScreenNotification::setContainsPointer(bool contains)
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

void OnScreenNotification::setSkipCloseAnimation(bool skip)
{
    if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
        win->setProperty("KWIN_SKIP_CLOSE_ANIMATION", skip);
    }
}

}
