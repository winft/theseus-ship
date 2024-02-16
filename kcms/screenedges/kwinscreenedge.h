/*
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __KWINSCREENEDGE_H__
#define __KWINSCREENEDGE_H__

#include <QWidget>

#include <como/win/types.h>

namespace theseus_ship
{

class Monitor;

class KWinScreenEdge : public QWidget
{
    Q_OBJECT

public:
    explicit KWinScreenEdge(QWidget* parent = nullptr);
    ~KWinScreenEdge() override;

    void monitorHideEdge(como::win::electric_border border, bool hidden);
    void monitorEnableEdge(como::win::electric_border border, bool enabled);

    void monitorAddItem(const QString& item);
    void monitorItemSetEnabled(int index, bool enabled);

    QList<como::win::electric_border> monitorCheckEffectHasEdge(int index) const;
    QList<int> monitorCheckEffectHasEdgeInt(int index) const;
    int selectedEdgeItem(como::win::electric_border border) const;

    void monitorChangeEdge(como::win::electric_border border, int index);
    void monitorChangeEdge(const QList<int>& borderList, int index);

    void monitorChangeDefaultEdge(como::win::electric_border border, int index);
    void monitorChangeDefaultEdge(const QList<int>& borderList, int index);

    // revert to reference settings and assess for saveNeeded and default changed
    virtual void reload();
    // reset to default settings and assess for saveNeeded and default changed
    virtual void setDefaults();

public Q_SLOTS:
    void onChanged();
    void createConnection();

Q_SIGNALS:
    void saveNeededChanged(bool isNeeded);
    void defaultChanged(bool isDefault);

private:
    virtual Monitor* monitor() const = 0;
    virtual bool isSaveNeeded() const;
    virtual bool isDefault() const;

    // internal use, return Monitor::None if border equals ELECTRIC_COUNT or ElectricNone
    static int electricBorderToMonitorEdge(como::win::electric_border border);

private:
    QHash<como::win::electric_border, int> m_reference; // reference settings
    QHash<como::win::electric_border, int> m_default;   // default settings
};

} // namespace

#endif
