/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QObject>
#include <QString>

namespace KWin::win
{

class KWIN_EXPORT subspace : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(uint x11DesktopNumber READ x11DesktopNumber NOTIFY x11DesktopNumberChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    explicit subspace(QObject* parent = nullptr);
    subspace(QString const& id, QObject* parent = nullptr);
    ~subspace() override;

    QString id() const
    {
        return m_id;
    }

    void setName(QString const& name);
    QString name() const
    {
        return m_name;
    }

    void setX11DesktopNumber(uint number);
    uint x11DesktopNumber() const
    {
        return m_x11DesktopNumber;
    }

Q_SIGNALS:
    void nameChanged();
    void x11DesktopNumberChanged();
    /**
     * Emitted just before the desktop gets destroyed.
     */
    void aboutToBeDestroyed();

private:
    QString m_id;
    QString m_name;
    int m_x11DesktopNumber = 0;
};

}
