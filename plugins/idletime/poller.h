/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef POLLER_H
#define POLLER_H

#include <private/kabstractidletimepoller_p.h>

#include <QHash>

namespace KWin::input
{
struct idle_listener;
}

class KWinIdleTimePoller : public KAbstractIdleTimePoller
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KAbstractIdleTimePoller_iid FILE "kwin.json")
    Q_INTERFACES(KAbstractIdleTimePoller)

public:
    KWinIdleTimePoller(QObject *parent = nullptr);
    ~KWinIdleTimePoller() override;

    bool isAvailable() override;
    bool setUpPoller() override;
    void unloadPoller() override;

public Q_SLOTS:
    void addTimeout(int nextTimeout) override;
    void removeTimeout(int nextTimeout) override;
    QList<int> timeouts() const override;
    int forcePollRequest() override;
    void catchIdleEvent() override;
    void stopCatchingIdleEvents() override;
    void simulateUserActivity() override;

private:
    void cleanup();

    KWin::input::idle_listener* m_catchResumeTimeout{nullptr};
    QHash<int, KWin::input::idle_listener*> m_timeouts;
};

#endif
