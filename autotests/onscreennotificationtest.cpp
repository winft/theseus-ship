/*
 * Copyright 2016  Martin Graesslin <mgraesslin@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "onscreennotificationtest.h"

#include "input/redirect.h"
#include "win/osd_notification.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

QTEST_MAIN(OnScreenNotificationTest);

namespace KWin
{

void input::redirect::installInputEventSpy(input::event_spy* spy)
{
    Q_UNUSED(spy);
}

void input::redirect::uninstallInputEventSpy(input::event_spy* spy)
{
    Q_UNUSED(spy);
}

}

void OnScreenNotificationTest::show()
{
    KWin::win::osd_notification notification;
    auto config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup group = config->group("OnScreenNotification");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();
    notification.m_config = config;
    notification.m_qmlEngine = new QQmlEngine(notification.qobject.get());
    notification.qobject->setMessage(QStringLiteral("Some text so that we see it in the test"));

    QSignalSpy visibleChangedSpy(notification.qobject.get(),
                                 &KWin::win::osd_notification_qobject::visibleChanged);
    QCOMPARE(notification.qobject->isVisible(), false);
    notification.qobject->setVisible(true);
    QCOMPARE(notification.qobject->isVisible(), true);
    QCOMPARE(visibleChangedSpy.count(), 1);

    // show again should not trigger
    notification.qobject->setVisible(true);
    QCOMPARE(visibleChangedSpy.count(), 1);

    // timer should not have hidden
    QTest::qWait(500);
    QCOMPARE(notification.qobject->isVisible(), true);

    // hide again
    notification.qobject->setVisible(false);
    QCOMPARE(notification.qobject->isVisible(), false);
    QCOMPARE(visibleChangedSpy.count(), 2);

    // now show with timer
    notification.qobject->setTimeout(250);
    notification.qobject->setVisible(true);
    QCOMPARE(notification.qobject->isVisible(), true);
    QCOMPARE(visibleChangedSpy.count(), 3);
    QVERIFY(visibleChangedSpy.wait());
    QCOMPARE(notification.qobject->isVisible(), false);
    QCOMPARE(visibleChangedSpy.count(), 4);
}

void OnScreenNotificationTest::timeout()
{
    KWin::win::osd_notification notification;
    QSignalSpy timeoutChangedSpy(notification.qobject.get(),
                                 &KWin::win::osd_notification_qobject::timeoutChanged);
    QCOMPARE(notification.qobject->timeout(), 0);
    notification.qobject->setTimeout(1000);
    QCOMPARE(notification.qobject->timeout(), 1000);
    QCOMPARE(timeoutChangedSpy.count(), 1);
    notification.qobject->setTimeout(1000);
    QCOMPARE(timeoutChangedSpy.count(), 1);
    notification.qobject->setTimeout(0);
    QCOMPARE(notification.qobject->timeout(), 0);
    QCOMPARE(timeoutChangedSpy.count(), 2);
}

void OnScreenNotificationTest::iconName()
{
    KWin::win::osd_notification notification;
    QSignalSpy iconNameChangedSpy(notification.qobject.get(),
                                  &KWin::win::osd_notification_qobject::iconNameChanged);
    QVERIFY(iconNameChangedSpy.isValid());
    QCOMPARE(notification.qobject->iconName(), QString());
    notification.qobject->setIconName(QStringLiteral("foo"));
    QCOMPARE(notification.qobject->iconName(), QStringLiteral("foo"));
    QCOMPARE(iconNameChangedSpy.count(), 1);
    notification.qobject->setIconName(QStringLiteral("foo"));
    QCOMPARE(iconNameChangedSpy.count(), 1);
    notification.qobject->setIconName(QStringLiteral("bar"));
    QCOMPARE(notification.qobject->iconName(), QStringLiteral("bar"));
    QCOMPARE(iconNameChangedSpy.count(), 2);
}

void OnScreenNotificationTest::message()
{
    KWin::win::osd_notification notification;
    QSignalSpy messageChangedSpy(notification.qobject.get(),
                                 &KWin::win::osd_notification_qobject::messageChanged);
    QVERIFY(messageChangedSpy.isValid());
    QCOMPARE(notification.qobject->message(), QString());
    notification.qobject->setMessage(QStringLiteral("foo"));
    QCOMPARE(notification.qobject->message(), QStringLiteral("foo"));
    QCOMPARE(messageChangedSpy.count(), 1);
    notification.qobject->setMessage(QStringLiteral("foo"));
    QCOMPARE(messageChangedSpy.count(), 1);
    notification.qobject->setMessage(QStringLiteral("bar"));
    QCOMPARE(notification.qobject->message(), QStringLiteral("bar"));
    QCOMPARE(messageChangedSpy.count(), 2);
}
