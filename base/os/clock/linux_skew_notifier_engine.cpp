/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "linux_skew_notifier_engine.h"

#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <unistd.h>

#ifndef TFD_TIMER_CANCEL_ON_SET // only available in newer glib
#define TFD_TIMER_CANCEL_ON_SET (1 << 1)
#endif

namespace KWin::base::os::clock
{

std::unique_ptr<linux_skew_notifier_engine> linux_skew_notifier_engine::create()
{
    const int fd = timerfd_create(CLOCK_REALTIME, O_CLOEXEC | O_NONBLOCK);
    if (fd == -1) {
        qWarning("Couldn't create clock skew notifier engine: %s", strerror(errno));
        return nullptr;
    }

    const itimerspec spec = {};
    const int ret
        = timerfd_settime(fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &spec, nullptr);
    if (ret == -1) {
        qWarning("Couldn't create clock skew notifier engine: %s", strerror(errno));
        close(fd);
        return nullptr;
    }

    return std::make_unique<linux_skew_notifier_engine>(fd);
}

linux_skew_notifier_engine::linux_skew_notifier_engine(int fd)
    : skew_notifier_engine()
    , m_fd(fd)
{
    const QSocketNotifier* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier,
            &QSocketNotifier::activated,
            this,
            &linux_skew_notifier_engine::handle_timer_cancelled);
}

linux_skew_notifier_engine::~linux_skew_notifier_engine()
{
    close(m_fd);
}

void linux_skew_notifier_engine::handle_timer_cancelled()
{
    uint64_t expirationCount;
    read(m_fd, &expirationCount, sizeof(expirationCount));

    Q_EMIT skewed();
}

}
