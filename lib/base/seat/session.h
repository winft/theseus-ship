/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>

namespace KWin::base::seat
{

class KWIN_EXPORT session : public QObject
{
    Q_OBJECT
public:
    session();

    virtual bool isConnected() const = 0;
    virtual bool hasSessionControl() const = 0;
    virtual bool isActiveSession() const = 0;
    virtual int vt() const = 0;
    virtual void switchVirtualTerminal(quint32 vtNr) = 0;

    virtual int takeDevice(const char* path) = 0;
    virtual void releaseDevice(int fd) = 0;

    virtual const QString seat() const = 0;

Q_SIGNALS:
    void connectedChanged();
    void sessionActiveChanged(bool);
    void virtualTerminalChanged(int);
};

}
