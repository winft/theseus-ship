/*
    SPDX-FileCopyrightText: 2016 Martin Graesslin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only

*/
#include "integration/lib/catch_macros.h"

#include "input/event_spy.h"
#include "win/osd_notification.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QQmlEngine>
#include <QSignalSpy>

namespace KWin::detail::test
{

class mock_pointer
{
public:
    QPointF pos() const
    {
        return {};
    }
};

class mock_redirect
{
public:
    using type = mock_redirect;
    using event_spy_t = input::event_spy<type>;

    mock_redirect()
        : pointer{std::make_unique<mock_pointer>()}
    {
    }

    std::vector<input::event_spy<mock_redirect>*> m_spies;
    std::unique_ptr<mock_pointer> pointer;
};

TEST_CASE("on screen notifications", "[unit],[win]")
{
    mock_redirect redirect;
    win::osd_notification<mock_redirect> notification(redirect);

    SECTION("show")
    {
        auto config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
        KConfigGroup group = config->group("OnScreenNotification");
        group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
        group.sync();
        notification.m_config = config;
        notification.m_qmlEngine = new QQmlEngine(notification.qobject.get());
        notification.qobject->setMessage(QStringLiteral("Some text so that we see it in the test"));

        QSignalSpy visibleChangedSpy(notification.qobject.get(),
                                     &win::osd_notification_qobject::visibleChanged);
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

    SECTION("timeout")
    {
        QSignalSpy timeoutChangedSpy(notification.qobject.get(),
                                     &win::osd_notification_qobject::timeoutChanged);
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

    SECTION("icon name")
    {
        QSignalSpy iconNameChangedSpy(notification.qobject.get(),
                                      &win::osd_notification_qobject::iconNameChanged);
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

    SECTION("message")
    {
        QSignalSpy messageChangedSpy(notification.qobject.get(),
                                     &win::osd_notification_qobject::messageChanged);
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
}

}
