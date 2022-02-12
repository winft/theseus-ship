/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "osd_notification.h"

#include "config-kwin.h"

#include "input/event.h"
#include "input/event_spy.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
#include "main.h"

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

namespace KWin::win
{

class osd_notification_input_spy : public input::event_spy
{
public:
    explicit osd_notification_input_spy(osd_notification* parent);

    void motion(input::motion_event const& event) override;

private:
    osd_notification* m_parent;
};

osd_notification_input_spy::osd_notification_input_spy(osd_notification* parent)
    : m_parent(parent)
{
}

void osd_notification_input_spy::motion(input::motion_event const& /*event*/)
{
    auto const pos = kwinApp()->input->redirect->pointer()->pos();
    m_parent->setContainsPointer(m_parent->geometry().contains(pos.toPoint()));
}

osd_notification::osd_notification(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    QObject::connect(
        m_timer, &QTimer::timeout, this, std::bind(&osd_notification::setVisible, this, false));
    QObject::connect(this, &osd_notification::visibleChanged, this, [this] {
        if (m_visible) {
            show();
        } else {
            m_timer->stop();
            m_spy.reset();
            m_containsPointer = false;
        }
    });
}

osd_notification::~osd_notification()
{
    if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
        win->hide();
        win->destroy();
    }
}

void osd_notification::setConfig(KSharedConfigPtr config)
{
    m_config = config;
}

void osd_notification::setEngine(QQmlEngine* engine)
{
    m_qmlEngine = engine;
}

bool osd_notification::isVisible() const
{
    return m_visible;
}

void osd_notification::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;
    Q_EMIT visibleChanged();
}

QString osd_notification::message() const
{
    return m_message;
}

void osd_notification::setMessage(const QString& message)
{
    if (m_message == message) {
        return;
    }

    m_message = message;
    Q_EMIT messageChanged();
}

QString osd_notification::iconName() const
{
    return m_iconName;
}

void osd_notification::setIconName(const QString& iconName)
{
    if (m_iconName == iconName) {
        return;
    }

    m_iconName = iconName;
    Q_EMIT iconNameChanged();
}

int osd_notification::timeout() const
{
    return m_timer->interval();
}

void osd_notification::setTimeout(int timeout)
{
    if (m_timer->interval() == timeout) {
        return;
    }

    m_timer->setInterval(timeout);
    Q_EMIT timeoutChanged();
}

void osd_notification::show()
{
    assert(m_visible);

    ensureQmlContext();
    ensureQmlComponent();
    createInputSpy();

    if (m_timer->interval() != 0) {
        m_timer->start();
    }
}

void osd_notification::ensureQmlContext()
{
    assert(m_qmlEngine);

    if (m_qmlContext) {
        return;
    }

    m_qmlContext.reset(new QQmlContext(m_qmlEngine));
    m_qmlContext->setContextProperty(QStringLiteral("osd"), this);
}

void osd_notification::ensureQmlComponent()
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

void osd_notification::createInputSpy()
{
    assert(!m_spy);

    auto win = qobject_cast<QQuickWindow*>(m_mainItem.get());
    if (!win) {
        return;
    }

    m_spy.reset(new osd_notification_input_spy(this));
    kwinApp()->input->redirect->installInputEventSpy(m_spy.get());

    if (!m_animation) {
        m_animation = new QPropertyAnimation(win, "opacity", this);
        m_animation->setStartValue(1.0);
        m_animation->setEndValue(0.0);
        m_animation->setDuration(250);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);
    }
}

QRect osd_notification::geometry() const
{
    if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
        return win->geometry();
    }
    return QRect();
}

void osd_notification::setContainsPointer(bool contains)
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

void osd_notification::setSkipCloseAnimation(bool skip)
{
    if (auto win = qobject_cast<QQuickWindow*>(m_mainItem.get())) {
        win->setProperty("KWIN_SKIP_CLOSE_ANIMATION", skip);
    }
}

}
